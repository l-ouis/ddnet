//! Game-server side account operations: turn a client's QUIC certificate into
//! an account id and auto-login it into the game server's own database.
//!
//! Wraps the `ddnet-account-game-server` library, driving its async API on the
//! shared tokio runtime and owning a SQLite pool for the account `user` table.

use std::sync::Arc;

use anyhow::Context;
use ddnet_account_sql::any::AnyPool;
use ddnet_account_game_server::shared::Shared;
use ddnet_accounts_shared::game_server::user_id::{user_id_from_cert, UserId, VerifyingKey};
use sqlx::sqlite::{SqliteConnectOptions, SqlitePoolOptions};
use x509_cert::der::{Decode, Encode};
use x509_cert::spki::DecodePublicKey;

use crate::transport::runtime;

/// Result of resolving + auto-logging a client certificate.
pub struct LoginOutcome {
    /// Non-empty if resolution/login failed.
    pub error: String,
    /// The account id, or 0 if the client has no account (public-key only).
    pub account_id: i64,
    /// The client's public-key fingerprint (32 bytes).
    pub public_key: Vec<u8>,
    /// True if a new account row was created by this login.
    pub created: bool,
}

/// Owns the account `user` database and the account server's CA verifying keys.
pub struct GameServerDb {
    pool: AnyPool,
    shared: Arc<Shared>,
    ca_keys: Vec<VerifyingKey>,
}

/// Opens (creating if necessary) the SQLite account database and prepares the
/// game-server statements.
pub fn open(sqlite_path: &str) -> anyhow::Result<GameServerDb> {
    runtime().block_on(async move {
        sqlx::any::install_default_drivers();
        let pool = AnyPool::Sqlite(
            SqlitePoolOptions::new()
                .max_connections(8)
                .connect_with(
                    SqliteConnectOptions::new()
                        .filename(sqlite_path)
                        .create_if_missing(true),
                )
                .await
                .context("opening sqlite account db")?,
        );
        ddnet_account_game_server::setup::setup(&pool)
            .await
            .context("account db setup")?;
        let shared = ddnet_account_game_server::prepare::prepare(&pool)
            .await
            .context("preparing account statements")?;
        anyhow::Ok(GameServerDb {
            pool,
            shared,
            ca_keys: Vec::new(),
        })
    })
}

/// Extracts the P-256 verifying key from an account-server CA certificate.
fn ca_cert_to_key(cert_der: &[u8]) -> Option<VerifyingKey> {
    let cert = x509_cert::Certificate::from_der(cert_der).ok()?;
    let der = cert.tbs_certificate.subject_public_key_info.to_der().ok()?;
    VerifyingKey::from_public_key_der(&der).ok()
}

impl GameServerDb {
    /// Drops all account-server CA keys (call before re-adding a fresh set).
    pub fn clear_ca_certs(&mut self) {
        self.ca_keys.clear();
    }

    /// Adds an account-server CA certificate (DER) used to verify client
    /// account certificates. Returns false if the certificate is unusable.
    pub fn add_ca_cert(&mut self, cert_der: &[u8]) -> bool {
        match ca_cert_to_key(cert_der) {
            Some(key) => {
                self.ca_keys.push(key);
                true
            }
            None => false,
        }
    }

    /// Resolves the client certificate to a [`UserId`], guarding the panic in
    /// [`user_id_from_cert`] by validating the DER first.
    fn resolve(&self, peer_cert_der: &[u8]) -> anyhow::Result<UserId> {
        x509_cert::Certificate::from_der(peer_cert_der).context("invalid client certificate")?;
        Ok(user_id_from_cert(&self.ca_keys, peer_cert_der.to_vec()))
    }

    /// Resolves the client certificate and auto-logs the user in (creating the
    /// account row on first login).
    pub fn login_by_cert(&self, peer_cert_der: &[u8]) -> LoginOutcome {
        let user_id = match self.resolve(peer_cert_der) {
            Ok(user_id) => user_id,
            Err(err) => {
                return LoginOutcome {
                    error: err.to_string(),
                    account_id: 0,
                    public_key: Vec::new(),
                    created: false,
                }
            }
        };
        let account_id = user_id.account_id.unwrap_or(0);
        let public_key = user_id.public_key.to_vec();
        match runtime().block_on(ddnet_account_game_server::auto_login::auto_login(
            self.shared.clone(),
            &self.pool,
            &user_id,
        )) {
            Ok(created) => LoginOutcome {
                error: String::new(),
                account_id,
                public_key,
                created,
            },
            Err(err) => LoginOutcome {
                error: err.to_string(),
                account_id,
                public_key,
                created: false,
            },
        }
    }

    /// Downloads the account server's CA certificates from `account_server_url`
    /// and trusts them (so account-signed client certs can be verified).
    /// Returns the number of CA keys loaded.
    pub fn load_ca_certs_from(&mut self, account_server_url: &str) -> anyhow::Result<usize> {
        let certs = crate::client::download_ca_certs_from(account_server_url)?;
        let mut loaded = 0;
        for cert in &certs {
            if self.add_ca_cert(cert) {
                loaded += 1;
            }
        }
        Ok(loaded)
    }

