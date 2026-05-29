//! DDNet account system, Rust part.
//!
//! This crate bridges the `ddnet-account-*` libraries (account client, game
//! server and the QUIC secure transport) into the C++ client and server via
//! `cxx`. It owns the async runtime that drives the network and HTTP
//! operations; the C++ side communicates with it through a command-in /
//! event-out interface that is drained once per tick.
//!
//! NOTE: DDNet compiles its C++ with `-fno-exceptions`, and `cxx` emits a
//! `throw` for every `Result<T>` return. Therefore no bridged function returns
//! `Result`; fallible operations report errors through an `error` string field
//! (empty means success) instead.

#![warn(missing_docs)]

pub mod client;
pub mod game_server;
pub mod transport;

#[cfg(test)]
mod e2e_live;

use std::sync::Arc;

pub use self::ffi::*;

// cxx 1.0.71 generates `Box::from_raw` calls for `Box<_>` returns that trip the
// `unused_must_use` lint inside the macro expansion; harmless.
#[allow(unused_must_use)]
#[cxx::bridge]
mod ffi {
    /// The kind of a [`QuicEvent`].
    enum QuicEventKind {
        /// A connection was established (`conn_id` valid; `data` holds the
        /// peer's leaf certificate DER on the server side).
        Connected,
        /// A connection closed (`data` holds a UTF-8 reason string).
        Disconnected,
        /// A reliable, ordered message arrived (`data` is the payload).
        Reliable,
        /// An unreliable datagram arrived (`data` is the payload).
        Unreliable,
    }

    /// An event drained from a [`QuicTransport`] once per tick.
    struct QuicEvent {
        /// What happened.
        kind: QuicEventKind,
        /// The connection it relates to.
        conn_id: u64,
        /// Event payload (see [`QuicEventKind`] for the meaning).
        data: Vec<u8>,
    }

    /// A freshly generated self-signed certificate and key.
    struct QuicCert {
        /// Non-empty if generation failed (then the other fields are empty).
        error: String,
        /// Certificate in DER encoding.
        cert_der: Vec<u8>,
        /// PKCS#8 private key in DER encoding.
        key_der: Vec<u8>,
        /// SHA-256 fingerprint of the certificate's `SubjectPublicKeyInfo`
        /// (32 bytes); what a client pins to identify the server.
        public_key_fingerprint: Vec<u8>,
    }

    /// Result of resolving a client certificate to an account and logging it in.
    struct AccountLogin {
        /// Non-empty if resolution/login failed.
        error: String,
        /// The account id, or 0 if the client has no account (public-key only).
        account_id: i64,
        /// The client's public-key fingerprint (32 bytes).
        public_key: Vec<u8>,
        /// True if a new account row was created by this login.
        created: bool,
    }

    /// Result of a client login attempt.
    struct AccountClientLogin {
        /// Non-empty if login failed.
        error: String,
        /// The logged-in account id (0 on failure).
        account_id: i64,
    }

    /// A freshly account-signed certificate to present to a game server, with
    /// its private key for the QUIC mutual-TLS handshake.
    struct AccountSignedCert {
        /// Non-empty if signing failed.
        error: String,
        /// The signed certificate in DER encoding.
        cert_der: Vec<u8>,
        /// The ed25519 private key (PKCS#8 DER) matching the certificate.
        key_der: Vec<u8>,
    }

