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
use std::time::SystemTime;

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

/// Result of resolving the account of a connecting client.
#[derive(Debug)]
pub struct LoginEvent {
    /// Id that was returned by [`AccountsGameServer::begin_login`].
    pub request_id: u64,
    /// The account id, 0 if the client has no (valid) account.
    pub account_id: i64,
    /// Sha256 fingerprint of the public key of the client certificate.
    /// Identifies clients without an account.
    pub public_key_hash: Vec<u8>,
    /// Whether this account was seen the first time on this game server.
    pub new_account: bool,
    /// Error description if the database registration failed.
    pub error: String,
}

/// The game server side account manager.
pub struct AccountsGameServer {
    pool: Option<AnyPool>,
    shared: Option<Arc<Shared>>,
    certs: Option<Arc<CertsDownloader>>,
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
        match runtime().block_on(Self::new_impl(
            db_file_path,
            storage_path,
            account_server_url,
        )) {
            Ok(this) => this,
            Err(err) => Self {
                pool: None,
                shared: None,
                certs: None,
                logins: Default::default(),
                next_request_id: AtomicU64::new(1),
                error: Some(err.to_string()),
            },
        }
    }

    async fn new_impl(
        db_file_path: &str,
        storage_path: &str,
        account_server_url: &str,
    ) -> anyhow::Result<Self> {
        let url = url::Url::parse(account_server_url)?;
        crate::client::check_account_server_url(&url)?;

        let options = sqlx::sqlite::SqliteConnectOptions::new()
            .filename(db_file_path)
            .create_if_missing(true);
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

        Ok(Self {
            pool: Some(pool),
            shared: Some(shared),
            certs: Some(certs),
            logins: Default::default(),
            next_request_id: AtomicU64::new(1),
            error: None,
        })
    }

    /// The error that occurred during creation, if any.
    pub fn error(&self) -> Option<&str> {
        self.error.as_deref()
    }

    /// Starts resolving the account for the given client certificate.
    /// The result arrives as [`LoginEvent`] via
    /// [`AccountsGameServer::poll_login`].
    pub fn begin_login(&self, cert_der: Vec<u8>) -> u64 {
        let request_id = self.next_request_id.fetch_add(1, Ordering::Relaxed);
        let logins = self.logins.clone();
        let (Some(pool), Some(shared), Some(certs)) =
            (self.pool.clone(), self.shared.clone(), self.certs.clone())
        else {
            logins.lock().push_back(LoginEvent {
                request_id,
                account_id: 0,
                public_key_hash: Vec::new(),
                new_account: false,
                error: self
                    .error
                    .clone()
                    .unwrap_or_else(|| "account support not initialized".to_owned()),
            });
            return request_id;
        };
        runtime().spawn(async move {
            let keys = certs.public_keys();
            let mut user_id = user_id_from_cert(&keys, cert_der.clone());
            // The account server only guarantees the signed account data
            // during the validity period of the certificate.
            if user_id.account_id.is_some() && !cert_validity_ok(&cert_der) {
                user_id.account_id = None;
            }
            let mut event = LoginEvent {
                request_id,
                account_id: user_id.account_id.unwrap_or(0),
                public_key_hash: user_id.public_key.to_vec(),
                new_account: false,
                error: String::new(),
            };
            match ddnet_account_game_server::auto_login::auto_login(shared, &pool, &user_id).await {
                Ok(new_account) => {
                    event.new_account = new_account;
                }
                Err(err) => {
                    // The identity of the client is still valid, only the
                    // registration in the database failed.
                    event.error = err.to_string();
                }
            }
            logins.lock().push_back(event);
        });
        request_id
    }

    /// Polls the next resolved login, if any.
    pub fn poll_login(&self) -> Option<LoginEvent> {
        self.logins.lock().pop_front()
    }
}

/// Checks that the current time is within the validity period of the
/// certificate.
fn cert_validity_ok(cert_der: &[u8]) -> bool {
    let Ok(cert) = x509_cert::Certificate::from_der(cert_der) else {
        return false;
    };
    let validity = &cert.tbs_certificate.validity;
    let now = SystemTime::now();
    now >= validity.not_before.to_system_time() && now <= validity.not_after.to_system_time()
}
