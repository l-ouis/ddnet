//! Client side account handling.
//!
//! Wraps the profile manager of `ddnet-account-client-http-fs`, which owns
//! the session key pairs and certificates on disk. All operations run
//! asynchronously on the shared runtime, results are polled by the C++ side
//! as [`AccountEvent`]s.

use std::collections::VecDeque;
use std::path::PathBuf;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::SystemTime;

use anyhow::anyhow;
use ddnet_account_client::account_info::AccountInfoResult;
use ddnet_account_client::account_token::AccountTokenResult;
use ddnet_account_client::credential_auth_token::CredentialAuthTokenResult;
use ddnet_account_client::delete::DeleteResult;
use ddnet_account_client::errors::{FsLikeError, HttpLikeError};
use ddnet_account_client::link_credential::LinkCredentialResult;
use ddnet_account_client::login::LoginResult;
use ddnet_account_client::logout::LogoutResult;
use ddnet_account_client::logout_all::LogoutAllResult;
use ddnet_account_client::unlink_credential::UnlinkCredentialResult;
use ddnet_account_client_http_fs::client::ClientHttpTokioFs;
use ddnet_account_client_http_fs::fs::Fs;
use ddnet_account_client_http_fs::http::Http;
use ddnet_account_client_http_fs::profiles::{Profiles, ProfilesLoading};
use ddnet_account_client_reqwest::client::HttpReqwest;
use ddnet_accounts_shared::account_server::account_token::AccountTokenError;
use ddnet_accounts_shared::account_server::credential_auth_token::CredentialAuthTokenError;
use ddnet_accounts_shared::account_server::errors::AccountServerRequestError;
use ddnet_accounts_shared::client::account_token::AccountTokenOperation;
use ddnet_accounts_shared::client::credential_auth_token::CredentialAuthTokenOperation;
use ed25519_dalek::pkcs8::EncodePrivateKey;
use parking_lot::Mutex;
use x509_cert::der::Encode;

use crate::runtime::runtime;

/// What kind of operation an [`AccountEvent`] belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AccountEventKind {
    /// Result of requesting a credential auth token by email.
    CredentialAuthEmailToken,
    /// Result of requesting a credential auth token by steam ticket.
    /// On success the token is in the payload.
    CredentialAuthSteamToken,
    /// Result of requesting an account token by email.
    AccountEmailToken,
    /// Result of requesting an account token by steam ticket.
    /// On success the token is in the payload.
    AccountSteamToken,
    /// Result of a login. On success the profile key is in the payload.
    Login,
    /// Result of a logout.
    Logout,
    /// Result of logging out all other sessions.
    LogoutAll,
    /// Result of deleting the account.
    Delete,
    /// Result of linking a credential.
    LinkCredential,
    /// Result of unlinking a credential.
    UnlinkCredential,
    /// Result of an account info request.
    AccountInfo,
    /// Result of a certificate request for connecting to a game server.
    CertAndKey,
}

/// Structured error class of a failed operation, so the UI can react
/// (e.g. open a browser for web validation).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AccountErrorKind {
    /// No error.
    None,
    /// Connection to the account server failed.
    Http,
    /// Reading/writing the session key pair failed.
    Fs,
    /// The account server rate limited the request.
    RateLimited,
    /// The account server denied the request because of a VPN ban.
    VpnBan,
    /// The user must visit a web page to continue, the url is in the
    /// payload of the event.
    WebValidationNeeded,
    /// Any other error.
    Other,
}

/// A linked credential of an account.
#[derive(Debug, Clone)]
pub struct AccountCredential {
    /// "email" or "steam".
    pub kind: String,
    /// Partially masked email address or steam id.
    pub identifier: String,
}

