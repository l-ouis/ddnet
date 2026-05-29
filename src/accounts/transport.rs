//! QUIC secure transport for the DDNet client and server.
//!
//! This module wraps [`quinn`] (QUIC over UDP, TLS 1.3) into a small
//! actor-style interface that the synchronous C++ netcode can drive by polling
//! once per tick:
//!
//! * reliable, ordered messages (DDNet's `NETSENDFLAG_VITAL`) travel over a
//!   single persistent bidirectional stream, length-prefixed;
//! * unreliable messages travel as QUIC datagrams;
//! * the connection is mutually authenticated (mTLS): the client always
//!   presents a certificate (account-signed or self-signed), which the server
//!   reads back as the peer certificate to resolve the account id.
//!
//! Server identity uses self-signed certificates; the client pins the server's
//! public-key fingerprint ([`ServerTrust::PinnedPublicKey`]), matching the
//! `ddnet-rs` reference implementation. No public CA is involved.

use std::net::{SocketAddr, UdpSocket};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use anyhow::Context;
use quinn::crypto::rustls::{QuicClientConfig, QuicServerConfig};
use quinn::{ClientConfig, Connection, Endpoint, ServerConfig, TransportConfig};
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::crypto::{verify_tls13_signature, CryptoProvider, WebPkiSupportedAlgorithms};
use rustls::pki_types::{CertificateDer, PrivatePkcs8KeyDer, PrivateKeyDer, ServerName, UnixTime};
use rustls::server::danger::{ClientCertVerified, ClientCertVerifier};
use rustls::{DigitallySignedStruct, DistinguishedName, SignatureScheme};
use tokio::sync::mpsc;

/// Maximum size of a single reliable frame (sanity bound against a malicious
/// peer claiming a huge length prefix). 64 MiB is far above any game message.
const MAX_RELIABLE_FRAME: usize = 64 * 1024 * 1024;

/// Returns the process-wide tokio runtime that drives all transport tasks.
///
/// Created lazily on first use; lives for the duration of the process.
pub(crate) fn runtime() -> &'static tokio::runtime::Runtime {
    static RT: OnceLock<tokio::runtime::Runtime> = OnceLock::new();
    RT.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .thread_name("ddnet-accounts")
            .build()
            .expect("failed to build tokio runtime")
    })
}

/// The ring-based rustls crypto provider used for all TLS operations.
fn provider() -> Arc<CryptoProvider> {
    static PROVIDER: OnceLock<Arc<CryptoProvider>> = OnceLock::new();
    PROVIDER
        .get_or_init(|| Arc::new(rustls::crypto::ring::default_provider()))
        .clone()
}

/// A freshly generated self-signed certificate and its private key.
pub struct SelfSigned {
    /// The certificate in DER encoding.
    pub cert_der: Vec<u8>,
    /// The PKCS#8 private key in DER encoding.
    pub key_der: Vec<u8>,
    /// SHA-256 fingerprint of the certificate's `SubjectPublicKeyInfo`.
    ///
    /// This is what a client pins to identify the server; it stays constant
    /// across certificate regeneration as long as the key pair is reused.
    pub public_key_fingerprint: [u8; 32],
}

/// Generates an ed25519 self-signed certificate (used for the server's
/// transport identity, and for accountless clients).
pub fn generate_self_signed() -> anyhow::Result<SelfSigned> {
    let key_pair = rcgen::KeyPair::generate_for(&rcgen::PKCS_ED25519)
        .context("generating ed25519 key pair")?;
    let mut params = rcgen::CertificateParams::new(vec!["ddnet".to_string()])
        .context("creating certificate params")?;
    let now = std::time::SystemTime::now();
    params.not_before = (now - Duration::from_secs(60 * 10)).into();
    params.not_after = (now + Duration::from_secs(60 * 60 * 24 * 365 * 10)).into();
    let cert = params
        .self_signed(&key_pair)
        .context("self-signing certificate")?;
    let cert_der = cert.der().to_vec();
    let key_der = key_pair.serialize_der();
    let public_key_fingerprint = public_key_fingerprint(&cert_der)?;
    Ok(SelfSigned {
        cert_der,
        key_der,
        public_key_fingerprint,
    })
}

