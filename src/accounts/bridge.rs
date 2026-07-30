//! The cxx bridge that exposes accounts and the QUIC transport to C++.
//!
//! Everything is poll based: operations start in the background and C++
//! fetches completions from event queues each tick. Events that are polled
//! when no event is pending have `m_Valid == false`.

#![allow(non_snake_case)]
#![allow(missing_docs)]

use std::path::Path;
use std::time::Duration;

use ddnet_accounts_shared::client::account_token::AccountTokenOperation;
use ddnet_accounts_shared::client::credential_auth_token::CredentialAuthTokenOperation;

use crate::client::{AccountErrorKind, AccountEventKind, AccountsClient};
use crate::game_server::AccountsGameServer;
use crate::identity::load_or_generate_identity;
use crate::quic::{Event, QuicClient, QuicServer, ServerVerification};

use self::ffi::{
    EAccountErrorKind, EAccountEventKind, EAccountOp, ECredentialAuthOp, EQuicEventKind,
    SAccountEvent, SAccountProfile, SGameServerLogin, SQuicEvent, SServerIdentity,
};

#[cxx::bridge(namespace = "accounts")]
mod ffi {
    /// What kind of operation an account event belongs to.
    enum EAccountEventKind {
        CREDENTIAL_AUTH_EMAIL_TOKEN,
        CREDENTIAL_AUTH_STEAM_TOKEN,
        ACCOUNT_EMAIL_TOKEN,
        ACCOUNT_STEAM_TOKEN,
        LOGIN,
        LOGOUT,
        LOGOUT_ALL,
        DELETE,
        LINK_CREDENTIAL,
        UNLINK_CREDENTIAL,
        ACCOUNT_INFO,
        CERT_AND_KEY,
    }

    /// Error class of a failed account operation.
    enum EAccountErrorKind {
        NONE,
        HTTP,
        FS,
        RATE_LIMITED,
        VPN_BAN,
        WEB_VALIDATION_NEEDED,
        OTHER,
    }

    /// Operations that need a credential auth token.
    enum ECredentialAuthOp {
        LOGIN,
        LINK_CREDENTIAL,
        UNLINK_CREDENTIAL,
    }

    /// Operations that need an account token.
    enum EAccountOp {
        LOGOUT_ALL,
        LINK_CREDENTIAL,
        DELETE,
    }

    /// A linked credential of an account.
    struct SAccountCredential {
        /// "email" or "steam".
        m_Kind: String,
        /// Partially masked email address or steam id.
        m_Identifier: String,
    }

    /// Completion of an asynchronous account operation.
    struct SAccountEvent {
        /// False if no event was pending.
        m_Valid: bool,
        /// Id that was returned when the operation was started. 0 means
        /// the event is unsolicited, currently only LOGOUT events for a
        /// profile that was removed as side effect of CERT_AND_KEY (the
        /// removed profile key is in the payload).
        m_RequestId: u64,
        /// Operation this event belongs to.
        m_Kind: EAccountEventKind,
        /// Whether the operation succeeded.
        m_Success: bool,
        /// Error class, NONE on success.
        m_ErrorKind: EAccountErrorKind,
        /// Human readable error description, empty on success.
        m_Error: String,
        /// Human readable warning for operations that succeeded in a
        /// degraded way, e.g. CERT_AND_KEY falling back to a self signed
        /// certificate.
        m_Warning: String,
        /// Operation specific payload: profile key for LOGIN, token for
        /// steam token operations, url for WEB_VALIDATION_NEEDED errors,
        /// removed profile key for unsolicited LOGOUT events.
        m_Payload: String,
        /// Certificate in der format, for CERT_AND_KEY.
        m_aCertDer: Vec<u8>,
        /// Private session key in pkcs8 der format, for CERT_AND_KEY.
        m_aKeyDer: Vec<u8>,
        /// Account id, for ACCOUNT_INFO.
        m_AccountId: i64,
        /// Account creation date as displayable string, for ACCOUNT_INFO.
        m_CreationDate: String,
        /// Linked credentials, for ACCOUNT_INFO.
        m_vCredentials: Vec<SAccountCredential>,
    }

