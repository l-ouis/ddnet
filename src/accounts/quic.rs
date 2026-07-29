//! QUIC transport for the game protocol.
//!
//! One QUIC connection per client. Vital chunks are sent over a single
//! bidirectional stream with length prefixed frames, non-vital chunks are
//! sent as QUIC datagrams. The TLS layer carries the account certificate of
//! the client, the server certificate is verified against a public key hash
//! that the server advertises (e.g. via the server browser).

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::anyhow;
use parking_lot::Mutex;
use quinn::crypto::rustls::{QuicClientConfig, QuicServerConfig};
use quinn::{Connection, Endpoint, IdleTimeout, RecvStream, SendStream, TransportConfig, VarInt};
use rustls::pki_types::{CertificateDer, PrivateKeyDer, UnixTime};

use crate::runtime::{install_crypto_provider, runtime};

/// Maximum size of a single chunk in a frame. Larger than the maximum
/// payload of the legacy protocol, so every game chunk fits.
pub const MAX_CHUNK_SIZE: usize = 8 * 1024;
/// How many chunks may queue up in each direction before the connection is
/// considered broken.
const CHUNK_QUEUE_SIZE: usize = 1024;
/// Close code for user initiated disconnects.
const CLOSE_CODE: u32 = 0;
/// Close code for protocol errors (oversized frames, queue overflow).
const CLOSE_CODE_PROTOCOL_ERROR: u32 = 1;

/// A transport event, polled by the C++ side.
#[derive(Debug)]
pub enum Event {
    /// The connection to the peer was fully established.
    Connected {
        /// Id of the peer.
        peer: u64,
        /// Remote address of the peer.
        addr: SocketAddr,
        /// Certificate the peer presented during the TLS handshake, in
        /// der format. Empty on the client side.
        cert_der: Vec<u8>,
    },
    /// A chunk of game data arrived.
    Chunk {
        /// Id of the peer.
        peer: u64,
        /// The chunk payload.
        data: Vec<u8>,
        /// Whether the chunk was sent as unreliable datagram.
        unreliable: bool,
    },
    /// The connection to the peer is gone.
    Disconnected {
        /// Id of the peer.
        peer: u64,
        /// Disconnect reason.
        reason: String,
        /// Whether the peer or the network caused the disconnect,
        /// rather than the local side.
        remote: bool,
    },
}

struct Peer {
    connection: Connection,
    reliable_tx: tokio::sync::mpsc::Sender<Vec<u8>>,
}

#[derive(Default)]
struct PeerTable {
    peers: HashMap<u64, Peer>,
    next_peer: u64,
}

struct Shared {
    event_tx: tokio::sync::mpsc::Sender<Event>,
    peers: Mutex<PeerTable>,
    last_receive: AtomicU64,
    start: Instant,
}

impl Shared {
    fn new(event_tx: tokio::sync::mpsc::Sender<Event>) -> Arc<Self> {
        Arc::new(Self {
            event_tx,
            peers: Mutex::new(PeerTable::default()),
            last_receive: AtomicU64::new(0),
            start: Instant::now(),
        })
    }

    fn touch_receive_time(&self) {
        self.last_receive
            .store(self.start.elapsed().as_millis() as u64, Ordering::Relaxed);
    }

    fn millis_since_receive(&self) -> u64 {
        (self.start.elapsed().as_millis() as u64)
            .saturating_sub(self.last_receive.load(Ordering::Relaxed))
    }
}

fn transport_config(idle_timeout: Duration) -> TransportConfig {
    let mut config = TransportConfig::default();
    config.keep_alive_interval(Some(Duration::from_secs(1)));
    config.max_idle_timeout(IdleTimeout::try_from(idle_timeout).ok());
    config
}

async fn write_frame(stream: &mut SendStream, data: &[u8]) -> anyhow::Result<()> {
    let len = u16::try_from(data.len())?;
    stream.write_all(&len.to_le_bytes()).await?;
    stream.write_all(data).await?;
    Ok(())
}

/// A frame read that did not yield a chunk.
enum ReadFrameEnd {
    /// The stream or connection is done, no protocol violation.
    Closed,
    /// The peer violated the framing protocol.
    ProtocolError,
}

