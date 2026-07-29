use std::sync::OnceLock;

use tokio::runtime::Runtime;

/// Shared tokio runtime for all account and QUIC tasks.
///
/// The C++ side drives everything by polling, the runtime only executes the
/// async parts in the background.
pub fn runtime() -> &'static Runtime {
    static RUNTIME: OnceLock<Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("accounts-bridge")
            .enable_all()
            .build()
            .expect("tokio runtime creation cannot fail")
    })
}

/// Installs the ring crypto provider for rustls, if none is installed yet.
pub fn install_crypto_provider() {
    if rustls::crypto::CryptoProvider::get_default().is_none() {
        let _ = rustls::crypto::CryptoProvider::install_default(
            rustls::crypto::ring::default_provider(),
        );
    }
}