    extern "Rust" {
        /// Returns a human readable version string for the account bridge.
        fn ddnet_accounts_version() -> String;

        /// Client-side account operations (login, sign).
        type AccountClient;

        /// Connects an account client to `account_server_url`, storing session
        /// data under `secure_dir`. Always returns a handle; check `error`.
        fn account_client_open(account_server_url: &str, secure_dir: &str) -> Box<AccountClient>;

        /// Empty if opened successfully, else the reason.
        fn error(self: &AccountClient) -> String;

        /// Requests an emailed login token for `email`. The user receives a
        /// code by email and passes it to `login`. Returns "" on success.
        fn request_login_token_email(self: &AccountClient, email: &str) -> String;

        /// Logs in with the emailed token (creating the account on first login)
        /// and persists the session.
        fn login(self: &AccountClient, token_hex: String) -> AccountClientLogin;

        /// Produces a fresh account-signed certificate to present over QUIC.
        fn sign(self: &AccountClient) -> AccountSignedCert;

        /// Logs out the current session. Returns "" on success.
        fn logout(self: &AccountClient) -> String;

        /// Game-server account database + certificate verifier.
        type AccountGameServer;

        /// Opens (creating if needed) the SQLite account database at `path` and
        /// prepares it. Always returns a handle; check [`AccountGameServer::error`].
        fn account_game_server_open(sqlite_path: &str) -> Box<AccountGameServer>;

        /// Empty if opened successfully, else the reason.
        fn error(self: &AccountGameServer) -> String;

        /// Drops all trusted account-server CA keys.
        fn clear_ca_certs(self: &mut AccountGameServer);

        /// Adds a trusted account-server CA certificate (DER). Returns false if
        /// the certificate is unusable.
        fn add_ca_cert(self: &mut AccountGameServer, cert_der: &[u8]) -> bool;

        /// Downloads + trusts the account server's CA certificates from
        /// `account_server_url`. Returns the number loaded, or -1 on error.
        fn load_account_ca_certs(self: &mut AccountGameServer, account_server_url: &str) -> i64;

        /// Resolves the client's certificate to an account id and auto-logs the
        /// user in (creating the account row on first login).
        fn login_by_cert(self: &AccountGameServer, peer_cert_der: &[u8]) -> AccountLogin;

        /// Renames the account user. `account_id` 0 means public-key-only.
        /// Returns an empty string on success, else the error.
        fn rename(self: &AccountGameServer, account_id: i64, public_key: &[u8], name: &str) -> String;

        /// A pollable QUIC endpoint (client or server).
        type QuicTransport;

        /// Generates an ed25519 self-signed certificate + key. On failure the
        /// returned [`QuicCert`] has a non-empty `error`.
        fn quic_generate_self_signed() -> QuicCert;

        /// Loads the server's persistent identity from `cert_path`/`key_path`,
        /// generating and saving a new one if absent. Keeps the server's
        /// fingerprint stable across restarts. On failure `error` is non-empty.
        fn quic_load_or_generate_identity(cert_path: &str, key_path: &str) -> QuicCert;

        /// Starts a QUIC server bound to `bind_addr` (e.g. "0.0.0.0:8303"),
        /// presenting the given certificate and requiring a client cert.
        /// Always returns a handle; check [`QuicTransport::error`].
        fn quic_server(bind_addr: &str, cert_der: &[u8], key_der: &[u8]) -> Box<QuicTransport>;

        /// Connects a QUIC client to `server_addr`. If `pinned_fingerprint` is
        /// 32 bytes it pins the server's public-key fingerprint; if empty the
        /// server cert is accepted unconditionally (insecure, LAN only). The
        /// client presents `client_cert_der`/`client_key_der` as its identity.
        /// Always returns a handle; check [`QuicTransport::error`].
        fn quic_client(
            server_addr: &str,
            server_name: &str,
            pinned_fingerprint: &[u8],
            client_cert_der: &[u8],
            client_key_der: &[u8],
        ) -> Box<QuicTransport>;

        /// Empty if the endpoint was created successfully, else the reason.
        fn error(self: &QuicTransport) -> String;

        /// Drains all currently available events (non-blocking).
        fn poll_events(self: &QuicTransport) -> Vec<QuicEvent>;

        /// Queues a reliable, ordered message on a connection.
        fn send_reliable(self: &QuicTransport, conn_id: u64, data: &[u8]);

        /// Queues an unreliable datagram on a connection.
        fn send_unreliable(self: &QuicTransport, conn_id: u64, data: &[u8]);

        /// Closes a connection.
        fn disconnect(self: &QuicTransport, conn_id: u64);

        /// The local UDP port the endpoint is bound to (0 if in error state).
        fn local_port(self: &QuicTransport) -> u16;
    }
}

fn ddnet_accounts_version() -> String {
    format!("ddnet-accounts-bridge {}", env!("CARGO_PKG_VERSION"))
}

/// A pollable QUIC endpoint exposed to C++. May be in an error state (then
/// `inner` is `None` and `error` explains why).
pub struct QuicTransport {
    inner: Option<Arc<transport::Transport>>,
    error: String,
}