async fn read_frame(stream: &mut RecvStream) -> Result<Vec<u8>, ReadFrameEnd> {
    let mut len_bytes = [0; 2];
    match stream.read_exact(&mut len_bytes).await {
        Ok(()) => {}
        Err(quinn::ReadExactError::FinishedEarly(_)) => return Err(ReadFrameEnd::Closed),
        Err(quinn::ReadExactError::ReadError(_)) => return Err(ReadFrameEnd::Closed),
    }
    let len = u16::from_le_bytes(len_bytes) as usize;
    if len > MAX_CHUNK_SIZE {
        return Err(ReadFrameEnd::ProtocolError);
    }
    let mut data = vec![0; len];
    match stream.read_exact(&mut data).await {
        Ok(()) => Ok(data),
        Err(_) => Err(ReadFrameEnd::Closed),
    }
}

fn disconnect_reason(err: &quinn::ConnectionError) -> (String, bool) {
    match err {
        quinn::ConnectionError::ApplicationClosed(closed) => {
            (String::from_utf8_lossy(&closed.reason).into_owned(), true)
        }
        quinn::ConnectionError::LocallyClosed => (String::new(), false),
        quinn::ConnectionError::TimedOut => ("Timeout".to_owned(), true),
        err => (err.to_string(), true),
    }
}

/// Runs reading of stream frames and datagrams of an established connection
/// and forwards outgoing reliable chunks. Returns when the connection died.
async fn run_connection(
    shared: Arc<Shared>,
    peer: u64,
    connection: Connection,
    mut send_stream: SendStream,
    mut recv_stream: RecvStream,
    mut reliable_rx: tokio::sync::mpsc::Receiver<Vec<u8>>,
) {
    let stream_shared = shared.clone();
    let stream_connection = connection.clone();
    let stream_read = async move {
        loop {
            match read_frame(&mut recv_stream).await {
                Ok(data) => {
                    if data.is_empty() {
                        // Hello frame, only sent to open the stream.
                        continue;
                    }
                    stream_shared.touch_receive_time();
                    if stream_shared
                        .event_tx
                        .send(Event::Chunk {
                            peer,
                            data,
                            unreliable: false,
                        })
                        .await
                        .is_err()
                    {
                        break;
                    }
                }
                Err(ReadFrameEnd::Closed) => break,
                Err(ReadFrameEnd::ProtocolError) => {
                    stream_connection.close(
                        VarInt::from_u32(CLOSE_CODE_PROTOCOL_ERROR),
                        b"invalid frame",
                    );
                    break;
                }
            }
        }
    };
    let datagram_shared = shared.clone();
    let datagram_connection = connection.clone();
    let datagram_read = async move {
        while let Ok(data) = datagram_connection.read_datagram().await {
            datagram_shared.touch_receive_time();
            // Drop unreliable chunks if the consumer cannot keep up.
            let _ = datagram_shared.event_tx.try_send(Event::Chunk {
                peer,
                data: data.to_vec(),
                unreliable: true,
            });
        }
    };
    let reliable_write = async move {
        while let Some(data) = reliable_rx.recv().await {
            if write_frame(&mut send_stream, &data).await.is_err() {
                break;
            }
        }
    };
    let close_connection = connection.clone();
    let closed = connection.closed();
    let (reason, remote) = tokio::select! {
        err = closed => disconnect_reason(&err),
        fallback = async {
            tokio::select! {
                () = stream_read => ("Stream closed".to_owned(), true),
                () = datagram_read => ("Connection closed".to_owned(), true),
                () = reliable_write => ("Send queue closed".to_owned(), false),
            }
        } => {
            // A dying stream usually means the connection is closing,
            // prefer the close reason of the connection if one arrives.
            match tokio::time::timeout(Duration::from_secs(1), close_connection.closed()).await {
                Ok(err) => disconnect_reason(&err),
                Err(_) => fallback,
            }
        },
    };
    shared.peers.lock().peers.remove(&peer);
    let _ = shared
        .event_tx
        .send(Event::Disconnected {
            peer,
            reason,
            remote,
        })
        .await;
}

