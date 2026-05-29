//! Live end-to-end test that drives the *client* bridge against a running
//! account server (+ MailHog), then resolves the resulting account-signed
//! certificate through the *game-server* bridge.
//!
//! Gated by `ACCOUNT_E2E_URL`; skipped in normal test runs. To run:
//!   ACCOUNT_E2E_URL=http://127.0.0.1:5455 cargo test -p ddnet-accounts-bridge \
//!     --lib e2e_live -- --ignored --nocapture --test-threads=1

use crate::{client, game_server, transport};
use std::process::Command;
use std::time::{Duration, Instant};

// Fetches the newest MailHog message addressed to `to` and extracts the
// `<pre>token</pre>` from the fallback email template.
fn mailhog_latest_token(mailhog_api: &str, to: &str) -> String {
    let script = format!(
        r#"
import json,urllib.request,re,sys
d=json.load(urllib.request.urlopen("{api}/api/v2/messages"))
for m in d["items"]:
    hdrs=m["Content"]["Headers"]
    if any("{to}" in v for v in hdrs.get("To",[])):
        body=m["Content"]["Body"].replace("=\r\n","").replace("=\n","")
        mm=re.search(r"<pre>([0-9a-fA-F]+)</pre>", body)
        if mm:
            print(mm.group(1)); sys.exit(0)
sys.exit(1)
"#,
        api = mailhog_api,
        to = to
    );
    let out = Command::new("python3")
        .arg("-c")
        .arg(script)
        .output()
        .expect("run python3");
    assert!(
        out.status.success(),
        "no token found in MailHog: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    String::from_utf8_lossy(&out.stdout).trim().to_string()
}

#[test]
#[ignore = "requires a running account server (set ACCOUNT_E2E_URL)"]
fn live_account_login_sign_resolve() {
    let url = std::env::var("ACCOUNT_E2E_URL").expect("set ACCOUNT_E2E_URL");
    let mailhog =
        std::env::var("MAILHOG_API").unwrap_or_else(|_| "http://127.0.0.1:8025".to_string());

    let secure = std::env::temp_dir().join(format!("ddnet-acc-e2e-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&secure);
    std::fs::create_dir_all(&secure).unwrap();
    let secure_s = secure.to_string_lossy().to_string();

    // --- client side: request token -> read email -> login -> sign ---
    let c = client::open(&url, &secure_s).expect("open account client");

    let err = c.request_login_token_email("test@example.com");
    assert!(err.is_empty(), "token request failed: {err}");

    let token = mailhog_latest_token(&mailhog, "test@example.com");
    assert!(!token.is_empty(), "empty token");

    let (err, account_id) = c.login(token);
    assert!(err.is_empty(), "login failed: {err}");
    assert!(account_id >= 1, "expected a real account id, got {account_id}");

    let (err, cert_der, key_der) = c.sign();
    assert!(err.is_empty(), "sign failed: {err}");
    assert!(!cert_der.is_empty(), "empty signed cert");
    assert!(!key_der.is_empty(), "empty signed key");

    let ca_certs = c.download_ca_certs().expect("download CA certs");
    assert!(!ca_certs.is_empty(), "no CA certs from account server");

    // --- present the account cert over a REAL QUIC mTLS connection ---
    // This proves possession of the key and that the game server reads the
    // account-signed peer certificate exactly as it will in production.
    let srv_id = transport::generate_self_signed().expect("server identity");
    let server = transport::server(
        "127.0.0.1:0".parse().unwrap(),
        srv_id.cert_der.clone(),
        srv_id.key_der.clone(),
    )
    .expect("quic server");
    let addr = server.local_addr().unwrap();
    let _quic_client = transport::client(
        addr,
        "ddnet",
        transport::ServerTrust::PinnedPublicKey(srv_id.public_key_fingerprint),
        cert_der.clone(),
        key_der.clone(),
    )
    .expect("quic client");

    // Wait for the server to observe the connection and capture the peer cert.
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut peer_cert = Vec::new();
    'outer: while Instant::now() < deadline {
        for ev in server.poll_events() {
            if let transport::Event::Connected { peer_cert_der, .. } = ev {
                peer_cert = peer_cert_der;
                break 'outer;
            }
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    assert!(!peer_cert.is_empty(), "server never saw the QUIC connection");
    assert_eq!(peer_cert, cert_der, "peer cert over QUIC must be the account cert");

    // --- game-server side: resolve the account cert received over QUIC ---
    let dbfile = std::env::temp_dir().join(format!("ddnet-acc-e2e-{}.sqlite", std::process::id()));
    let _ = std::fs::remove_file(&dbfile);
    let mut gs = game_server::open(&dbfile.to_string_lossy()).expect("open game-server db");
    for cert in &ca_certs {
        assert!(gs.add_ca_cert(cert), "failed to load a CA cert");
    }
    let outcome = gs.login_by_cert(&peer_cert);
    assert!(outcome.error.is_empty(), "resolve failed: {}", outcome.error);
    assert_eq!(
        outcome.account_id, account_id,
        "account id resolved by the game server must match the login"
    );
    assert!(outcome.created, "first login should create the game-server user row");

    eprintln!(
        "E2E OK: account_id={account_id} flowed client-bridge -> account-server -> QUIC mTLS -> game-server-bridge"
    );

    let _ = std::fs::remove_dir_all(&secure);
    let _ = std::fs::remove_file(&dbfile);
}