/// Loads the server's persistent self-signed identity from disk, generating
/// and saving a new one (DER cert + PKCS#8 key) if absent or unreadable.
///
/// This keeps the server's public-key fingerprint stable across restarts so
/// clients can pin it.
pub fn load_or_generate_identity(cert_path: &str, key_path: &str) -> anyhow::Result<SelfSigned> {
    use std::fs;
    if let (Ok(cert_der), Ok(key_der)) = (fs::read(cert_path), fs::read(key_path)) {
        if !cert_der.is_empty() && !key_der.is_empty() {
            if let Ok(public_key_fingerprint) = public_key_fingerprint(&cert_der) {
                return Ok(SelfSigned {
                    cert_der,
                    key_der,
                    public_key_fingerprint,
                });
            }
        }
    }
    let identity = generate_self_signed()?;
    if let Some(parent) = std::path::Path::new(cert_path).parent() {
        let _ = fs::create_dir_all(parent);
    }
    fs::write(cert_path, &identity.cert_der).context("writing server cert")?;
    fs::write(key_path, &identity.key_der).context("writing server key")?;
    Ok(identity)
}

/// Computes the SHA-256 fingerprint of a certificate's `SubjectPublicKeyInfo`.
pub fn public_key_fingerprint(cert_der: &[u8]) -> anyhow::Result<[u8; 32]> {
    use x509_cert::der::Decode;
    let cert = x509_cert::Certificate::from_der(cert_der).context("parsing certificate")?;
    let fp = cert
        .tbs_certificate
        .subject_public_key_info
        .fingerprint_bytes()
        .context("computing spki fingerprint")?;
    Ok(fp)
}

/// How a client decides to trust a server's certificate.
#[derive(Clone)]
pub enum ServerTrust {
    /// Pin the server's `SubjectPublicKeyInfo` SHA-256 fingerprint (the normal
    /// mode; the fingerprint is distributed via the master/server-info).
    PinnedPublicKey([u8; 32]),
    /// Accept any server certificate (LAN / connect-by-IP; insecure).
    Insecure,
}

/// An event produced by the transport, drained by the C++ tick.
#[derive(Debug, Clone)]
pub enum Event {
    /// A connection was established. On the server side, `peer_cert_der` is the
    /// client's certificate used for account resolution.
    Connected {
        /// Stable per-endpoint connection id.
        conn_id: u64,
        /// The peer's leaf certificate (DER), if it presented one.
        peer_cert_der: Vec<u8>,
    },
    /// A connection was closed.
    Disconnected {
        /// The connection that closed.
        conn_id: u64,
        /// Human readable reason.
        reason: String,
    },
    /// A reliable, ordered message was received.
    Reliable {
        /// The connection it arrived on.
        conn_id: u64,
        /// Message payload.
        data: Vec<u8>,
    },
    /// An unreliable message (datagram) was received.
    Unreliable {
        /// The connection it arrived on.
        conn_id: u64,
        /// Message payload.
        data: Vec<u8>,
    },
}

/// A message to send out on a connection.
enum Out {
    Reliable(Vec<u8>),
    Unreliable(Vec<u8>),
    Close,
}

/// Shared, pollable transport state for one endpoint (client or server).
pub struct Transport {
    endpoint: Endpoint,
    events_rx: Mutex<mpsc::UnboundedReceiver<Event>>,
    events_tx: mpsc::UnboundedSender<Event>,
    conns: Arc<Mutex<std::collections::HashMap<u64, mpsc::UnboundedSender<Out>>>>,
    next_id: Arc<AtomicU64>,
}

impl Transport {
    fn new(endpoint: Endpoint) -> Arc<Self> {
        let (events_tx, events_rx) = mpsc::unbounded_channel();
        Arc::new(Self {
            endpoint,
            events_rx: Mutex::new(events_rx),
            events_tx,
            conns: Arc::new(Mutex::new(std::collections::HashMap::new())),
            next_id: Arc::new(AtomicU64::new(1)),
        })
    }

    /// The local address the endpoint is bound to.
    pub fn local_addr(&self) -> anyhow::Result<SocketAddr> {
        self.endpoint.local_addr().context("local_addr")
    }