    /// Info about a stored account profile.
    struct SAccountProfile {
        /// Key of the profile, `acc_<account id>`.
        m_Key: String,
        /// Display name, usually derived from the login credential.
        m_DisplayName: String,
        /// Whether this is the currently active profile.
        m_Current: bool,
    }

    /// What kind of transport event happened.
    enum EQuicEventKind {
        NONE,
        CONNECTED,
        CHUNK,
        DISCONNECTED,
    }

    /// A transport event of a QUIC endpoint.
    struct SQuicEvent {
        /// False if no event was pending.
        m_Valid: bool,
        /// Kind of the event.
        m_Kind: EQuicEventKind,
        /// Id of the peer the event belongs to. Always 0 on the client.
        m_PeerId: u64,
        /// CONNECTED: remote address of the peer as string.
        m_Addr: String,
        /// CONNECTED on the server: certificate the peer presented during
        /// the TLS handshake, in der format.
        m_aCertDer: Vec<u8>,
        /// CHUNK: the payload.
        m_aData: Vec<u8>,
        /// CHUNK: whether it was sent as unreliable datagram.
        m_Unreliable: bool,
        /// DISCONNECTED: reason.
        m_Reason: String,
        /// DISCONNECTED: whether the peer or the network caused the
        /// disconnect, rather than the local side.
        m_Remote: bool,
    }

    /// Result of resolving the account of a connecting client.
    struct SGameServerLogin {
        /// False if no login was pending.
        m_Valid: bool,
        /// Id that was returned by `BeginLogin`.
        m_RequestId: u64,
        /// The account id, 0 if the client has no (valid) account or the
        /// database registration failed (the client must be treated as
        /// anonymous then).
        m_AccountId: i64,
        /// Sha256 fingerprint of the public key of the client certificate.
        m_aPublicKeyHash: Vec<u8>,
        /// Whether this account was seen the first time on this server.
        m_NewAccount: bool,
        /// Error description if the login could not be resolved.
        m_Error: String,
    }

    /// Persistent TLS identity of a game server.
    struct SServerIdentity {
        /// Empty on success, error description otherwise.
        m_Error: String,
        /// Self signed certificate in der format.
        m_aCertDer: Vec<u8>,
        /// Private key in pkcs8 der format.
        m_aKeyDer: Vec<u8>,
        /// Sha256 fingerprint of the subject public key info of the
        /// certificate. This is what clients pin.
        m_aPublicKeyHash: Vec<u8>,
    }