impl QuicTransport {
    fn failed(error: String) -> Box<Self> {
        Box::new(Self { inner: None, error })
    }
    fn ok(inner: Arc<transport::Transport>) -> Box<Self> {
        Box::new(Self {
            inner: Some(inner),
            error: String::new(),
        })
    }
}

fn quic_generate_self_signed() -> QuicCert {
    match transport::generate_self_signed() {
        Ok(c) => QuicCert {
            error: String::new(),
            cert_der: c.cert_der,
            key_der: c.key_der,
            public_key_fingerprint: c.public_key_fingerprint.to_vec(),
        },
        Err(e) => QuicCert {
            error: e.to_string(),
            cert_der: Vec::new(),
            key_der: Vec::new(),
            public_key_fingerprint: Vec::new(),
        },
    }
}

fn quic_load_or_generate_identity(cert_path: &str, key_path: &str) -> QuicCert {
    match transport::load_or_generate_identity(cert_path, key_path) {
        Ok(c) => QuicCert {
            error: String::new(),
            cert_der: c.cert_der,
            key_der: c.key_der,
            public_key_fingerprint: c.public_key_fingerprint.to_vec(),
        },
        Err(e) => QuicCert {
            error: e.to_string(),
            cert_der: Vec::new(),
            key_der: Vec::new(),
            public_key_fingerprint: Vec::new(),
        },
    }
}

fn quic_server(bind_addr: &str, cert_der: &[u8], key_der: &[u8]) -> Box<QuicTransport> {
    let addr = match bind_addr.parse() {
        Ok(a) => a,
        Err(e) => return QuicTransport::failed(format!("invalid bind address: {e}")),
    };
    match transport::server(addr, cert_der.to_vec(), key_der.to_vec()) {
        Ok(inner) => QuicTransport::ok(inner),
        Err(e) => QuicTransport::failed(e.to_string()),
    }
}

fn quic_client(
    server_addr: &str,
    server_name: &str,
    pinned_fingerprint: &[u8],
    client_cert_der: &[u8],
    client_key_der: &[u8],
) -> Box<QuicTransport> {
    let addr = match server_addr.parse() {
        Ok(a) => a,
        Err(e) => return QuicTransport::failed(format!("invalid server address: {e}")),
    };
    let trust = if pinned_fingerprint.is_empty() {
        transport::ServerTrust::Insecure
    } else if pinned_fingerprint.len() == 32 {
        let mut fp = [0u8; 32];
        fp.copy_from_slice(pinned_fingerprint);
        transport::ServerTrust::PinnedPublicKey(fp)
    } else {
        return QuicTransport::failed("pinned fingerprint must be 32 bytes".to_string());
    };
    match transport::client(
        addr,
        server_name,
        trust,
        client_cert_der.to_vec(),
        client_key_der.to_vec(),
    ) {
        Ok(inner) => QuicTransport::ok(inner),
        Err(e) => QuicTransport::failed(e.to_string()),
    }
}

impl QuicTransport {
    fn error(&self) -> String {
        self.error.clone()
    }

    fn poll_events(&self) -> Vec<QuicEvent> {
        let Some(inner) = &self.inner else {
            return Vec::new();
        };
        inner
            .poll_events()
            .into_iter()
            .map(|ev| match ev {
                transport::Event::Connected {
                    conn_id,
                    peer_cert_der,
                } => QuicEvent {
                    kind: QuicEventKind::Connected,
                    conn_id,
                    data: peer_cert_der,
                },
                transport::Event::Disconnected { conn_id, reason } => QuicEvent {
                    kind: QuicEventKind::Disconnected,
                    conn_id,
                    data: reason.into_bytes(),
                },
                transport::Event::Reliable { conn_id, data } => QuicEvent {
                    kind: QuicEventKind::Reliable,
                    conn_id,
                    data,
                },
                transport::Event::Unreliable { conn_id, data } => QuicEvent {
                    kind: QuicEventKind::Unreliable,
                    conn_id,
                    data,
                },
            })
            .collect()
    }

    fn send_reliable(&self, conn_id: u64, data: &[u8]) {
        if let Some(inner) = &self.inner {
            inner.send_reliable(conn_id, data.to_vec());
        }
    }