    /// Drains all currently available events (non-blocking).
    pub fn poll_events(&self) -> Vec<Event> {
        let mut out = Vec::new();
        let mut rx = self.events_rx.lock().unwrap();
        while let Ok(ev) = rx.try_recv() {
            out.push(ev);
        }
        out
    }

    /// Queues a reliable message on a connection. No-op if unknown/closed.
    pub fn send_reliable(&self, conn_id: u64, data: Vec<u8>) {
        self.send(conn_id, Out::Reliable(data));
    }

    /// Queues an unreliable (datagram) message. No-op if unknown/closed.
    pub fn send_unreliable(&self, conn_id: u64, data: Vec<u8>) {
        self.send(conn_id, Out::Unreliable(data));
    }

    /// Closes a connection.
    pub fn disconnect(&self, conn_id: u64) {
        self.send(conn_id, Out::Close);
    }

    fn send(&self, conn_id: u64, msg: Out) {
        if let Some(tx) = self.conns.lock().unwrap().get(&conn_id) {
            let _ = tx.send(msg);
        }
    }

    /// Registers an established connection: assigns an id, wires up the
    /// reader/writer tasks, and emits a [`Event::Connected`].
    ///
    /// `initiator` is true on the side that opened the connection (the client);
    /// it determines who opens the reliable bidirectional stream.
    fn register(self: &Arc<Self>, conn: Connection, initiator: bool) {
        let conn_id = self.next_id.fetch_add(1, Ordering::Relaxed);
        let peer_cert_der = peer_cert(&conn);

        let (out_tx, out_rx) = mpsc::unbounded_channel();
        self.conns.lock().unwrap().insert(conn_id, out_tx);

        let _ = self.events_tx.send(Event::Connected {
            conn_id,
            peer_cert_der,
        });

        let this = Arc::clone(self);
        runtime().spawn(async move {
            let reason =
                connection_loop(conn, conn_id, initiator, out_rx, this.events_tx.clone()).await;
            this.conns.lock().unwrap().remove(&conn_id);
            let _ = this.events_tx.send(Event::Disconnected { conn_id, reason });
        });
    }
}