    extern "Rust" {
        /// Client side account manager, see `AccountsClient` in the
        /// `ddnet-accounts-bridge` crate.
        type CAccountsClient;

        /// Creates the account manager with profile storage below
        /// `BasePath` and the given account server url.
        fn CreateAccountsClient(BasePath: &str, AccountServerUrl: &str) -> Box<CAccountsClient>;
        /// The error that occurred during creation, empty if none.
        fn Error(self: &CAccountsClient) -> String;
        /// Polls the next completed operation.
        fn PollEvent(self: &CAccountsClient) -> SAccountEvent;
        /// Requests a credential auth token, sent to the given email
        /// address. Empty `SecretKeyHex` means no secret key.
        fn CredentialAuthEmailToken(
            self: &CAccountsClient,
            Email: &str,
            Op: ECredentialAuthOp,
            SecretKeyHex: &str,
        ) -> u64;
        /// Requests a credential auth token for the given steam session
        /// ticket. On success the token is in the payload of the event.
        fn CredentialAuthSteamToken(
            self: &CAccountsClient,
            SteamTicket: &[u8],
            Op: ECredentialAuthOp,
            SecretKeyHex: &str,
        ) -> u64;
        /// Requests an account token, sent to the given email address.
        fn AccountEmailToken(
            self: &CAccountsClient,
            Email: &str,
            Op: EAccountOp,
            SecretKeyHex: &str,
        ) -> u64;
        /// Requests an account token for the given steam session ticket.
        /// On success the token is in the payload of the event.
        fn AccountSteamToken(
            self: &CAccountsClient,
            SteamTicket: &[u8],
            Op: EAccountOp,
            SecretKeyHex: &str,
        ) -> u64;
        /// Logs in with the credential auth token that was sent by email.
        fn LoginEmail(self: &CAccountsClient, Email: &str, CredentialAuthTokenHex: &str) -> u64;
        /// Logs in with a credential auth token obtained for a steam
        /// session ticket.
        fn LoginSteam(
            self: &CAccountsClient,
            SteamUserName: &str,
            CredentialAuthTokenHex: &str,
        ) -> u64;
        /// Logs out the given profile and removes it from disk.
        fn Logout(self: &CAccountsClient, ProfileKey: &str) -> u64;
        /// Logs out all other sessions of the account of the profile.
        fn LogoutAll(self: &CAccountsClient, ProfileKey: &str, AccountTokenHex: &str) -> u64;
        /// Deletes the account of the given profile.
        fn Delete(self: &CAccountsClient, ProfileKey: &str, AccountTokenHex: &str) -> u64;
        /// Links another credential to the account of the given profile.
        fn LinkCredential(
            self: &CAccountsClient,
            ProfileKey: &str,
            AccountTokenHex: &str,
            CredentialAuthTokenHex: &str,
        ) -> u64;
        /// Unlinks a credential from the account of the given profile.
        fn UnlinkCredential(
            self: &CAccountsClient,
            ProfileKey: &str,
            CredentialAuthTokenHex: &str,
        ) -> u64;
        /// Fetches the account info of the given profile.
        fn AccountInfo(self: &CAccountsClient, ProfileKey: &str) -> u64;
        /// Requests a certificate and session key for connecting to a game
        /// server, also used to refresh a certificate that is about to
        /// expire. Also works without an account (self signed cert, with a
        /// warning in the event).
        fn RequestCertAndKey(self: &CAccountsClient) -> u64;
        /// Currently stored profiles.
        fn Profiles(self: &CAccountsClient) -> Vec<SAccountProfile>;
        /// Switches the active profile.
        fn SetProfile(self: &CAccountsClient, ProfileKey: &str);
        /// Changes the display name of a profile.
        fn SetProfileDisplayName(self: &CAccountsClient, ProfileKey: &str, DisplayName: &str);

        /// Client side of the QUIC transport.
        type CQuicClient;

        /// Creates the client and starts connecting to `Addr` in the
        /// background. `BindAddr` is the local IP without port to bind the
        /// endpoint to, empty for the unspecified address of the target's
        /// address family; a bind address that cannot be parsed, bound or
        /// whose address family does not match the target fails the
        /// connect. The server certificate is verified against the sha256
        /// fingerprint `ServerPubKeyHash` (32 bytes). `CertDer` and
        /// `KeyDer` are the own certificate and pkcs8 session key.
        fn CreateQuicClient(
            Addr: &str,
            BindAddr: &str,
            ServerPubKeyHash: &[u8],
            CertDer: &[u8],
            KeyDer: &[u8],
            IdleTimeoutMs: u64,
        ) -> Box<CQuicClient>;
        /// Polls the next transport event.
        fn PollEvent(self: &mut CQuicClient) -> SQuicEvent;
        /// Sends a chunk to the server. Returns false if the chunk could
        /// not even be queued.
        fn Send(self: &CQuicClient, Data: &[u8], Unreliable: bool) -> bool;
        /// Closes the connection with the given reason.
        fn Close(self: &CQuicClient, Reason: &str);
        /// Milliseconds since the last time data arrived from the server,
        /// 0 while the connection is not established.
        fn MillisSinceReceive(self: &CQuicClient) -> u64;
        /// Current smoothed round trip time to the server in milliseconds.
        fn RttMillis(self: &CQuicClient) -> u64;

        /// Server side of the QUIC transport.
        type CQuicServer;

        /// Opens a QUIC endpoint on `BindAddr` with the given TLS identity,
        /// usually from `LoadOrGenerateServerIdentity`.
        fn CreateQuicServer(
            BindAddr: &str,
            CertDer: &[u8],
            KeyDer: &[u8],
            IdleTimeoutMs: u64,
            MaxPeers: usize,
        ) -> Box<CQuicServer>;
        /// The error that occurred while opening the endpoint, empty if
        /// none.
        fn Error(self: &CQuicServer) -> String;
        /// The port the endpoint is bound to, 0 on error.
        fn Port(self: &CQuicServer) -> u16;
        /// Polls the next transport event.
        fn PollEvent(self: &mut CQuicServer) -> SQuicEvent;
        /// Sends a chunk to the given peer. Returns false if the chunk
        /// could not even be queued.
        fn Send(self: &CQuicServer, PeerId: u64, Data: &[u8], Unreliable: bool) -> bool;
        /// Closes the connection to the given peer with the given reason.
        fn ClosePeer(self: &CQuicServer, PeerId: u64, Reason: &str);
        /// Current smoothed round trip time to the peer in milliseconds.
        fn RttMillis(self: &CQuicServer, PeerId: u64) -> u64;
        /// Milliseconds since the last stream frame or datagram arrived
        /// from the peer, -1 if the peer is unknown. QUIC keep alives do
        /// not count, so this is an application level liveness signal.
        fn MillisSinceReceive(self: &CQuicServer, PeerId: u64) -> i64;

        /// Game server side account manager.
        type CAccountsGameServer;

        /// Creates the account manager. `DbFilePath` is the sqlite database
        /// for the user table, `StoragePath` caches the account server
        /// certificates. Returns immediately, initialization runs in the
        /// background and is retried until it succeeds.
        fn CreateAccountsGameServer(
            DbFilePath: &str,
            StoragePath: &str,
            AccountServerUrl: &str,
        ) -> Box<CAccountsGameServer>;
        /// The hard configuration error (invalid account server url) that
        /// occurred during creation, empty if none. Network failures are
        /// not reported here, initialization keeps retrying.
        fn Error(self: &CAccountsGameServer) -> String;
        /// Starts resolving the account for the given client certificate.
        /// Logins that arrive before initialization finished are queued.
        fn BeginLogin(self: &CAccountsGameServer, CertDer: &[u8]) -> u64;
        /// Polls the next resolved login.
        fn PollLogin(self: &CAccountsGameServer) -> SGameServerLogin;

        /// Loads the server key from `KeyPath`, generating and persisting a
        /// new one if the file does not exist, and creates a self signed
        /// certificate for it.
        fn LoadOrGenerateServerIdentity(KeyPath: &str) -> SServerIdentity;
        /// Seconds until the given der certificate expires. Negative if
        /// already expired.
        fn CertExpiresInSeconds(CertDer: &[u8]) -> i64;
    }
}