fn register_peer(
    shared: &Shared,
    connection: &Connection,
) -> (u64, tokio::sync::mpsc::Receiver<Vec<u8>>) {
    let (reliable_tx, reliable_rx) = tokio::sync::mpsc::channel(CHUNK_QUEUE_SIZE);
    let mut table = shared.peers.lock();
    let peer = table.next_peer;
    table.next_peer += 1;
    table.peers.insert(
        peer,
        Peer {
            connection: connection.clone(),
            reliable_tx,
        },
    );
    (peer, reliable_rx)
}

/// Common send/close operations on an established endpoint.
struct Transport {
    shared: Arc<Shared>,
    event_rx: tokio::sync::mpsc::Receiver<Event>,
}

impl Transport {
    fn new() -> (Self, tokio::sync::mpsc::Sender<Event>) {
        let (event_tx, event_rx) = tokio::sync::mpsc::channel(CHUNK_QUEUE_SIZE);
        (
            Self {
                shared: Shared::new(event_tx.clone()),
                event_rx,
            },
            event_tx,
        )
    }

    fn poll_event(&mut self) -> Option<Event> {
        self.event_rx.try_recv().ok()
    }

    fn send(&self, peer: u64, data: &[u8], unreliable: bool) -> bool {
        if data.len() > MAX_CHUNK_SIZE {
            return false;
        }
        let table = self.shared.peers.lock();
        let Some(entry) = table.peers.get(&peer) else {
            return false;
        };
        if unreliable {
            // Datagrams that do not fit or cannot be sent right now are
            // dropped, like unreliable chunks on the legacy transport.
            let _ = entry.connection.send_datagram(data.to_vec().into());
            return true;
        }
        if entry.reliable_tx.try_send(data.to_vec()).is_err() {
            // The send queue overflowing means the connection is dead or
            // unusably slow, treat it as broken.
            entry.connection.close(
                VarInt::from_u32(CLOSE_CODE_PROTOCOL_ERROR),
                b"send queue overflow",
            );
            return false;
        }
        true
    }

    fn close_peer(&self, peer: u64, reason: &str) {
        let connection = self
            .shared
            .peers
            .lock()
            .peers
            .get(&peer)
            .map(|entry| entry.connection.clone());
        if let Some(connection) = connection {
            connection.close(VarInt::from_u32(CLOSE_CODE), reason.as_bytes());
        }
    }

    fn rtt_millis(&self, peer: u64) -> u64 {
        self.shared
            .peers
            .lock()
            .peers
            .get(&peer)
            .map_or(0, |entry| entry.connection.rtt().as_millis() as u64)
    }
}

/// Verifies the server certificate by comparing the sha256 fingerprint of
/// its subject public key info against a known hash.
#[derive(Debug)]
struct PubKeyHashServerVerifier {
    provider: Arc<rustls::crypto::CryptoProvider>,
    hash: [u8; 32],
}

impl rustls::client::danger::ServerCertVerifier for PubKeyHashServerVerifier {
    fn verify_server_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &rustls::pki_types::ServerName<'_>,
        _ocsp_response: &[u8],
        _now: UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        use x509_cert::der::Decode;
        let hash = x509_cert::Certificate::from_der(end_entity)
            .ok()
            .and_then(|cert| {
                cert.tbs_certificate
                    .subject_public_key_info
                    .fingerprint_bytes()
                    .ok()
            })
            .ok_or(rustls::Error::InvalidCertificate(
                rustls::CertificateError::BadEncoding,
            ))?;
        if hash == self.hash {
            Ok(rustls::client::danger::ServerCertVerified::assertion())
        } else {
            Err(rustls::Error::InvalidCertificate(
                rustls::CertificateError::ApplicationVerificationFailure,
            ))
        }
    }

    fn verify_tls12_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        self.provider
            .signature_verification_algorithms
            .supported_schemes()
    }
}

/// Accepts any syntactically valid x509 client certificate. The certificate
/// is only used to identify the account of the client, its signature is
/// verified separately against the account server keys.
#[derive(Debug)]
struct AcceptAnyClientVerifier {
    provider: Arc<rustls::crypto::CryptoProvider>,
}

impl rustls::server::danger::ClientCertVerifier for AcceptAnyClientVerifier {
    fn root_hint_subjects(&self) -> &[rustls::DistinguishedName] {
        &[]
    }