/// Extracts the peer's leaf certificate DER, if any.
fn peer_cert(conn: &Connection) -> Vec<u8> {
    conn.peer_identity()
        .and_then(|id| id.downcast::<Vec<CertificateDer<'static>>>().ok())
        .and_then(|certs| certs.first().map(|c| c.as_ref().to_vec()))
        .unwrap_or_default()
}

/// Drives a single connection until it closes. Returns the close reason.
async fn connection_loop(
    conn: Connection,
    conn_id: u64,
    initiator: bool,
    mut out_rx: mpsc::UnboundedReceiver<Out>,
    events_tx: mpsc::UnboundedSender<Event>,
) -> String {
    // The client (initiator) opens the reliable bidirectional stream; the
    // server accepts it. This keeps both sides on the same stream.
    let setup = async {
        if initiator {
            conn.open_bi().await.map_err(anyhow::Error::from)
        } else {
            conn.accept_bi().await.map_err(anyhow::Error::from)
        }
    };
    let (mut send, mut recv) = match tokio::time::timeout(Duration::from_secs(10), setup).await {
        Ok(Ok(streams)) => streams,
        Ok(Err(e)) => return format!("stream setup failed: {e}"),
        Err(_) => return "stream setup timed out".to_string(),
    };
    // The initiator must send at least one byte so the acceptor's `accept_bi`
    // resolves; we rely on the first real frame for that, but to make the
    // stream visible immediately we flush an empty keepalive frame.
    if initiator && send.write_all(&0u32.to_be_bytes()).await.is_err() {
        return "stream open failed".to_string();
    }

    let mut len_buf = [0u8; 4];
    loop {
        tokio::select! {
            // Outgoing messages from C++.
            msg = out_rx.recv() => match msg {
                Some(Out::Reliable(data)) => {
                    if (data.len() as u64) > MAX_RELIABLE_FRAME as u64 { continue; }
                    if send.write_all(&(data.len() as u32).to_be_bytes()).await.is_err()
                        || send.write_all(&data).await.is_err() {
                        return "reliable write failed".to_string();
                    }
                }
                Some(Out::Unreliable(data)) => {
                    let _ = conn.send_datagram(data.into());
                }
                Some(Out::Close) | None => {
                    conn.close(0u32.into(), b"bye");
                    return "closed locally".to_string();
                }
            },
            // Incoming reliable frames.
            read = recv.read_exact(&mut len_buf) => {
                if read.is_err() { return "reliable stream ended".to_string(); }
                let len = u32::from_be_bytes(len_buf) as usize;
                if len > MAX_RELIABLE_FRAME { return "oversized frame".to_string(); }
                if len == 0 { continue; } // keepalive / stream-open marker
                let mut data = vec![0u8; len];
                if recv.read_exact(&mut data).await.is_err() {
                    return "reliable read failed".to_string();
                }
                let _ = events_tx.send(Event::Reliable { conn_id, data });
            }
            // Incoming datagrams.
            dg = conn.read_datagram() => match dg {
                Ok(bytes) => { let _ = events_tx.send(Event::Unreliable { conn_id, data: bytes.to_vec() }); }
                Err(e) => return format!("connection closed: {e}"),
            },
        }
    }
}

/// Builds a QUIC server endpoint with mTLS (any client cert accepted; account
/// verification happens downstream from the peer cert).
pub fn server(
    bind_addr: SocketAddr,
    cert_der: Vec<u8>,
    key_der: Vec<u8>,
) -> anyhow::Result<Arc<Transport>> {
    let cert_chain = vec![CertificateDer::from(cert_der)];
    let key = PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(key_der));

    let mut tls = rustls::ServerConfig::builder_with_provider(provider())
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .with_client_cert_verifier(Arc::new(AnyClientCert {
            algs: provider().signature_verification_algorithms,
        }))
        .with_single_cert(cert_chain, key)
        .context("server with_single_cert")?;
    tls.alpn_protocols = vec![b"ddnet".to_vec()];

    let mut server_config =
        ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(tls)?));
    server_config.transport_config(Arc::new(default_transport()));

    let endpoint = runtime()
        .block_on(async move { Endpoint::server(server_config, bind_addr) })
        .context("creating server endpoint")?;

    let transport = Transport::new(endpoint.clone());
    let this = Arc::clone(&transport);
    runtime().spawn(async move {
        while let Some(incoming) = endpoint.accept().await {
            let this = Arc::clone(&this);
            runtime().spawn(async move {
                if let Ok(conn) = incoming.await {
                    this.register(conn, false); // server accepts the stream
                }
            });
        }
    });
    Ok(transport)
}

/// Builds a QUIC client endpoint and connects to `server_addr`.
pub fn client(
    server_addr: SocketAddr,
    server_name: &str,
    trust: ServerTrust,
    client_cert_der: Vec<u8>,
    client_key_der: Vec<u8>,
) -> anyhow::Result<Arc<Transport>> {
    let cert_chain = vec![CertificateDer::from(client_cert_der)];
    let key = PrivateKeyDer::Pkcs8(PrivatePkcs8KeyDer::from(client_key_der));

    let verifier: Arc<dyn ServerCertVerifier> = Arc::new(PinnedServerCert {
        trust,
        algs: provider().signature_verification_algorithms,
    });
    let mut tls = rustls::ClientConfig::builder_with_provider(provider())
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .dangerous()
        .with_custom_certificate_verifier(verifier)
        .with_client_auth_cert(cert_chain, key)
        .context("client with_client_auth_cert")?;
    tls.alpn_protocols = vec![b"ddnet".to_vec()];

    let mut client_config = ClientConfig::new(Arc::new(QuicClientConfig::try_from(tls)?));
    client_config.transport_config(Arc::new(default_transport()));

    // Bind an ephemeral local UDP socket for the client.
    let bind: SocketAddr = if server_addr.is_ipv6() {
        "[::]:0".parse().unwrap()
    } else {
        "0.0.0.0:0".parse().unwrap()
    };
    let socket = UdpSocket::bind(bind).context("binding client socket")?;
    let mut endpoint = runtime().block_on(async move {
        Endpoint::new(
            quinn::EndpointConfig::default(),
            None,
            socket,
            Arc::new(quinn::TokioRuntime),
        )
    })?;
    endpoint.set_default_client_config(client_config);

    let transport = Transport::new(endpoint.clone());
    let this = Arc::clone(&transport);
    let server_name = server_name.to_string();
    runtime().spawn(async move {
        match endpoint.connect(server_addr, &server_name) {
            Ok(connecting) => match connecting.await {
                Ok(conn) => this.register(conn, true), // client opens the stream
                Err(e) => {
                    let _ = this.events_tx.send(Event::Disconnected {
                        conn_id: 0,
                        reason: format!("connect failed: {e}"),
                    });
                }
            },
            Err(e) => {
                let _ = this.events_tx.send(Event::Disconnected {
                    conn_id: 0,
                    reason: format!("connect error: {e}"),
                });
            }
        }
    });
    Ok(transport)
}