pub struct CAccountsClient(AccountsClient);
pub struct CQuicClient(QuicClient);
pub struct CQuicServer(QuicServer);
pub struct CAccountsGameServer(AccountsGameServer);

fn CreateAccountsClient(BasePath: &str, AccountServerUrl: &str) -> Box<CAccountsClient> {
    Box::new(CAccountsClient(AccountsClient::new(
        BasePath,
        AccountServerUrl,
    )))
}

fn secret_key(SecretKeyHex: &str) -> Option<String> {
    if SecretKeyHex.is_empty() {
        None
    } else {
        Some(SecretKeyHex.to_owned())
    }
}

fn credential_auth_op(Op: ECredentialAuthOp) -> CredentialAuthTokenOperation {
    match Op {
        ECredentialAuthOp::LINK_CREDENTIAL => CredentialAuthTokenOperation::LinkCredential,
        ECredentialAuthOp::UNLINK_CREDENTIAL => CredentialAuthTokenOperation::UnlinkCredential,
        _ => CredentialAuthTokenOperation::Login,
    }
}

fn account_op(Op: EAccountOp) -> AccountTokenOperation {
    match Op {
        EAccountOp::LINK_CREDENTIAL => AccountTokenOperation::LinkCredential,
        EAccountOp::DELETE => AccountTokenOperation::Delete,
        _ => AccountTokenOperation::LogoutAll,
    }
}

impl SAccountEvent {
    fn invalid() -> Self {
        Self {
            m_Valid: false,
            m_RequestId: 0,
            m_Kind: EAccountEventKind::LOGIN,
            m_Success: false,
            m_ErrorKind: EAccountErrorKind::NONE,
            m_Error: String::new(),
            m_Warning: String::new(),
            m_Payload: String::new(),
            m_aCertDer: Vec::new(),
            m_aKeyDer: Vec::new(),
            m_AccountId: 0,
            m_CreationDate: String::new(),
            m_vCredentials: Vec::new(),
        }
    }
}