/// Completion of an asynchronous account operation.
#[derive(Debug)]
pub struct AccountEvent {
    /// Id that was returned when the operation was started.
    pub request_id: u64,
    /// Operation this event belongs to.
    pub kind: AccountEventKind,
    /// Whether the operation succeeded.
    pub success: bool,
    /// Error class, [`AccountErrorKind::None`] on success.
    pub error_kind: AccountErrorKind,
    /// Human readable error description.
    pub error: String,
    /// Human readable warning for operations that succeeded in a degraded
    /// way, e.g. [`AccountEventKind::CertAndKey`] falling back to a self
    /// signed certificate.
    pub warning: String,
    /// Operation specific payload, see [`AccountEventKind`].
    pub payload: String,
    /// Certificate in der format, for [`AccountEventKind::CertAndKey`].
    pub cert_der: Vec<u8>,
    /// Private session key in pkcs8 der format, for
    /// [`AccountEventKind::CertAndKey`].
    pub key_der: Vec<u8>,
    /// Account id, for [`AccountEventKind::AccountInfo`].
    pub account_id: i64,
    /// Account creation date as displayable string, for
    /// [`AccountEventKind::AccountInfo`].
    pub creation_date: String,
    /// Linked credentials, for [`AccountEventKind::AccountInfo`].
    pub credentials: Vec<AccountCredential>,
}

impl AccountEvent {
    fn new(request_id: u64, kind: AccountEventKind) -> Self {
        Self {
            request_id,
            kind,
            success: false,
            error_kind: AccountErrorKind::None,
            error: String::new(),
            warning: String::new(),
            payload: String::new(),
            cert_der: Vec::new(),
            key_der: Vec::new(),
            account_id: 0,
            creation_date: String::new(),
            credentials: Vec::new(),
        }
    }

    fn success(mut self) -> Self {
        self.success = true;
        self
    }

    fn error(mut self, kind: AccountErrorKind, error: String) -> Self {
        self.error_kind = kind;
        self.error = error;
        self
    }
}

/// Info about a stored profile.
#[derive(Debug)]
pub struct ProfileInfo {
    /// Key of the profile, `acc_<account id>`.
    pub key: String,
    /// Display name, usually derived from the login credential.
    pub display_name: String,
    /// Whether this is the currently active profile.
    pub current: bool,
}

type ProfileFactory = Box<
    dyn Fn(
            PathBuf,
        ) -> Pin<
            Box<dyn std::future::Future<Output = anyhow::Result<ClientHttpTokioFs>> + Sync + Send>,
        > + Sync
        + Send,
>;

struct Factory(ProfileFactory);

impl std::fmt::Debug for Factory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("Factory").finish()
    }
}

impl std::ops::Deref for Factory {
    type Target = dyn Fn(
        PathBuf,
    ) -> Pin<
        Box<dyn std::future::Future<Output = anyhow::Result<ClientHttpTokioFs>> + Sync + Send>,
    >;

    fn deref(&self) -> &Self::Target {
        self.0.as_ref()
    }
}

/// The client side account manager.
pub struct AccountsClient {
    profiles: Option<Arc<Profiles<ClientHttpTokioFs, Factory>>>,
    events: Arc<Mutex<VecDeque<AccountEvent>>>,
    next_request_id: AtomicU64,
    error: Option<String>,
}

fn http_error_kind(err: &HttpLikeError) -> AccountErrorKind {
    match err {
        HttpLikeError::Request | HttpLikeError::Status(_) => AccountErrorKind::Http,
        HttpLikeError::Other(_) => AccountErrorKind::Other,
    }
}

fn request_error_kind<E>(err: &AccountServerRequestError<E>) -> AccountErrorKind {
    match err {
        AccountServerRequestError::RateLimited(_) => AccountErrorKind::RateLimited,
        AccountServerRequestError::VpnBan(_) => AccountErrorKind::VpnBan,
        _ => AccountErrorKind::Other,
    }
}

fn credential_auth_token_error(
    event: AccountEvent,
    err: &CredentialAuthTokenResult,
) -> AccountEvent {
    match err {
        CredentialAuthTokenResult::HttpLikeError(err) => {
            event.error(http_error_kind(err), err.to_string())
        }
        CredentialAuthTokenResult::FsLikeError(err) => {
            event.error(AccountErrorKind::Fs, err.to_string())
        }
        CredentialAuthTokenResult::AccountServerRequstError(
            AccountServerRequestError::LogicError(
                CredentialAuthTokenError::WebValidationProcessNeeded { url },
            ),
        ) => {
            let mut event = event.error(
                AccountErrorKind::WebValidationNeeded,
                "Web validation needed".to_owned(),
            );
            event.payload = url.to_string();
            event
        }
        CredentialAuthTokenResult::AccountServerRequstError(err) => {
            event.error(request_error_kind(err), err.to_string())
        }
        err => event.error(AccountErrorKind::Other, err.to_string()),
    }
}