    fn send_unreliable(&self, conn_id: u64, data: &[u8]) {
        if let Some(inner) = &self.inner {
            inner.send_unreliable(conn_id, data.to_vec());
        }
    }

    fn disconnect(&self, conn_id: u64) {
        if let Some(inner) = &self.inner {
            inner.disconnect(conn_id);
        }
    }

    fn local_port(&self) -> u16 {
        self.inner
            .as_ref()
            .and_then(|i| i.local_addr().ok())
            .map(|a| a.port())
            .unwrap_or(0)
    }
}

/// Game-server account database exposed to C++. May be in an error state.
pub struct AccountGameServer {
    inner: Option<game_server::GameServerDb>,
    error: String,
}

fn account_game_server_open(sqlite_path: &str) -> Box<AccountGameServer> {
    match game_server::open(sqlite_path) {
        Ok(db) => Box::new(AccountGameServer {
            inner: Some(db),
            error: String::new(),
        }),
        Err(e) => Box::new(AccountGameServer {
            inner: None,
            error: e.to_string(),
        }),
    }
}

impl AccountGameServer {
    fn error(&self) -> String {
        self.error.clone()
    }

    fn clear_ca_certs(&mut self) {
        if let Some(db) = &mut self.inner {
            db.clear_ca_certs();
        }
    }

    fn add_ca_cert(&mut self, cert_der: &[u8]) -> bool {
        match &mut self.inner {
            Some(db) => db.add_ca_cert(cert_der),
            None => false,
        }
    }

    fn load_account_ca_certs(&mut self, account_server_url: &str) -> i64 {
        match &mut self.inner {
            Some(db) => db
                .load_ca_certs_from(account_server_url)
                .map(|n| n as i64)
                .unwrap_or(-1),
            None => -1,
        }
    }

    fn login_by_cert(&self, peer_cert_der: &[u8]) -> AccountLogin {
        let Some(db) = &self.inner else {
            return AccountLogin {
                error: "account game server not initialized".to_string(),
                account_id: 0,
                public_key: Vec::new(),
                created: false,
            };
        };
        let outcome = db.login_by_cert(peer_cert_der);
        AccountLogin {
            error: outcome.error,
            account_id: outcome.account_id,
            public_key: outcome.public_key,
            created: outcome.created,
        }
    }

    fn rename(&self, account_id: i64, public_key: &[u8], name: &str) -> String {
        match &self.inner {
            Some(db) => db.rename(account_id, public_key, name),
            None => "account game server not initialized".to_string(),
        }
    }
}

/// Client-side account operations exposed to C++. May be in an error state.
pub struct AccountClient {
    inner: Option<client::AccountClient>,
    error: String,
}

fn account_client_open(account_server_url: &str, secure_dir: &str) -> Box<AccountClient> {
    match client::open(account_server_url, secure_dir) {
        Ok(c) => Box::new(AccountClient {
            inner: Some(c),
            error: String::new(),
        }),
        Err(e) => Box::new(AccountClient {
            inner: None,
            error: e.to_string(),
        }),
    }
}

impl AccountClient {
    fn error(&self) -> String {
        self.error.clone()
    }

    fn request_login_token_email(&self, email: &str) -> String {
        match &self.inner {
            Some(c) => c.request_login_token_email(email),
            None => self.error.clone(),
        }
    }

    fn login(&self, token_hex: String) -> AccountClientLogin {
        match &self.inner {
            Some(c) => {
                let (error, account_id) = c.login(token_hex);
                AccountClientLogin { error, account_id }
            }
            None => AccountClientLogin {
                error: "account client not initialized".to_string(),
                account_id: 0,
            },
        }
    }

    fn sign(&self) -> AccountSignedCert {
        match &self.inner {
            Some(c) => {
                let (error, cert_der, key_der) = c.sign();
                AccountSignedCert {
                    error,
                    cert_der,
                    key_der,
                }
            }
            None => AccountSignedCert {
                error: "account client not initialized".to_string(),
                cert_der: Vec::new(),
                key_der: Vec::new(),
            },
        }
    }

    fn logout(&self) -> String {
        match &self.inner {
            Some(c) => c.logout(),
            None => self.error.clone(),
        }
    }
}