impl CAccountsClient {
    fn Error(&self) -> String {
        self.0.error().unwrap_or_default().to_owned()
    }

    fn PollEvent(&self) -> SAccountEvent {
        let Some(event) = self.0.poll_event() else {
            return SAccountEvent::invalid();
        };
        SAccountEvent {
            m_Valid: true,
            m_RequestId: event.request_id,
            m_Kind: match event.kind {
                AccountEventKind::CredentialAuthEmailToken => {
                    EAccountEventKind::CREDENTIAL_AUTH_EMAIL_TOKEN
                }
                AccountEventKind::CredentialAuthSteamToken => {
                    EAccountEventKind::CREDENTIAL_AUTH_STEAM_TOKEN
                }
                AccountEventKind::AccountEmailToken => EAccountEventKind::ACCOUNT_EMAIL_TOKEN,
                AccountEventKind::AccountSteamToken => EAccountEventKind::ACCOUNT_STEAM_TOKEN,
                AccountEventKind::Login => EAccountEventKind::LOGIN,
                AccountEventKind::Logout => EAccountEventKind::LOGOUT,
                AccountEventKind::LogoutAll => EAccountEventKind::LOGOUT_ALL,
                AccountEventKind::Delete => EAccountEventKind::DELETE,
                AccountEventKind::LinkCredential => EAccountEventKind::LINK_CREDENTIAL,
                AccountEventKind::UnlinkCredential => EAccountEventKind::UNLINK_CREDENTIAL,
                AccountEventKind::AccountInfo => EAccountEventKind::ACCOUNT_INFO,
                AccountEventKind::CertAndKey => EAccountEventKind::CERT_AND_KEY,
            },
            m_Success: event.success,
            m_ErrorKind: match event.error_kind {
                AccountErrorKind::None => EAccountErrorKind::NONE,
                AccountErrorKind::Http => EAccountErrorKind::HTTP,
                AccountErrorKind::Fs => EAccountErrorKind::FS,
                AccountErrorKind::RateLimited => EAccountErrorKind::RATE_LIMITED,
                AccountErrorKind::VpnBan => EAccountErrorKind::VPN_BAN,
                AccountErrorKind::WebValidationNeeded => EAccountErrorKind::WEB_VALIDATION_NEEDED,
                AccountErrorKind::Other => EAccountErrorKind::OTHER,
            },
            m_Error: event.error,
            m_Warning: event.warning,
            m_Payload: event.payload,
            m_aCertDer: event.cert_der,
            m_aKeyDer: event.key_der,
            m_AccountId: event.account_id,
            m_CreationDate: event.creation_date,
            m_vCredentials: event
                .credentials
                .into_iter()
                .map(|credential| ffi::SAccountCredential {
                    m_Kind: credential.kind,
                    m_Identifier: credential.identifier,
                })
                .collect(),
        }
    }

    fn CredentialAuthEmailToken(
        &self,
        Email: &str,
        Op: ECredentialAuthOp,
        SecretKeyHex: &str,
    ) -> u64 {
        self.0
            .credential_auth_email_token(Email, credential_auth_op(Op), secret_key(SecretKeyHex))
    }

    fn CredentialAuthSteamToken(
        &self,
        SteamTicket: &[u8],
        Op: ECredentialAuthOp,
        SecretKeyHex: &str,
    ) -> u64 {
        self.0.credential_auth_steam_token(
            SteamTicket.to_vec(),
            credential_auth_op(Op),
            secret_key(SecretKeyHex),
        )
    }

    fn AccountEmailToken(&self, Email: &str, Op: EAccountOp, SecretKeyHex: &str) -> u64 {
        self.0
            .account_email_token(Email, account_op(Op), secret_key(SecretKeyHex))
    }

    fn AccountSteamToken(&self, SteamTicket: &[u8], Op: EAccountOp, SecretKeyHex: &str) -> u64 {
        self.0.account_steam_token(
            SteamTicket.to_vec(),
            account_op(Op),
            secret_key(SecretKeyHex),
        )
    }

