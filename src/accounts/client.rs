//! Client-side account operations: request a login token (emailed), log in,
//! and produce the short-lived account-signed certificate that the client
//! presents to game servers over QUIC.
//!
//! Wraps `ddnet-account-client` (+ the reqwest/fs concrete client), driving its
//! async API on the shared tokio runtime.

use std::path::Path;
use std::str::FromStr;

use ddnet_account_client::credential_auth_token::credential_auth_token_email;
use ddnet_account_client::login::login as account_login;
use ddnet_account_client::logout::logout as account_logout;
use ddnet_account_client::sign::sign as account_sign;
use ddnet_account_client_reqwest::client::ClientReqwestTokioFs;
use ddnet_accounts_shared::client::credential_auth_token::CredentialAuthTokenOperation;
use email_address::EmailAddress;
use url::Url;

use crate::transport::runtime;

/// Owns the account client (HTTP client + on-disk profile/session storage).
pub struct AccountClient {
    client: ClientReqwestTokioFs,
}

/// Downloads the account server's CA certificates (DER) without a persistent
/// session — used by game servers to verify account-signed client certs.
pub fn download_ca_certs_from(account_server_url: &str) -> anyhow::Result<Vec<Vec<u8>>> {
    use x509_cert::der::Encode;
    let url = Url::parse(account_server_url)?;
    let dir = std::env::temp_dir().join("ddnet-account-ca-fetch");
    std::fs::create_dir_all(&dir)?;
    runtime().block_on(async move {
        let client = ClientReqwestTokioFs::new(vec![url], &dir).await?;
        let certs = ddnet_account_client::certs::download_certs(&*client).await?;
        certs
            .iter()
            .map(|c| c.to_der().map_err(anyhow::Error::from))
            .collect::<anyhow::Result<Vec<_>>>()
    })
}

/// Connects an account client to `account_server_url`, storing session data
/// (the ed25519 key pair, profiles) under `secure_dir`.
pub fn open(account_server_url: &str, secure_dir: &str) -> anyhow::Result<AccountClient> {
    let url = Url::parse(account_server_url)?;
    let secure_dir = secure_dir.to_string();
    runtime().block_on(async move {
        let client = ClientReqwestTokioFs::new(vec![url], Path::new(&secure_dir)).await?;
        anyhow::Ok(AccountClient { client })
    })
}

impl AccountClient {
    /// Requests a login credential-auth token for `email`. The account server
    /// emails the token/code to the user; the user then calls [`Self::login`]
    /// with it. Returns an empty string on success, else the error.
    pub fn request_login_token_email(&self, email: &str) -> String {
        let res: anyhow::Result<()> = runtime().block_on(async {
            let email = EmailAddress::from_str(email)?;
            credential_auth_token_email(
                email,
                CredentialAuthTokenOperation::Login,
                None,
                &*self.client,
            )
            .await?;
            Ok(())
        });
        res.err().map(|e| e.to_string()).unwrap_or_default()
    }

    /// Logs in with the token from the email (creating the account on first
    /// login) and persists the session. Returns (error, account_id).
    pub fn login(&self, token_hex: String) -> (String, i64) {
        let res: anyhow::Result<i64> = runtime().block_on(async {
            let (account_id, writer) = account_login(token_hex, &*self.client).await?;
            writer.write(&*self.client).await?;
            Ok(account_id)
        });
        match res {
            Ok(account_id) => (String::new(), account_id),
            Err(e) => (e.to_string(), 0),
        }
    }

    /// Produces a fresh account-signed certificate to present to a game server
    /// over QUIC, together with its ed25519 private key (PKCS#8 DER) needed for
    /// the mutual-TLS handshake. Returns (error, cert_der, key_der).
    pub fn sign(&self) -> (String, Vec<u8>, Vec<u8>) {
        use ed25519_dalek::pkcs8::EncodePrivateKey;
        let res: anyhow::Result<(Vec<u8>, Vec<u8>)> = runtime().block_on(async {
            let data = account_sign(&*self.client).await?;
            let key_der = data
                .session_key_pair
                .private_key
                .to_pkcs8_der()?
                .as_bytes()
                .to_vec();
            Ok((data.certificate_der, key_der))
        });
        match res {
            Ok((cert_der, key_der)) => (String::new(), cert_der, key_der),
            Err(e) => (e.to_string(), Vec::new(), Vec::new()),
        }
    }

    /// Downloads the account server's CA certificates (DER) used by game
    /// servers to verify account-signed client certificates.
    pub fn download_ca_certs(&self) -> anyhow::Result<Vec<Vec<u8>>> {
        use x509_cert::der::Encode;
        runtime().block_on(async {
            let certs = ddnet_account_client::certs::download_certs(&*self.client).await?;
            certs
                .iter()
                .map(|c| c.to_der().map_err(anyhow::Error::from))
                .collect::<anyhow::Result<Vec<_>>>()
        })
    }

    /// Logs out the current session. Returns an empty string on success.
    pub fn logout(&self) -> String {
        let res: anyhow::Result<()> = runtime().block_on(async {
            account_logout(&*self.client).await?;
            Ok(())
        });
        res.err().map(|e| e.to_string()).unwrap_or_default()
    }
}