    fn verify_client_cert(
        &self,
        end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _now: UnixTime,
    ) -> Result<rustls::server::danger::ClientCertVerified, rustls::Error> {
        use x509_cert::der::Decode;
        x509_cert::Certificate::from_der(end_entity)
            .map(|_| rustls::server::danger::ClientCertVerified::assertion())
            .map_err(|_| rustls::Error::InvalidCertificate(rustls::CertificateError::BadEncoding))
    }

    fn verify_tls12_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        cert: &CertificateDer<'_>,
        dss: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            message,
            cert,
            dss,
            &self.provider.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        self.provider
            .signature_verification_algorithms
            .supported_schemes()
    }
}

/// How the client verifies the server certificate.
pub enum ServerVerification {
    /// Compare against the sha256 fingerprint of the subject public key
    /// info of the server certificate.
    PubKeyHash([u8; 32]),
}

/// Client side of the QUIC transport.
pub struct QuicClient {
    transport: Transport,
    endpoint: Arc<Mutex<Option<Endpoint>>>,
}

impl QuicClient {
    /// Creates the client and starts connecting to `addr` in the
    /// background. Progress is reported via [`QuicClient::poll_event`].
    pub fn connect(
        addr: String,
        verification: ServerVerification,
        cert_der: Vec<u8>,
        key_pkcs8_der: Vec<u8>,
        idle_timeout: Duration,
    ) -> Self {
        install_crypto_provider();
        let (transport, event_tx) = Transport::new();
        let shared = transport.shared.clone();
        let endpoint = Arc::new(Mutex::new(None));
        let endpoint_out = endpoint.clone();
        runtime().spawn(async move {
            match Self::connect_impl(
                shared.clone(),
                endpoint_out,
                addr,
                verification,
                cert_der,
                key_pkcs8_der,
                idle_timeout,
            )
            .await
            {
                Ok(()) => {}
                Err(err) => {
                    let _ = event_tx
                        .send(Event::Disconnected {
                            peer: 0,
                            reason: err.to_string(),
                            remote: true,
                        })
                        .await;
                }
            }
        });
        Self {
            transport,
            endpoint,
        }
    }

    async fn connect_impl(
        shared: Arc<Shared>,
        endpoint_out: Arc<Mutex<Option<Endpoint>>>,
        addr: String,
        verification: ServerVerification,
        cert_der: Vec<u8>,
        key_pkcs8_der: Vec<u8>,
        idle_timeout: Duration,
    ) -> anyhow::Result<()> {
        let remote_addr = tokio::net::lookup_host(&addr)
            .await?
            .next()
            .ok_or_else(|| anyhow!("could not resolve address {}", addr))?;
        let provider = Arc::new(rustls::crypto::ring::default_provider());
        let verifier: Arc<dyn rustls::client::danger::ServerCertVerifier> = match verification {
            ServerVerification::PubKeyHash(hash) => Arc::new(PubKeyHashServerVerifier {
                provider: provider.clone(),
                hash,
            }),
        };
        let tls_config = rustls::ClientConfig::builder_with_provider(provider)
            .with_safe_default_protocol_versions()?
            .dangerous()
            .with_custom_certificate_verifier(verifier)
            .with_client_auth_cert(
                vec![CertificateDer::from(cert_der)],
                PrivateKeyDer::try_from(key_pkcs8_der).map_err(|err| anyhow!(err))?,
            )?;
        let mut client_config =
            quinn::ClientConfig::new(Arc::new(QuicClientConfig::try_from(tls_config)?));
        client_config.transport_config(Arc::new(transport_config(idle_timeout)));

        let bind_addr: SocketAddr = if remote_addr.is_ipv4() {
            "0.0.0.0:0".parse().unwrap()
        } else {
            "[::]:0".parse().unwrap()
        };
        let mut endpoint = Endpoint::client(bind_addr)?;
        endpoint.set_default_client_config(client_config);
        *endpoint_out.lock() = Some(endpoint.clone());

        // The server name is irrelevant, verification uses the public key hash.
        let connection = endpoint.connect(remote_addr, "ddnet")?.await?;
        let (mut send_stream, recv_stream) = connection.open_bi().await?;
        // Announce the stream to the server, quinn streams only exist for
        // the peer once data was sent on them.
        write_frame(&mut send_stream, &[]).await?;

        let (peer, reliable_rx) = register_peer(&shared, &connection);
        shared.touch_receive_time();
        let _ = shared
            .event_tx
            .send(Event::Connected {
                peer,
                addr: connection.remote_address(),
                cert_der: Vec::new(),
            })
            .await;
        run_connection(
            shared,
            peer,
            connection,
            send_stream,
            recv_stream,
            reliable_rx,
        )
        .await;
        Ok(())
    }

