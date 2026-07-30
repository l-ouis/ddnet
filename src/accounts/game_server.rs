//! Game server side account handling.
//!
//! Downloads the account server certificates (cached on disk, refreshed in
//! the background), resolves the account of connecting clients from their
//! TLS certificate and registers them in the account database of the game
//! server.

use std::collections::VecDeque;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, SystemTime};

use ddnet_account_client_http_fs::cert_downloader::CertsDownloader;
use ddnet_account_client_http_fs::client::ClientHttpTokioFs;
use ddnet_account_client_http_fs::fs::Fs;
use ddnet_account_client_http_fs::http::Http;
use ddnet_account_client_reqwest::client::HttpReqwest;
use ddnet_account_game_server::shared::Shared;
use ddnet_account_sql::any::AnyPool;
use ddnet_accounts_shared::game_server::user_id::user_id_from_cert;
use parking_lot::Mutex;
use x509_cert::der::Decode;

use crate::runtime::runtime;

/// Timeout for one initialization attempt. The upstream reqwest client has
/// no timeout configured, a blackholed account server would stall forever.
const INIT_ATTEMPT_TIMEOUT: Duration = Duration::from_secs(20);
/// Retry delays after failed initialization attempts, the last one repeats.
const INIT_RETRY_DELAYS: [Duration; 4] = [
    Duration::from_secs(5),
    Duration::from_secs(15),
    Duration::from_secs(60),
    Duration::from_secs(5 * 60),
];
/// Maximum number of logins queued while initialization is still running.
const MAX_QUEUED_LOGINS: usize = 128;

/// Result of resolving the account of a connecting client.
#[derive(Debug)]
pub struct LoginEvent {
    /// Id that was returned by [`AccountsGameServer::begin_login`].
    pub request_id: u64,
    /// The account id, 0 if the client has no (valid) account or the
    /// database registration failed.
    pub account_id: i64,
    /// Sha256 fingerprint of the public key of the client certificate.
    /// Identifies clients without an account.
    pub public_key_hash: Vec<u8>,
    /// Whether this account was seen the first time on this game server.
    pub new_account: bool,
    /// Error description if the login could not be resolved.
    pub error: String,
}

#[derive(Clone)]
struct Backend {
    pool: AnyPool,
    shared: Arc<Shared>,
    certs: Arc<CertsDownloader>,
}

struct QueuedLogin {
    request_id: u64,
    cert_der: Vec<u8>,
}

enum Init {
    Pending(Vec<QueuedLogin>),
    Ready(Backend),
}

/// The game server side account manager.
///
/// Construction returns immediately; downloading the account server
/// certificates and opening the database run on the runtime in the
/// background and are retried until they succeed. Only hard configuration
/// errors (invalid account server url) are reported via
/// [`AccountsGameServer::error`].
pub struct AccountsGameServer {
    init: Arc<Mutex<Init>>,
    logins: Arc<Mutex<VecDeque<LoginEvent>>>,
    next_request_id: AtomicU64,
    error: Option<String>,
}