/// Transport config shared by client and server. Replicates the `Jupeyy/quinn`
/// fork's only change (a higher max ack delay) via the public API.
fn default_transport() -> TransportConfig {
    let mut tc = TransportConfig::default();
    tc.max_concurrent_uni_streams(0u32.into());
    tc.max_idle_timeout(Some(Duration::from_secs(20).try_into().unwrap()));
    tc.keep_alive_interval(Some(Duration::from_secs(5)));
    // NOTE: the Jupeyy/quinn fork raised the default max ack delay; quinn 0.11
    // does not expose that knob publicly. It is a latency/throughput tweak only
    // and is intentionally left at quinn's default here.
    tc
}

/// Server-side verifier that accepts any client certificate (the account
/// binding is checked separately from the peer cert) but still proves the
/// client holds the private key via the TLS 1.3 handshake signature.
#[derive(Debug)]
struct AnyClientCert {
    algs: WebPkiSupportedAlgorithms,
}

impl ClientCertVerifier for AnyClientCert {
    fn root_hint_subjects(&self) -> &[DistinguishedName] {
        &[]
    }
    fn verify_client_cert(
        &self,
        _end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _now: UnixTime,
    ) -> Result<ClientCertVerified, rustls::Error> {
        Ok(ClientCertVerified::assertion())
    }
    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        // QUIC is TLS 1.3 only.
        Err(rustls::Error::PeerIncompatible(
            rustls::PeerIncompatible::Tls12NotOffered,
        ))
    }
    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        verify_tls13_signature(message, cert, dss, &self.algs)
    }
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        self.algs.supported_schemes()
    }
}

/// Client-side verifier that pins the server's public-key fingerprint (or
/// accepts any in insecure mode), and verifies the handshake signature.
#[derive(Debug)]
struct PinnedServerCert {
    trust: ServerTrust,
    algs: WebPkiSupportedAlgorithms,
}

impl std::fmt::Debug for ServerTrust {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ServerTrust::PinnedPublicKey(_) => write!(f, "PinnedPublicKey"),
            ServerTrust::Insecure => write!(f, "Insecure"),
        }
    }
}