    /// Polls the next transport event, if any.
    pub fn poll_event(&mut self) -> Option<Event> {
        self.transport.poll_event()
    }

    /// Sends a chunk to the server. Returns false if the chunk could not
    /// even be queued.
    pub fn send(&self, data: &[u8], unreliable: bool) -> bool {
        self.transport.send(0, data, unreliable)
    }

    /// Closes the connection with the given reason.
    pub fn close(&self, reason: &str) {
        self.transport.close_peer(0, reason);
        if let Some(endpoint) = self.endpoint.lock().as_ref() {
            endpoint.close(VarInt::from_u32(CLOSE_CODE), reason.as_bytes());
        }
    }

    /// Milliseconds since the last time data arrived from the server.
    pub fn millis_since_receive(&self) -> u64 {
        self.transport.shared.millis_since_receive()
    }

    /// Current smoothed round trip time to the server, in milliseconds.
    pub fn rtt_millis(&self) -> u64 {
        self.transport.rtt_millis(0)
    }
}

impl Drop for QuicClient {
    fn drop(&mut self) {
        self.close("");
    }
}

/// Server side of the QUIC transport.
pub struct QuicServer {
    transport: Transport,
    endpoint: Endpoint,
    accept_connections: Arc<AtomicBool>,
    error: Option<String>,
}

impl QuicServer {
    /// Opens a QUIC endpoint on `bind_addr` using the given self signed
    /// certificate as TLS identity.
    pub fn new(
        bind_addr: &str,
        cert_der: Vec<u8>,
        key_pkcs8_der: Vec<u8>,
        idle_timeout: Duration,
        max_peers: usize,
    ) -> Self {
        install_crypto_provider();
        let (transport, _) = Transport::new();
        let accept_connections = Arc::new(AtomicBool::new(true));
        match Self::open_endpoint(
            &transport,
            bind_addr,
            cert_der,
            key_pkcs8_der,
            idle_timeout,
            max_peers,
            accept_connections.clone(),
        ) {
            Ok(endpoint) => Self {
                transport,
                endpoint,
                accept_connections,
                error: None,
            },
            Err(err) => Self {
                transport,
                endpoint: Endpoint::client("127.0.0.1:0".parse().unwrap())
                    .expect("local endpoint creation cannot fail"),
                accept_connections,
                error: Some(err.to_string()),
            },
        }
    }

    fn open_endpoint(
        transport: &Transport,
        bind_addr: &str,
        cert_der: Vec<u8>,
        key_pkcs8_der: Vec<u8>,
        idle_timeout: Duration,
        max_peers: usize,
        accept_connections: Arc<AtomicBool>,
    ) -> anyhow::Result<Endpoint> {
        let provider = Arc::new(rustls::crypto::ring::default_provider());
        let client_verifier = Arc::new(AcceptAnyClientVerifier {
            provider: provider.clone(),
        });
        let tls_config = rustls::ServerConfig::builder_with_provider(provider)
            .with_safe_default_protocol_versions()?
            .with_client_cert_verifier(client_verifier)
            .with_single_cert(
                vec![CertificateDer::from(cert_der)],
                PrivateKeyDer::try_from(key_pkcs8_der).map_err(|err| anyhow!(err))?,
            )?;
        let mut server_config =
            quinn::ServerConfig::with_crypto(Arc::new(QuicServerConfig::try_from(tls_config)?));
        server_config.transport_config(Arc::new(transport_config(idle_timeout)));

        let bind_addr: SocketAddr = bind_addr.parse()?;
        let endpoint = {
            let _guard = runtime().enter();
            Endpoint::server(server_config, bind_addr)?
        };

        let shared = transport.shared.clone();
        let accept_endpoint = endpoint.clone();
        runtime().spawn(async move {
            while let Some(incoming) = accept_endpoint.accept().await {
                if !accept_connections.load(Ordering::Relaxed)
                    || shared.peers.lock().peers.len() >= max_peers
                {
                    incoming.refuse();
                    continue;
                }
                let shared = shared.clone();
                runtime().spawn(async move {
                    let Ok(connection) = incoming.await else {
                        return;
                    };
                    let Some(cert_der) = peer_certificate(&connection) else {
                        connection.close(
                            VarInt::from_u32(CLOSE_CODE_PROTOCOL_ERROR),
                            b"client certificate required",
                        );
                        return;
                    };
                    let Ok((send_stream, recv_stream)) = connection.accept_bi().await else {
                        return;
                    };
                    let (peer, reliable_rx) = register_peer(&shared, &connection);
                    shared.touch_receive_time();
                    if shared
                        .event_tx
                        .send(Event::Connected {
                            peer,
                            addr: connection.remote_address(),
                            cert_der,
                        })
                        .await
                        .is_err()
                    {
                        return;
                    }
                    run_connection(
                        shared,
                        peer,
                        connection,
                        send_stream,
                        recv_stream,
                        reliable_rx,
                    )
                    .await;
                });
            }
        });
        Ok(endpoint)
    }