impl AccountsGameServer {
    /// Creates the account manager.
    ///
    /// `db_file_path` is the sqlite database file for the user table,
    /// `storage_path` caches the downloaded account server certificates,
    /// `account_server_url` is the url of the account server whose
    /// signatures are accepted.
    pub fn new(db_file_path: &str, storage_path: &str, account_server_url: &str) -> Self {
        let init: Arc<Mutex<Init>> = Arc::new(Mutex::new(Init::Pending(Vec::new())));
        let logins: Arc<Mutex<VecDeque<LoginEvent>>> = Default::default();
        let url = match url::Url::parse(account_server_url)
            .map_err(anyhow::Error::from)
            .and_then(|url| crate::client::check_account_server_url(&url).map(|()| url))
        {
            Ok(url) => url,
            Err(err) => {
                return Self {
                    init,
                    logins,
                    next_request_id: AtomicU64::new(1),
                    error: Some(format!("invalid account server url: {}", err)),
                }
            }
        };
        let task_init = init.clone();
        let task_logins = logins.clone();
        let db_file_path = db_file_path.to_owned();
        let storage_path = storage_path.to_owned();
        runtime().spawn(async move {
            let mut delays = INIT_RETRY_DELAYS.iter();
            let backend = loop {
                match tokio::time::timeout(
                    INIT_ATTEMPT_TIMEOUT,
                    Self::init_impl(&db_file_path, &storage_path, url.clone()),
                )
                .await
                {
                    Ok(Ok(backend)) => break backend,
                    Ok(Err(_)) | Err(_) => {
                        let delay = delays.next().unwrap_or(INIT_RETRY_DELAYS.last().unwrap());
                        tokio::time::sleep(*delay).await;
                    }
                }
            };
            let queued = {
                let mut init = task_init.lock();
                let queued = match &mut *init {
                    Init::Pending(queued) => std::mem::take(queued),
                    Init::Ready(_) => Vec::new(),
                };
                *init = Init::Ready(backend.clone());
                queued
            };
            for login in queued {
                Self::resolve(
                    backend.clone(),
                    task_logins.clone(),
                    login.request_id,
                    login.cert_der,
                );
            }
        });
        Self {
            init,
            logins,
            next_request_id: AtomicU64::new(1),
            error: None,
        }
    }

    async fn init_impl(
        db_file_path: &str,
        storage_path: &str,
        url: url::Url,
    ) -> anyhow::Result<Backend> {
        let options = sqlx::sqlite::SqliteConnectOptions::new()
            .filename(db_file_path)
            .create_if_missing(true)
            // Concurrent login bursts would hit SQLITE_BUSY in the default
            // journal mode.
            .journal_mode(sqlx::sqlite::SqliteJournalMode::Wal);
        let pool = AnyPool::Sqlite(
            sqlx::sqlite::SqlitePoolOptions::new()
                .max_connections(4)
                .connect_with(options)
                .await?,
        );
        ddnet_account_game_server::setup::setup(&pool).await?;
        let shared = ddnet_account_game_server::prepare::prepare(&pool).await?;

        let http_client = Arc::new(ClientHttpTokioFs {
            http: vec![Arc::new(HttpReqwest::new(url)) as Arc<dyn Http>],
            cur_http: Default::default(),
            fs: Fs::new(PathBuf::from(storage_path)).await?,
        });
        let certs = CertsDownloader::new(http_client).await?;
        let certs_task = certs.clone();
        runtime().spawn(async move { certs_task.download_task().await });

        Ok(Backend {
            pool,
            shared,
            certs,
        })
    }

    /// The hard configuration error that occurred during creation, if any.
    /// Network failures are not reported here, initialization keeps
    /// retrying in the background.
    pub fn error(&self) -> Option<&str> {
        self.error.as_deref()
    }

    /// Starts resolving the account for the given client certificate.
    /// The result arrives as [`LoginEvent`] via
    /// [`AccountsGameServer::poll_login`]. Logins that arrive before the
    /// background initialization finished are queued.
    pub fn begin_login(&self, cert_der: Vec<u8>) -> u64 {
        let request_id = self.next_request_id.fetch_add(1, Ordering::Relaxed);
        if let Some(error) = &self.error {
            self.logins
                .lock()
                .push_back(Self::error_event(request_id, error.clone()));
            return request_id;
        }
        let backend = {
            let mut init = self.init.lock();
            match &mut *init {
                Init::Pending(queued) => {
                    if queued.len() < MAX_QUEUED_LOGINS {
                        queued.push(QueuedLogin {
                            request_id,
                            cert_der,
                        });
                        return request_id;
                    }
                    None
                }
                Init::Ready(backend) => Some(backend.clone()),
            }
        };
        match backend {
            Some(backend) => Self::resolve(backend, self.logins.clone(), request_id, cert_der),
            None => self.logins.lock().push_back(Self::error_event(
                request_id,
                "account support still initializing".to_owned(),
            )),
        }
        request_id
    }