    fn LoginEmail(&self, Email: &str, CredentialAuthTokenHex: &str) -> u64 {
        self.0.login_email(Email, CredentialAuthTokenHex)
    }

    fn LoginSteam(&self, SteamUserName: &str, CredentialAuthTokenHex: &str) -> u64 {
        self.0.login_steam(SteamUserName, CredentialAuthTokenHex)
    }

    fn Logout(&self, ProfileKey: &str) -> u64 {
        self.0.logout(ProfileKey)
    }

    fn LogoutAll(&self, ProfileKey: &str, AccountTokenHex: &str) -> u64 {
        self.0.logout_all(ProfileKey, AccountTokenHex)
    }

    fn Delete(&self, ProfileKey: &str, AccountTokenHex: &str) -> u64 {
        self.0.delete(ProfileKey, AccountTokenHex)
    }

    fn LinkCredential(
        &self,
        ProfileKey: &str,
        AccountTokenHex: &str,
        CredentialAuthTokenHex: &str,
    ) -> u64 {
        self.0
            .link_credential(ProfileKey, AccountTokenHex, CredentialAuthTokenHex)
    }

    fn UnlinkCredential(&self, ProfileKey: &str, CredentialAuthTokenHex: &str) -> u64 {
        self.0.unlink_credential(ProfileKey, CredentialAuthTokenHex)
    }

    fn AccountInfo(&self, ProfileKey: &str) -> u64 {
        self.0.account_info(ProfileKey)
    }

    fn RequestCertAndKey(&self) -> u64 {
        self.0.cert_and_key()
    }

    fn Profiles(&self) -> Vec<SAccountProfile> {
        self.0
            .profiles()
            .into_iter()
            .map(|profile| SAccountProfile {
                m_Key: profile.key,
                m_DisplayName: profile.display_name,
                m_Current: profile.current,
            })
            .collect()
    }

    fn SetProfile(&self, ProfileKey: &str) {
        self.0.set_profile(ProfileKey);
    }

    fn SetProfileDisplayName(&self, ProfileKey: &str, DisplayName: &str) {
        self.0.set_profile_display_name(ProfileKey, DisplayName);
    }
}

impl SQuicEvent {
    fn invalid() -> Self {
        Self {
            m_Valid: false,
            m_Kind: EQuicEventKind::NONE,
            m_PeerId: 0,
            m_Addr: String::new(),
            m_aCertDer: Vec::new(),
            m_aData: Vec::new(),
            m_Unreliable: false,
            m_Reason: String::new(),
            m_Remote: false,
        }
    }

    fn from_event(event: Event) -> Self {
        let mut result = Self::invalid();
        result.m_Valid = true;
        match event {
            Event::Connected {
                peer,
                addr,
                cert_der,
            } => {
                result.m_Kind = EQuicEventKind::CONNECTED;
                result.m_PeerId = peer;
                result.m_Addr = addr.to_string();
                result.m_aCertDer = cert_der;
            }
            Event::Chunk {
                peer,
                data,
                unreliable,
            } => {
                result.m_Kind = EQuicEventKind::CHUNK;
                result.m_PeerId = peer;
                result.m_aData = data;
                result.m_Unreliable = unreliable;
            }
            Event::Disconnected {
                peer,
                reason,
                remote,
            } => {
                result.m_Kind = EQuicEventKind::DISCONNECTED;
                result.m_PeerId = peer;
                result.m_Reason = reason;
                result.m_Remote = remote;
            }
        }
        result
    }
}

fn CreateQuicClient(
    Addr: &str,
    BindAddr: &str,
    ServerPubKeyHash: &[u8],
    CertDer: &[u8],
    KeyDer: &[u8],
    IdleTimeoutMs: u64,
) -> Box<CQuicClient> {
    let hash: [u8; 32] = ServerPubKeyHash.try_into().unwrap_or_default();
    Box::new(CQuicClient(QuicClient::connect(
        Addr.to_owned(),
        BindAddr.to_owned(),
        ServerVerification::PubKeyHash(hash),
        CertDer.to_vec(),
        KeyDer.to_vec(),
        Duration::from_millis(IdleTimeoutMs),
    )))
}