    /// The error that occurred while opening the endpoint, if any.
    pub fn error(&self) -> Option<&str> {
        self.error.as_deref()
    }

    /// The port the endpoint is bound to, 0 on error.
    pub fn port(&self) -> u16 {
        self.endpoint.local_addr().map_or(0, |addr| addr.port())
    }

    /// Whether new connections are currently accepted.
    pub fn set_accept_connections(&self, accept: bool) {
        self.accept_connections.store(accept, Ordering::Relaxed);
    }

    /// Polls the next transport event, if any.
    pub fn poll_event(&mut self) -> Option<Event> {
        self.transport.poll_event()
    }

    /// Sends a chunk to the given peer. Returns false if the chunk could
    /// not even be queued.
    pub fn send(&self, peer: u64, data: &[u8], unreliable: bool) -> bool {
        self.transport.send(peer, data, unreliable)
    }

    /// Closes the connection to the given peer with the given reason.
    pub fn close_peer(&self, peer: u64, reason: &str) {
        self.transport.close_peer(peer, reason);
    }

    /// Current smoothed round trip time to the peer, in milliseconds.
    pub fn rtt_millis(&self, peer: u64) -> u64 {
        self.transport.rtt_millis(peer)
    }
}

impl Drop for QuicServer {
    fn drop(&mut self) {
        self.endpoint
            .close(VarInt::from_u32(CLOSE_CODE), b"shutdown");
    }
}