    /// Renames the account user. `account_id` 0 means a public-key-only user.
    /// Returns an empty string on success, else the error.
    pub fn rename(&self, account_id: i64, public_key: &[u8], name: &str) -> String {
        let mut key = [0u8; 32];
        let n = public_key.len().min(32);
        key[..n].copy_from_slice(&public_key[..n]);
        let user_id = UserId {
            account_id: (account_id > 0).then_some(account_id),
            public_key: key,
        };
        match runtime().block_on(ddnet_account_game_server::rename::rename(
            self.shared.clone(),
            &self.pool,
            &user_id,
            name,
        )) {
            Ok(_) => String::new(),
            Err(err) => err.to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::str::FromStr;
    use std::time::Duration;

    use ddnet_accounts_shared::account_server::cert_account_ext::{AccountCertData, AccountCertExt};
    use p256::ecdsa::{DerSignature, SigningKey as P256SigningKey};
    use rand::rngs::OsRng;
    use x509_cert::builder::{Builder, CertificateBuilder, Profile};
    use x509_cert::name::Name;
    use x509_cert::serial_number::SerialNumber;
    use x509_cert::spki::SubjectPublicKeyInfoOwned;
    use x509_cert::time::Validity;

    // Builds a P-256 CA (mimicking the account server) and a client ed25519
    // certificate signed by it carrying `account_id` in the account extension.
    fn make_ca_and_signed_client(account_id: i64) -> (Vec<u8>, Vec<u8>) {
        let ca_key = P256SigningKey::random(&mut OsRng);
        let ca_spki = SubjectPublicKeyInfoOwned::from_key(*ca_key.verifying_key()).unwrap();
        let ca_cert = CertificateBuilder::new(
            Profile::Root,
            SerialNumber::from(1u32),
            Validity::from_now(Duration::from_secs(3600)).unwrap(),
            Name::from_str("CN=DDNet,O=DDNet.org,C=EU").unwrap(),
            ca_spki,
            &ca_key,
        )
        .unwrap()
        .build::<DerSignature>()
        .unwrap();
        let ca_der = ca_cert.to_der().unwrap();

        let client_key = ed25519_dalek::SigningKey::generate(&mut OsRng);
        let client_spki = SubjectPublicKeyInfoOwned::from_key(client_key.verifying_key()).unwrap();
        let mut builder = CertificateBuilder::new(
            Profile::Root,
            SerialNumber::from(2u32),
            Validity::from_now(Duration::from_secs(3600)).unwrap(),
            Name::from_str("O=DDNet").unwrap(),
            client_spki,
            &ca_key, // signed by the CA's P-256 key
        )
        .unwrap();
        builder
            .add_extension(&AccountCertExt {
                data: AccountCertData {
                    account_id,
                    utc_time_since_unix_epoch_millis: 0,
                },
            })
            .unwrap();
        let client_der = builder.build::<DerSignature>().unwrap().to_der().unwrap();
        (ca_der, client_der)
    }

    fn self_signed_accountless() -> Vec<u8> {
        crate::transport::generate_self_signed().unwrap().cert_der
    }

    #[test]
    fn account_cert_resolves_and_auto_logs_in() {
        let db_file = "test-game-server-acc.sqlite";
        let _ = std::fs::remove_file(db_file);
        let mut db = open(db_file).unwrap();

        let (ca_der, client_der) = make_ca_and_signed_client(42);
        assert!(db.add_ca_cert(&ca_der));

        // First login creates the account row.
        let r1 = db.login_by_cert(&client_der);
        assert!(r1.error.is_empty(), "login error: {}", r1.error);
        assert_eq!(r1.account_id, 42);
        assert!(r1.created);
        assert_eq!(r1.public_key.len(), 32);

        // Second login finds the existing account (not created again).
        let r2 = db.login_by_cert(&client_der);
        assert!(r2.error.is_empty());
        assert_eq!(r2.account_id, 42);
        assert!(!r2.created);

        let _ = std::fs::remove_file(db_file);
    }

    #[test]
    fn accountless_cert_has_no_account_id() {
        let db_file = "test-game-server-noacc.sqlite";
        let _ = std::fs::remove_file(db_file);
        let db = open(db_file).unwrap();

        // No CA keys added, and the cert is self-signed: no account id, but a
        // valid public-key fingerprint is returned. auto_login creates nothing.
        let cert = self_signed_accountless();
        let r = db.login_by_cert(&cert);
        assert!(r.error.is_empty(), "error: {}", r.error);
        assert_eq!(r.account_id, 0);
        assert_eq!(r.public_key.len(), 32);
        assert!(r.public_key.iter().any(|&b| b != 0));
        assert!(!r.created);

        let _ = std::fs::remove_file(db_file);
    }

    #[test]
    fn wrong_ca_does_not_yield_account_id() {
        let db_file = "test-game-server-wrongca.sqlite";
        let _ = std::fs::remove_file(db_file);
        let mut db = open(db_file).unwrap();

        // A client cert signed by CA "A", but we trust an unrelated CA "B".
        let (_ca_a, client_der) = make_ca_and_signed_client(7);
        let (ca_b, _client_b) = make_ca_and_signed_client(7);
        assert!(db.add_ca_cert(&ca_b));

        let r = db.login_by_cert(&client_der);
        assert!(r.error.is_empty());
        assert_eq!(r.account_id, 0, "must not trust a cert signed by an unknown CA");

        let _ = std::fs::remove_file(db_file);
    }
}