impl CQuicClient {
    fn PollEvent(&mut self) -> SQuicEvent {
        self.0
            .poll_event()
            .map_or_else(SQuicEvent::invalid, SQuicEvent::from_event)
    }

    fn Send(&self, Data: &[u8], Unreliable: bool) -> bool {
        self.0.send(Data, Unreliable)
    }

    fn Close(&self, Reason: &str) {
        self.0.close(Reason);
    }

    fn MillisSinceReceive(&self) -> u64 {
        self.0.millis_since_receive()
    }

    fn RttMillis(&self) -> u64 {
        self.0.rtt_millis()
    }
}

fn CreateQuicServer(
    BindAddr: &str,
    CertDer: &[u8],
    KeyDer: &[u8],
    IdleTimeoutMs: u64,
    MaxPeers: usize,
) -> Box<CQuicServer> {
    Box::new(CQuicServer(QuicServer::new(
        BindAddr,
        CertDer.to_vec(),
        KeyDer.to_vec(),
        Duration::from_millis(IdleTimeoutMs),
        MaxPeers,
    )))
}

impl CQuicServer {
    fn Error(&self) -> String {
        self.0.error().unwrap_or_default().to_owned()
    }

    fn Port(&self) -> u16 {
        self.0.port()
    }

    fn PollEvent(&mut self) -> SQuicEvent {
        self.0
            .poll_event()
            .map_or_else(SQuicEvent::invalid, SQuicEvent::from_event)
    }

    fn Send(&self, PeerId: u64, Data: &[u8], Unreliable: bool) -> bool {
        self.0.send(PeerId, Data, Unreliable)
    }

    fn ClosePeer(&self, PeerId: u64, Reason: &str) {
        self.0.close_peer(PeerId, Reason);
    }

    fn RttMillis(&self, PeerId: u64) -> u64 {
        self.0.rtt_millis(PeerId)
    }

    fn MillisSinceReceive(&self, PeerId: u64) -> i64 {
        self.0.millis_since_receive(PeerId)
    }
}

fn CreateAccountsGameServer(
    DbFilePath: &str,
    StoragePath: &str,
    AccountServerUrl: &str,
) -> Box<CAccountsGameServer> {
    Box::new(CAccountsGameServer(AccountsGameServer::new(
        DbFilePath,
        StoragePath,
        AccountServerUrl,
    )))
}

impl CAccountsGameServer {
    fn Error(&self) -> String {
        self.0.error().unwrap_or_default().to_owned()
    }

    fn BeginLogin(&self, CertDer: &[u8]) -> u64 {
        self.0.begin_login(CertDer.to_vec())
    }

    fn PollLogin(&self) -> SGameServerLogin {
        match self.0.poll_login() {
            Some(login) => SGameServerLogin {
                m_Valid: true,
                m_RequestId: login.request_id,
                m_AccountId: login.account_id,
                m_aPublicKeyHash: login.public_key_hash,
                m_NewAccount: login.new_account,
                m_Error: login.error,
            },
            None => SGameServerLogin {
                m_Valid: false,
                m_RequestId: 0,
                m_AccountId: 0,
                m_aPublicKeyHash: Vec::new(),
                m_NewAccount: false,
                m_Error: String::new(),
            },
        }
    }
}

fn LoadOrGenerateServerIdentity(KeyPath: &str) -> SServerIdentity {
    match load_or_generate_identity(Path::new(KeyPath)) {
        Ok(identity) => SServerIdentity {
            m_Error: String::new(),
            m_aCertDer: identity.cert_der,
            m_aKeyDer: identity.key_der,
            m_aPublicKeyHash: identity.public_key_hash.to_vec(),
        },
        Err(err) => SServerIdentity {
            m_Error: err.to_string(),
            m_aCertDer: Vec::new(),
            m_aKeyDer: Vec::new(),
            m_aPublicKeyHash: Vec::new(),
        },
    }
}

fn CertExpiresInSeconds(CertDer: &[u8]) -> i64 {
    crate::client::cert_expires_in_seconds(CertDer)
}