    fn error_event(request_id: u64, error: String) -> LoginEvent {
        LoginEvent {
            request_id,
            account_id: 0,
            public_key_hash: Vec::new(),
            new_account: false,
            error,
        }
    }

    fn resolve(
        backend: Backend,
        logins: Arc<Mutex<VecDeque<LoginEvent>>>,
        request_id: u64,
        cert_der: Vec<u8>,
    ) {
        runtime().spawn(async move {
            let event = Self::resolve_impl(backend, request_id, cert_der).await;
            logins.lock().push_back(event);
        });
    }

    async fn resolve_impl(backend: Backend, request_id: u64, cert_der: Vec<u8>) -> LoginEvent {
        // Pre-check the der, `user_id_from_cert` panics on invalid input.
        let cert = match x509_cert::Certificate::from_der(&cert_der) {
            Ok(cert) => cert,
            Err(err) => {
                return Self::error_event(request_id, format!("invalid certificate: {}", err))
            }
        };
        let keys = backend.certs.public_keys();
        let mut user_id = user_id_from_cert(&keys, cert_der);
        // The account server only guarantees the signed account data
        // during the validity period of the certificate.
        if user_id.account_id.is_some() && !cert_validity_ok(&cert) {
            user_id.account_id = None;
        }
        let mut event = LoginEvent {
            request_id,
            account_id: user_id.account_id.unwrap_or(0),
            public_key_hash: user_id.public_key.to_vec(),
            new_account: false,
            error: String::new(),
        };
        match ddnet_account_game_server::auto_login::auto_login(
            backend.shared,
            &backend.pool,
            &user_id,
        )
        .await
        {
            Ok(new_account) => {
                event.new_account = new_account;
            }
            Err(err) => {
                // Without a users row the server must treat the client as
                // anonymous, an account id would reference a missing row.
                event.account_id = 0;
                event.error = err.to_string();
            }
        }
        event
    }

    /// Polls the next resolved login, if any.
    pub fn poll_login(&self) -> Option<LoginEvent> {
        self.logins.lock().pop_front()
    }
}

/// Checks that the current time is within the validity period of the
/// certificate.
fn cert_validity_ok(cert: &x509_cert::Certificate) -> bool {
    let validity = &cert.tbs_certificate.validity;
    let now = SystemTime::now();
    now >= validity.not_before.to_system_time() && now <= validity.not_after.to_system_time()
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::pkcs8::spki::der::pem::LineEnding;
    use ed25519_dalek::pkcs8::EncodePrivateKey;

    fn self_signed(not_before: SystemTime, not_after: SystemTime) -> x509_cert::Certificate {
        let key = ed25519_dalek::SigningKey::generate(&mut rand::rngs::OsRng);
        let pem = key.to_pkcs8_pem(LineEnding::LF).unwrap();
        let key_pair =
            rcgen::KeyPair::from_pkcs8_pem_and_sign_algo(&pem, &rcgen::PKCS_ED25519).unwrap();
        let mut params = rcgen::CertificateParams::new(vec!["ddnet".into()]).unwrap();
        params.not_before = not_before.into();
        params.not_after = not_after.into();
        let cert = params.self_signed(&key_pair).unwrap();
        x509_cert::Certificate::from_der(cert.der()).unwrap()
    }

    #[test]
    fn cert_expiry_rejected() {
        let now = SystemTime::now();
        let hour = Duration::from_secs(60 * 60);
        assert!(cert_validity_ok(&self_signed(now - hour, now + hour)));
        assert!(!cert_validity_ok(&self_signed(now - 2 * hour, now - hour)));
        assert!(!cert_validity_ok(&self_signed(now + hour, now + 2 * hour)));
    }
}