fn peer_certificate(connection: &Connection) -> Option<Vec<u8>> {
    let identity = connection.peer_identity()?;
    let certs = identity.downcast::<Vec<CertificateDer>>().ok()?;
    certs.first().map(|cert| cert.as_ref().to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::pkcs8::EncodePrivateKey;
    use x509_cert::der::{Decode, Encode};

    const TIMEOUT: Duration = Duration::from_secs(10);

    struct TestIdentity {
        cert_der: Vec<u8>,
        key_der: Vec<u8>,
        public_key_hash: [u8; 32],
    }

    fn test_identity() -> TestIdentity {
        let key = ed25519_dalek::SigningKey::generate(&mut rand::rngs::OsRng);
        let cert = ddnet_accounts_shared::cert::generate_self_signed(&key).unwrap();
        let cert_der = cert.to_der().unwrap();
        let public_key_hash = cert
            .tbs_certificate
            .subject_public_key_info
            .fingerprint_bytes()
            .unwrap();
        TestIdentity {
            cert_der,
            key_der: key.to_pkcs8_der().unwrap().as_bytes().to_vec(),
            public_key_hash,
        }
    }

    fn wait_event(poll: &mut dyn FnMut() -> Option<Event>) -> Event {
        let start = Instant::now();
        loop {
            if let Some(event) = poll() {
                return event;
            }
            if start.elapsed() > TIMEOUT {
                panic!("timeout waiting for transport event");
            }
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    fn connected_pair() -> (QuicServer, QuicClient, u64, TestIdentity) {
        let server_identity = test_identity();
        let client_identity = test_identity();
        let mut server = QuicServer::new(
            "127.0.0.1:0",
            server_identity.cert_der.clone(),
            server_identity.key_der.clone(),
            Duration::from_secs(5),
            16,
        );
        assert!(server.error().is_none(), "{:?}", server.error());
        let mut client = QuicClient::connect(
            format!("127.0.0.1:{}", server.port()),
            ServerVerification::PubKeyHash(server_identity.public_key_hash),
            client_identity.cert_der.clone(),
            client_identity.key_der.clone(),
            Duration::from_secs(5),
        );
        let event = wait_event(&mut || client.poll_event());
        assert!(matches!(event, Event::Connected { .. }));
        let event = wait_event(&mut || server.poll_event());
        let Event::Connected { peer, cert_der, .. } = event else {
            panic!("expected connect, got {:?}", event);
        };
        assert_eq!(cert_der, client_identity.cert_der);
        (server, client, peer, client_identity)
    }

    #[test]
    fn chunk_exchange() {
        let (mut server, mut client, peer, _) = connected_pair();

        assert!(client.send(b"hello server", false));
        let event = wait_event(&mut || server.poll_event());
        let Event::Chunk {
            data, unreliable, ..
        } = event
        else {
            panic!("expected chunk, got {:?}", event);
        };
        assert_eq!(data, b"hello server");
        assert!(!unreliable);

        assert!(server.send(peer, b"hello client", false));
        let event = wait_event(&mut || client.poll_event());
        let Event::Chunk { data, .. } = event else {
            panic!("expected chunk, got {:?}", event);
        };
        assert_eq!(data, b"hello client");
    }

    #[test]
    fn datagram_exchange() {
        let (mut server, client, _, _) = connected_pair();

        // Datagrams are unreliable, resend until one arrives.
        let start = Instant::now();
        loop {
            assert!(client.send(b"unreliable", true));
            match server.poll_event() {
                Some(Event::Chunk {
                    data, unreliable, ..
                }) => {
                    assert_eq!(data, b"unreliable");
                    assert!(unreliable);
                    break;
                }
                Some(event) => panic!("expected chunk, got {:?}", event),
                None => {
                    if start.elapsed() > TIMEOUT {
                        panic!("timeout waiting for datagram");
                    }
                    std::thread::sleep(Duration::from_millis(5));
                }
            }
        }
    }

    #[test]
    fn user_id_of_peer_cert() {
        let (_server, _client, _, client_identity) = connected_pair();
        let user_id = ddnet_accounts_shared::game_server::user_id::user_id_from_cert(
            &[],
            client_identity.cert_der.clone(),
        );
        assert!(user_id.account_id.is_none());
        let expected = x509_cert::Certificate::from_der(&client_identity.cert_der)
            .unwrap()
            .tbs_certificate
            .subject_public_key_info
            .fingerprint_bytes()
            .unwrap();
        assert_eq!(user_id.public_key, expected);
    }

    #[test]
    fn close_reason_reaches_peer() {
        let (mut server, mut client, _, _) = connected_pair();
        client.close("changing server");
        let event = wait_event(&mut || server.poll_event());
        let Event::Disconnected { reason, remote, .. } = event else {
            panic!("expected disconnect, got {:?}", event);
        };
        assert_eq!(reason, "changing server");
        assert!(remote);
        let event = wait_event(&mut || client.poll_event());
        assert!(matches!(event, Event::Disconnected { remote: false, .. }));
    }

    #[test]
    fn wrong_server_key_hash_fails() {
        let server_identity = test_identity();
        let client_identity = test_identity();
        let server = QuicServer::new(
            "127.0.0.1:0",
            server_identity.cert_der.clone(),
            server_identity.key_der.clone(),
            Duration::from_secs(5),
            16,
        );
        let mut client = QuicClient::connect(
            format!("127.0.0.1:{}", server.port()),
            ServerVerification::PubKeyHash([0x11; 32]),
            client_identity.cert_der,
            client_identity.key_der,
            Duration::from_secs(5),
        );
        let event = wait_event(&mut || client.poll_event());
        assert!(matches!(event, Event::Disconnected { .. }));
    }

    #[test]
    fn oversized_chunk_rejected() {
        let (_server, client, _, _) = connected_pair();
        assert!(!client.send(&vec![0; MAX_CHUNK_SIZE + 1], false));
    }
}
