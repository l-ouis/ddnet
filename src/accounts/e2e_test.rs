//! Live end to end test against a locally running account server.
//!
//! Needs the account server on `http://127.0.0.1:5555` with its database,
//! and mailpit on `http://127.0.0.1:8025` receiving the emails of the
//! account server.
//!
//! ```text
//! cargo test -p ddnet-accounts-bridge -- --ignored e2e_live
//! ```

use std::time::{Duration, Instant};

use crate::client::{AccountEvent, AccountEventKind, AccountsClient};
use crate::game_server::AccountsGameServer;

const ACCOUNT_SERVER_URL: &str = "http://127.0.0.1:5555";
const MAILPIT_URL: &str = "http://127.0.0.1:8025";

fn wait_account_event(client: &AccountsClient, request_id: u64) -> AccountEvent {
    let start = Instant::now();
    loop {
        if let Some(event) = client.poll_event() {
            if event.request_id == request_id {
                return event;
            }
            continue;
        }
        if start.elapsed() > Duration::from_secs(30) {
            panic!("timeout waiting for account event");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

fn http_get(url: &str) -> String {
    crate::runtime::runtime().block_on(async {
        reqwest::get(url)
            .await
            .expect("request failed")
            .text()
            .await
            .expect("no text response")
    })
}

/// Fetches the newest email sent to `email` from mailpit and extracts the
/// contained token.
fn token_from_email(email: &str) -> String {
    let start = Instant::now();
    loop {
        let messages = http_get(&format!("{}/api/v1/messages", MAILPIT_URL));
        let messages: serde_json::Value = serde_json::from_str(&messages).unwrap();
        let message_id = messages["messages"]
            .as_array()
            .into_iter()
            .flatten()
            .find(|message| {
                message["To"]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .any(|to| to["Address"].as_str() == Some(email))
            })
            .and_then(|message| message["ID"].as_str().map(String::from));
        if let Some(message_id) = message_id {
            let message = http_get(&format!("{}/api/v1/message/{}", MAILPIT_URL, message_id));
            let message: serde_json::Value = serde_json::from_str(&message).unwrap();
            let text = message["Text"].as_str().unwrap_or_default().to_owned()
                + message["HTML"].as_str().unwrap_or_default();
            let token: String = text
                .split(|char: char| !char.is_ascii_hexdigit())
                .filter(|part| part.len() >= 20)
                .map(String::from)
                .next()
                .expect("no token in email");
            return token;
        }
        if start.elapsed() > Duration::from_secs(30) {
            panic!("timeout waiting for the token email of {}", email);
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

/// Logs in to the account server and connects to a running DDNet server
/// over QUIC as that account. The server log should show the account id.
///
/// Environment: `DDNET_E2E_QUIC_ADDR` (e.g. `127.0.0.1:8305`),
/// `DDNET_E2E_QUIC_HASH_HEX` (from the server startup log) and
/// `DDNET_E2E_PROFILES_DIR` for the client profile storage.
#[test]
#[ignore = "needs a local account server, mailpit and a running DDNet server with sv_quic"]
fn e2e_cpp_server_login() {
    let addr = std::env::var("DDNET_E2E_QUIC_ADDR").expect("DDNET_E2E_QUIC_ADDR");
    let hash_hex = std::env::var("DDNET_E2E_QUIC_HASH_HEX").expect("DDNET_E2E_QUIC_HASH_HEX");
    let profiles_dir = std::env::var("DDNET_E2E_PROFILES_DIR").expect("DDNET_E2E_PROFILES_DIR");
    let mut hash = [0; 32];
    for i in 0..32 {
        hash[i] = u8::from_str_radix(&hash_hex[i * 2..i * 2 + 2], 16).unwrap();
    }
    std::fs::create_dir_all(&profiles_dir).unwrap();

    let client = AccountsClient::new(&profiles_dir, ACCOUNT_SERVER_URL);
    assert!(client.error().is_none(), "{:?}", client.error());
    if client.profiles().is_empty() {
        let email = format!("cpp-e2e-{}@example.com", std::process::id());
        let event = wait_account_event(
            &client,
            client.credential_auth_email_token(
                &email,
                ddnet_accounts_shared::client::credential_auth_token::CredentialAuthTokenOperation::Login,
                None,
            ),
        );
        assert!(event.success, "token request failed: {}", event.error);
        let token = token_from_email(&email);
        let event = wait_account_event(&client, client.login_email(&email, &token));
        assert!(event.success, "login failed: {}", event.error);
    }
    let event = wait_account_event(&client, client.cert_and_key());
    assert!(event.success);
    assert!(event.error.is_empty(), "cert warning: {}", event.error);

    let mut quic = crate::quic::QuicClient::connect(
        addr,
        crate::quic::ServerVerification::PubKeyHash(hash),
        event.cert_der,
        event.key_der,
        Duration::from_secs(5),
    );
    let start = Instant::now();
    loop {
        match quic.poll_event() {
            Some(crate::quic::Event::Connected { .. }) => break,
            Some(event) => panic!("unexpected transport event: {:?}", event),
            None => {
                assert!(start.elapsed() < Duration::from_secs(10), "connect timeout");
                std::thread::sleep(Duration::from_millis(10));
            }
        }
    }
    // give the server time to resolve the account before disconnecting
    std::thread::sleep(Duration::from_secs(2));
    quic.close("bye");
}

#[test]
#[ignore = "needs a local account server and mailpit, see the module documentation"]
fn e2e_live() {
    let base_path = std::env::temp_dir().join(format!("ddnet-accounts-e2e-{}", std::process::id()));
    let client_path = base_path.join("client");
    let server_path = base_path.join("server");
    std::fs::create_dir_all(&client_path).unwrap();
    std::fs::create_dir_all(&server_path).unwrap();

    // request a login token by email
    let client = AccountsClient::new(client_path.to_str().unwrap(), ACCOUNT_SERVER_URL);
    assert!(client.error().is_none(), "{:?}", client.error());
    let email = format!("e2e-{}@example.com", std::process::id());
    let event = wait_account_event(
        &client,
        client.credential_auth_email_token(
            &email,
            ddnet_accounts_shared::client::credential_auth_token::CredentialAuthTokenOperation::Login,
            None,
        ),
    );
    assert!(event.success, "token request failed: {}", event.error);

    // log in with the token from the email
    let token = token_from_email(&email);
    let event = wait_account_event(&client, client.login_email(&email, &token));
    assert!(event.success, "login failed: {}", event.error);
    let profile_key = event.payload.clone();
    assert!(profile_key.starts_with("acc_"));

    // obtain a certificate signed by the account server
    let event = wait_account_event(&client, client.cert_and_key());
    assert!(event.success);
    assert!(event.error.is_empty(), "cert warning: {}", event.error);
    assert!(!event.cert_der.is_empty());
    assert!(!event.key_der.is_empty());
    let cert_der = event.cert_der.clone();

    // the game server resolves the account from the certificate
    let game_server = AccountsGameServer::new(
        server_path.join("accounts.sqlite").to_str().unwrap(),
        server_path.to_str().unwrap(),
        ACCOUNT_SERVER_URL,
    );
    assert!(game_server.error().is_none(), "{:?}", game_server.error());
    let request_id = game_server.begin_login(cert_der.clone());
    let login = loop {
        if let Some(login) = game_server.poll_login() {
            break login;
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert_eq!(login.request_id, request_id);
    assert!(login.error.is_empty(), "{}", login.error);
    assert!(login.account_id > 0);
    assert_eq!(profile_key, format!("acc_{}", login.account_id));
    assert!(login.new_account);

    // the account is only registered once
    game_server.begin_login(cert_der);
    let login_again = loop {
        if let Some(login_again) = game_server.poll_login() {
            break login_again;
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert_eq!(login_again.account_id, login.account_id);
    assert!(!login_again.new_account);

    // account info round trip
    let event = wait_account_event(&client, client.account_info(&profile_key));
    assert!(event.success, "account info failed: {}", event.error);
    assert_eq!(event.account_id, login.account_id);
    assert_eq!(event.credentials.len(), 1);
    assert_eq!(event.credentials[0].kind, "email");

    // logout removes the profile
    let event = wait_account_event(&client, client.logout(&profile_key));
    assert!(event.success, "logout failed: {}", event.error);
    assert!(client.profiles().is_empty());

    std::fs::remove_dir_all(&base_path).unwrap();
}