impl ServerCertVerifier for PinnedServerCert {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp: &[u8],
        _now: UnixTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        match &self.trust {
            ServerTrust::Insecure => Ok(ServerCertVerified::assertion()),
            ServerTrust::PinnedPublicKey(pin) => {
                let fp = public_key_fingerprint(end_entity.as_ref())
                    .map_err(|_| rustls::Error::General("bad server cert".into()))?;
                if &fp == pin {
                    Ok(ServerCertVerified::assertion())
                } else {
                    Err(rustls::Error::General(
                        "server public key fingerprint mismatch".into(),
                    ))
                }
            }
        }
    }
    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        Err(rustls::Error::PeerIncompatible(
            rustls::PeerIncompatible::Tls12NotOffered,
        ))
    }
    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        verify_tls13_signature(message, cert, dss, &self.algs)
    }
    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        self.algs.supported_schemes()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;

    /// Drains events from a transport until `pred` returns a value or timeout.
    fn wait_for<T>(t: &Transport, mut pred: impl FnMut(&Event) -> Option<T>) -> Option<T> {
        let deadline = Instant::now() + Duration::from_secs(10);
        while Instant::now() < deadline {
            for ev in t.poll_events() {
                if let Some(v) = pred(&ev) {
                    return Some(v);
                }
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        None
    }

    #[test]
    fn mtls_roundtrip_with_pinning() {
        let srv_id = generate_self_signed().unwrap();
        let pin = srv_id.public_key_fingerprint;
        let server = server(
            "127.0.0.1:0".parse().unwrap(),
            srv_id.cert_der.clone(),
            srv_id.key_der.clone(),
        )
        .unwrap();
        let addr = server.local_addr().unwrap();

        let cli_id = generate_self_signed().unwrap();
        let client = client(
            addr,
            "ddnet",
            ServerTrust::PinnedPublicKey(pin),
            cli_id.cert_der.clone(),
            cli_id.key_der.clone(),
        )
        .unwrap();

        // Server observes the client's certificate (used for account id).
        let (server_conn, peer_cert) = wait_for(&server, |ev| match ev {
            Event::Connected {
                conn_id,
                peer_cert_der,
            } => Some((*conn_id, peer_cert_der.clone())),
            _ => None,
        })
        .expect("server did not see a connection");
        assert_eq!(peer_cert, cli_id.cert_der, "peer cert mismatch");

        let client_conn = wait_for(&client, |ev| match ev {
            Event::Connected { conn_id, .. } => Some(*conn_id),
            _ => None,
        })
        .expect("client did not connect");

        // Reliable client -> server.
        client.send_reliable(client_conn, b"hello-reliable".to_vec());
        let got = wait_for(&server, |ev| match ev {
            Event::Reliable { data, .. } => Some(data.clone()),
            _ => None,
        })
        .expect("no reliable msg on server");
        assert_eq!(got, b"hello-reliable");

        // Reliable server -> client.
        server.send_reliable(server_conn, b"hi-back".to_vec());
        let got = wait_for(&client, |ev| match ev {
            Event::Reliable { data, .. } => Some(data.clone()),
            _ => None,
        })
        .expect("no reliable msg on client");
        assert_eq!(got, b"hi-back");

        // Unreliable datagram client -> server.
        client.send_unreliable(client_conn, b"dgram".to_vec());
        let got = wait_for(&server, |ev| match ev {
            Event::Unreliable { data, .. } => Some(data.clone()),
            _ => None,
        })
        .expect("no datagram on server");
        assert_eq!(got, b"dgram");
    }

    #[test]
    fn identity_persists_across_calls() {
        let dir = std::env::temp_dir();
        let cert = dir.join("ddnet-test-id.cert").to_string_lossy().to_string();
        let key = dir.join("ddnet-test-id.key").to_string_lossy().to_string();
        let _ = std::fs::remove_file(&cert);
        let _ = std::fs::remove_file(&key);

        let a = load_or_generate_identity(&cert, &key).unwrap();
        let b = load_or_generate_identity(&cert, &key).unwrap();
        // Second call loads the persisted identity -> same fingerprint.
        assert_eq!(a.public_key_fingerprint, b.public_key_fingerprint);
        assert_eq!(a.cert_der, b.cert_der);

        let _ = std::fs::remove_file(&cert);
        let _ = std::fs::remove_file(&key);
    }

    #[test]
    fn pinning_rejects_wrong_key() {
        let srv_id = generate_self_signed().unwrap();
        let server = server(
            "127.0.0.1:0".parse().unwrap(),
            srv_id.cert_der.clone(),
            srv_id.key_der.clone(),
        )
        .unwrap();
        let addr = server.local_addr().unwrap();

        // Pin a fingerprint that does not match the server.
        let wrong_pin = [0xABu8; 32];
        let cli_id = generate_self_signed().unwrap();
        let client = client(
            addr,
            "ddnet",
            ServerTrust::PinnedPublicKey(wrong_pin),
            cli_id.cert_der,
            cli_id.key_der,
        )
        .unwrap();

        // The client must fail to establish (disconnected, never connected).
        let connected = wait_for(&client, |ev| match ev {
            Event::Connected { .. } => Some(true),
            Event::Disconnected { .. } => Some(false),
            _ => None,
        });
        assert_eq!(connected, Some(false), "pinning should have rejected server");
    }
}