fn account_token_error(event: AccountEvent, err: &AccountTokenResult) -> AccountEvent {
    match err {
        AccountTokenResult::HttpLikeError(err) => {
            event.error(http_error_kind(err), err.to_string())
        }
        AccountTokenResult::FsLikeError(err) => event.error(AccountErrorKind::Fs, err.to_string()),
        AccountTokenResult::AccountServerRequstError(AccountServerRequestError::LogicError(
            AccountTokenError::WebValidationProcessNeeded { url },
        )) => {
            let mut event = event.error(
                AccountErrorKind::WebValidationNeeded,
                "Web validation needed".to_owned(),
            );
            event.payload = url.to_string();
            event
        }
        AccountTokenResult::AccountServerRequstError(err) => {
            event.error(request_error_kind(err), err.to_string())
        }
        err => event.error(AccountErrorKind::Other, err.to_string()),
    }
}

/// Classifies the anyhow wrapped errors of the upstream `Profiles`
/// operations. Each operation has its own result enum with http/fs/logic
/// variants, all of them end up type erased in `anyhow::Error`, so this
/// downcasts against every known enum. The goal is that C++ can reliably
/// distinguish "network down" from e.g. "wrong code".
fn anyhow_error_kind(err: &anyhow::Error) -> AccountErrorKind {
    if let Some(err) = err.downcast_ref::<HttpLikeError>() {
        return http_error_kind(err);
    }
    if err.downcast_ref::<FsLikeError>().is_some() {
        return AccountErrorKind::Fs;
    }
    if let Some(err) = err.downcast_ref::<LoginResult>() {
        return match err {
            LoginResult::HttpLikeError(err) => http_error_kind(err),
            LoginResult::FsLikeError(_) => AccountErrorKind::Fs,
            LoginResult::AccountServerRequstError(err) => request_error_kind(err),
            LoginResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<LogoutResult>() {
        return match err {
            LogoutResult::HttpLikeError(err) => http_error_kind(err),
            LogoutResult::FsLikeError(_) => AccountErrorKind::Fs,
            LogoutResult::SessionWasInvalid | LogoutResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<LogoutAllResult>() {
        return match err {
            LogoutAllResult::HttpLikeError(err) => http_error_kind(err),
            LogoutAllResult::FsLikeError(_) => AccountErrorKind::Fs,
            LogoutAllResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<DeleteResult>() {
        return match err {
            DeleteResult::HttpLikeError(err) => http_error_kind(err),
            DeleteResult::FsLikeError(_) => AccountErrorKind::Fs,
            DeleteResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<LinkCredentialResult>() {
        return match err {
            LinkCredentialResult::HttpLikeError(err) => http_error_kind(err),
            LinkCredentialResult::FsLikeError(_) => AccountErrorKind::Fs,
            LinkCredentialResult::AccountServerRequstError(err) => request_error_kind(err),
            LinkCredentialResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<UnlinkCredentialResult>() {
        return match err {
            UnlinkCredentialResult::HttpLikeError(err) => http_error_kind(err),
            UnlinkCredentialResult::FsLikeError(_) => AccountErrorKind::Fs,
            UnlinkCredentialResult::AccountServerRequstError(err) => request_error_kind(err),
            UnlinkCredentialResult::Other(_) => AccountErrorKind::Other,
        };
    }
    if let Some(err) = err.downcast_ref::<AccountInfoResult>() {
        return match err {
            AccountInfoResult::HttpLikeError(err) => http_error_kind(err),
            AccountInfoResult::FsLikeError(_) => AccountErrorKind::Fs,
            AccountInfoResult::SessionWasInvalid | AccountInfoResult::Other(_) => {
                AccountErrorKind::Other
            }
        };
    }
    AccountErrorKind::Other
}

fn anyhow_error(event: AccountEvent, err: &anyhow::Error) -> AccountEvent {
    event.error(anyhow_error_kind(err), err.to_string())
}

/// The account server certificates downloaded from this url are a trust
/// root, so the url itself must be trustworthy. Loopback addresses are
/// exempt for local testing.
pub fn check_account_server_url(url: &url::Url) -> anyhow::Result<()> {
    let loopback = match url.host() {
        Some(url::Host::Ipv4(ip)) => ip.is_loopback(),
        Some(url::Host::Ipv6(ip)) => ip.is_loopback(),
        Some(url::Host::Domain(domain)) => domain == "localhost",
        None => false,
    };
    if url.scheme() != "https" && !loopback {
        return Err(anyhow!("account server url must use https: {}", url));
    }
    Ok(())
}

impl AccountsClient {
    /// Creates the account manager with profile storage below `base_path`
    /// and the given account server url.
    pub fn new(base_path: &str, account_server_url: &str) -> Self {
        let events: Arc<Mutex<VecDeque<AccountEvent>>> = Default::default();
        let url = match url::Url::parse(account_server_url)
            .map_err(anyhow::Error::from)
            .and_then(|url| check_account_server_url(&url).map(|()| url))
        {
            Ok(url) => url,
            Err(err) => {
                return Self {
                    profiles: None,
                    events,
                    next_request_id: AtomicU64::new(1),
                    error: Some(format!("invalid account server url: {}", err)),
                }
            }
        };
        let factory_url = url.clone();
        let factory = Factory(Box::new(move |path| {
            let url = factory_url.clone();
            Box::pin(async move {
                Ok(ClientHttpTokioFs {
                    http: vec![Arc::new(HttpReqwest::new(url)) as Arc<dyn Http>],
                    cur_http: Default::default(),
                    fs: Fs::new(path).await?,
                })
            })
        }));
        let base_path = PathBuf::from(base_path);
        let loading = runtime().block_on(ProfilesLoading::new(base_path, Arc::new(factory)));
        match loading {
            Ok(loading) => Self {
                profiles: Some(Arc::new(Profiles::new(loading))),
                events,
                next_request_id: AtomicU64::new(1),
                error: None,
            },
            Err(err) => Self {
                profiles: None,
                events,
                next_request_id: AtomicU64::new(1),
                error: Some(err.to_string()),
            },
        }
    }

    /// The error that occurred during creation, if any.
    pub fn error(&self) -> Option<&str> {
        self.error.as_deref()
    }

    /// Polls the next completed operation, if any.
    pub fn poll_event(&self) -> Option<AccountEvent> {
        self.events.lock().pop_front()
    }

    fn spawn<F>(
        &self,
        kind: AccountEventKind,
        run: impl FnOnce(Arc<Profiles<ClientHttpTokioFs, Factory>>, AccountEvent) -> F + Send + 'static,
    ) -> u64
    where
        F: std::future::Future<Output = AccountEvent> + Send + 'static,
    {
        let request_id = self.next_request_id.fetch_add(1, Ordering::Relaxed);
        let event = AccountEvent::new(request_id, kind);
        let events = self.events.clone();
        match self.profiles.clone() {
            Some(profiles) => {
                runtime().spawn(async move {
                    let event = run(profiles, event).await;
                    events.lock().push_back(event);
                });
            }
            None => {
                let error = self
                    .error
                    .clone()
                    .unwrap_or_else(|| "account client not initialized".to_owned());
                events
                    .lock()
                    .push_back(event.error(AccountErrorKind::Other, error));
            }
        }
        request_id
    }

    /// Requests a credential auth token, sent to the given email address.
    pub fn credential_auth_email_token(
        &self,
        email: &str,
        op: CredentialAuthTokenOperation,
        secret_key_hex: Option<String>,
    ) -> u64 {
        let email = email.to_owned();
        self.spawn(
            AccountEventKind::CredentialAuthEmailToken,
            move |profiles, event| async move {
                let email: email_address::EmailAddress = match email.parse() {
                    Ok(email) => email,
                    Err(_) => {
                        return event
                            .error(AccountErrorKind::Other, "invalid email address".to_owned())
                    }
                };
                match profiles
                    .credential_auth_email_token(email, op, secret_key_hex)
                    .await
                {
                    Ok(()) => event.success(),
                    Err(err) => credential_auth_token_error(event, &err),
                }
            },
        )
    }

    /// Requests a credential auth token for the given steam session ticket.
    pub fn credential_auth_steam_token(
        &self,
        steam_ticket: Vec<u8>,
        op: CredentialAuthTokenOperation,
        secret_key_hex: Option<String>,
    ) -> u64 {
        self.spawn(
            AccountEventKind::CredentialAuthSteamToken,
            move |profiles, event| async move {
                match profiles
                    .credential_auth_steam_token(steam_ticket, op, secret_key_hex)
                    .await
                {
                    Ok(token) => {
                        let mut event = event.success();
                        event.payload = token;
                        event
                    }
                    Err(err) => credential_auth_token_error(event, &err),
                }
            },
        )
    }

    /// Requests an account token, sent to the given email address.
    pub fn account_email_token(
        &self,
        email: &str,
        op: AccountTokenOperation,
        secret_key_hex: Option<String>,
    ) -> u64 {
        let email = email.to_owned();
        self.spawn(
            AccountEventKind::AccountEmailToken,
            move |profiles, event| async move {
                let email: email_address::EmailAddress = match email.parse() {
                    Ok(email) => email,
                    Err(_) => {
                        return event
                            .error(AccountErrorKind::Other, "invalid email address".to_owned())
                    }
                };
                match profiles
                    .account_email_token(email, op, secret_key_hex)
                    .await
                {
                    Ok(()) => event.success(),
                    Err(err) => account_token_error(event, &err),
                }
            },
        )
    }

    /// Requests an account token for the given steam session ticket.
    pub fn account_steam_token(
        &self,
        steam_ticket: Vec<u8>,
        op: AccountTokenOperation,
        secret_key_hex: Option<String>,
    ) -> u64 {
        self.spawn(
            AccountEventKind::AccountSteamToken,
            move |profiles, event| async move {
                match profiles
                    .account_steam_token(steam_ticket, op, secret_key_hex)
                    .await
                {
                    Ok(token) => {
                        let mut event = event.success();
                        event.payload = token;
                        event
                    }
                    Err(err) => account_token_error(event, &err),
                }
            },
        )
    }

    /// Logs in with the credential auth token that was sent by email.
    pub fn login_email(&self, email: &str, credential_auth_token_hex: &str) -> u64 {
        let email = email.to_owned();
        let token = credential_auth_token_hex.to_owned();
        self.spawn(AccountEventKind::Login, move |profiles, event| async move {
            let email: email_address::EmailAddress = match email.parse() {
                Ok(email) => email,
                Err(_) => {
                    return event.error(AccountErrorKind::Other, "invalid email address".to_owned())
                }
            };
            match profiles.login_email(email, token).await {
                Ok(profile_key) => {
                    let mut event = event.success();
                    event.payload = profile_key;
                    event
                }
                Err(err) => anyhow_error(event, &err),
            }
        })
    }

    /// Logs in with a credential auth token obtained for a steam session
    /// ticket.
    pub fn login_steam(&self, steam_user_name: &str, credential_auth_token_hex: &str) -> u64 {
        let name = steam_user_name.to_owned();
        let token = credential_auth_token_hex.to_owned();
        self.spawn(AccountEventKind::Login, move |profiles, event| async move {
            match profiles.login_steam(name, token).await {
                Ok(profile_key) => {
                    let mut event = event.success();
                    event.payload = profile_key;
                    event
                }
                Err(err) => anyhow_error(event, &err),
            }
        })
    }

    /// Logs out the given profile and removes it from disk.
    pub fn logout(&self, profile_key: &str) -> u64 {
        let profile_key = profile_key.to_owned();
        self.spawn(
            AccountEventKind::Logout,
            move |profiles, event| async move {
                match profiles.logout(&profile_key).await {
                    Ok(()) => event.success(),
                    Err(err) => anyhow_error(event, &err),
                }
            },
        )
    }

    /// Logs out all other sessions of the account of the given profile.
    pub fn logout_all(&self, profile_key: &str, account_token_hex: &str) -> u64 {
        let profile_key = profile_key.to_owned();
        let token = account_token_hex.to_owned();
        self.spawn(
            AccountEventKind::LogoutAll,
            move |profiles, event| async move {
                match profiles.logout_all(token, &profile_key).await {
                    Ok(()) => event.success(),
                    Err(err) => anyhow_error(event, &err),
                }
            },
        )
    }

    /// Deletes the account of the given profile.
    pub fn delete(&self, profile_key: &str, account_token_hex: &str) -> u64 {
        let profile_key = profile_key.to_owned();
        let token = account_token_hex.to_owned();
        self.spawn(
            AccountEventKind::Delete,
            move |profiles, event| async move {
                match profiles.delete(token, &profile_key).await {
                    Ok(()) => event.success(),
                    Err(err) => anyhow_error(event, &err),
                }
            },
        )
    }

    /// Links another credential to the account of the given profile.
    pub fn link_credential(
        &self,
        profile_key: &str,
        account_token_hex: &str,
        credential_auth_token_hex: &str,
    ) -> u64 {
        let profile_key = profile_key.to_owned();
        let account_token = account_token_hex.to_owned();
        let credential_auth_token = credential_auth_token_hex.to_owned();
        self.spawn(
            AccountEventKind::LinkCredential,
            move |profiles, event| async move {
                match profiles
                    .link_credential(account_token, credential_auth_token, &profile_key)
                    .await
                {
                    Ok(()) => event.success(),
                    Err(err) => anyhow_error(event, &err),
                }
            },
        )
    }

    /// Unlinks a credential from the account of the given profile.
    pub fn unlink_credential(&self, profile_key: &str, credential_auth_token_hex: &str) -> u64 {
        let profile_key = profile_key.to_owned();
        let token = credential_auth_token_hex.to_owned();
        self.spawn(
            AccountEventKind::UnlinkCredential,
            move |profiles, event| async move {
                match profiles.unlink_credential(token, &profile_key).await {
                    Ok(()) => event.success(),
                    Err(err) => anyhow_error(event, &err),
                }
            },
        )
    }

    /// Fetches the account info of the given profile.
    pub fn account_info(&self, profile_key: &str) -> u64 {
        let profile_key = profile_key.to_owned();
        self.spawn(AccountEventKind::AccountInfo, move |profiles, event| async move {
            match profiles.account_info(&profile_key).await {
                Ok(info) => {
                    let mut event = event.success();
                    event.account_id = info.account_id;
                    event.creation_date = info
                        .creation_date
                        .format("%Y-%m-%d %H:%M:%S UTC")
                        .to_string();
                    event.credentials = info
                        .credentials
                        .into_iter()
                        .map(|credential| match credential {
                            ddnet_accounts_shared::account_server::account_info::CredentialType::Email(email) => {
                                AccountCredential {
                                    kind: "email".to_owned(),
                                    identifier: email,
                                }
                            }
                            ddnet_accounts_shared::account_server::account_info::CredentialType::Steam(id) => {
                                AccountCredential {
                                    kind: "steam".to_owned(),
                                    identifier: id.to_string(),
                                }
                            }
                        })
                        .collect();
                    event
                }
                Err(err) => anyhow_error(event, &err),
            }
        })
    }

    /// Requests a certificate and session key for connecting to a game
    /// server, also used to refresh a certificate that is about to expire.
    /// Also works without an account (self signed), a warning is put into
    /// the warning field then. If the upstream profile manager silently
    /// removed the profile (invalid session, fs error), an unsolicited
    /// [`AccountEventKind::Logout`] event with request id 0 and the removed
    /// profile key as payload is emitted additionally.
    pub fn cert_and_key(&self) -> u64 {
        let events = self.events.clone();
        self.spawn(
            AccountEventKind::CertAndKey,
            move |profiles, event| async move {
                let profile_before = {
                    let (profiles, current) = profiles.profiles();
                    profiles.contains_key(&current).then_some(current)
                };
                let (account_data, cert, warning) = profiles.signed_cert_and_key_pair().await;
                if let Some(profile_before) = profile_before {
                    if !profiles.profiles().0.contains_key(&profile_before) {
                        let mut removed = AccountEvent::new(0, AccountEventKind::Logout);
                        removed.success = true;
                        removed.payload = profile_before;
                        events.lock().push_back(removed);
                    }
                }
                let mut event = event.success();
                match (cert.to_der(), account_data.private_key.to_pkcs8_der()) {
                    (Ok(cert_der), Ok(key_der)) => {
                        event.cert_der = cert_der;
                        event.key_der = key_der.as_bytes().to_vec();
                        if let Some(warning) = warning {
                            event.warning = warning.to_string();
                        }
                        event
                    }
                    (Err(err), _) => anyhow_error(
                        AccountEvent::new(event.request_id, event.kind),
                        &anyhow!(err),
                    ),
                    (_, Err(err)) => anyhow_error(
                        AccountEvent::new(event.request_id, event.kind),
                        &anyhow!(err),
                    ),
                }
            },
        )
    }

    /// Currently stored profiles and which one is active.
    pub fn profiles(&self) -> Vec<ProfileInfo> {
        let Some(profiles) = &self.profiles else {
            return Vec::new();
        };
        let (profiles, current) = profiles.profiles();
        let mut infos: Vec<ProfileInfo> = profiles
            .into_iter()
            .map(|(key, data)| ProfileInfo {
                current: key == current,
                key,
                display_name: data.name,
            })
            .collect();
        infos.sort_by(|a, b| a.key.cmp(&b.key));
        infos
    }

    /// Switches the active profile.
    pub fn set_profile(&self, profile_key: &str) {
        if let Some(profiles) = self.profiles.clone() {
            let profile_key = profile_key.to_owned();
            runtime().spawn(async move {
                profiles.set_profile(&profile_key).await;
            });
        }
    }

    /// Changes the display name of a profile.
    pub fn set_profile_display_name(&self, profile_key: &str, display_name: &str) {
        if let Some(profiles) = self.profiles.clone() {
            let profile_key = profile_key.to_owned();
            let display_name = display_name.to_owned();
            runtime().spawn(async move {
                profiles
                    .set_profile_display_name(&profile_key, display_name)
                    .await;
            });
        }
    }
}

/// Seconds until the given der certificate expires. Negative if already
/// expired.
pub fn cert_expires_in_seconds(cert_der: &[u8]) -> i64 {
    use x509_cert::der::Decode;
    let Ok(cert) = x509_cert::Certificate::from_der(cert_der) else {
        return 0;
    };
    let expires_at = cert.tbs_certificate.validity.not_after.to_system_time();
    match expires_at.duration_since(SystemTime::now()) {
        Ok(duration) => duration.as_secs() as i64,
        Err(err) => -(err.duration().as_secs() as i64),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ddnet_accounts_shared::account_server::login::LoginError;

    fn classify(err: anyhow::Error) -> AccountErrorKind {
        anyhow_error(AccountEvent::new(1, AccountEventKind::Login), &err).error_kind
    }

    #[test]
    fn login_error_classification() {
        // Network down vs. wrong code must be distinguishable for C++.
        assert_eq!(
            classify(anyhow::Error::new(LoginResult::HttpLikeError(
                HttpLikeError::Request
            ))),
            AccountErrorKind::Http
        );
        assert_eq!(
            classify(anyhow::Error::new(LoginResult::FsLikeError(
                FsLikeError::Fs(std::io::Error::other("disk broken"))
            ))),
            AccountErrorKind::Fs
        );
        assert_eq!(
            classify(anyhow::Error::new(LoginResult::AccountServerRequstError(
                AccountServerRequestError::RateLimited("slow down".to_owned())
            ))),
            AccountErrorKind::RateLimited
        );
        let logic = anyhow::Error::new(LoginResult::AccountServerRequstError(
            AccountServerRequestError::LogicError(LoginError::TokenInvalid),
        ));
        let event = anyhow_error(AccountEvent::new(1, AccountEventKind::Login), &logic);
        assert_eq!(event.error_kind, AccountErrorKind::Other);
        assert!(event.error.contains("not valid anymore"), "{}", event.error);
        // Errors from other layers of the upstream code stay Other.
        assert_eq!(classify(anyhow!("unknown")), AccountErrorKind::Other);
        // Directly wrapped http/fs errors, e.g. from the profile factory.
        assert_eq!(
            classify(anyhow::Error::new(HttpLikeError::Status(500))),
            AccountErrorKind::Http
        );
    }
}
