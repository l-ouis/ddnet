//! Persistent TLS identity of a game server.
//!
//! The server proves its identity solely by possession of this key pair,
//! clients pin the sha256 fingerprint of the public key that the server
//! advertises e.g. via the server browser. There is no CA.

use std::path::Path;

use anyhow::anyhow;
use ed25519_dalek::pkcs8::spki::der::pem::LineEnding;
use ed25519_dalek::pkcs8::{DecodePrivateKey, EncodePrivateKey};
use ed25519_dalek::SigningKey;
use rcgen::{CertificateParams, KeyPair, PKCS_ED25519};
use x509_cert::der::Decode;

/// TLS identity of a game server.
pub struct ServerIdentity {
    /// Self signed certificate in der format.
    pub cert_der: Vec<u8>,
    /// Private key in pkcs8 der format.
    pub key_der: Vec<u8>,
    /// Sha256 fingerprint of the subject public key info of the
    /// certificate. This is what clients pin.
    pub public_key_hash: [u8; 32],
}

/// Loads the server key from `key_path`, generating and persisting a new
/// one if the file does not exist, and creates a long lived self signed
/// certificate for it.
pub fn load_or_generate_identity(key_path: &Path) -> anyhow::Result<ServerIdentity> {
    let key = match std::fs::read_to_string(key_path) {
        Ok(pem) => SigningKey::from_pkcs8_pem(&pem)?,
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => {
            let key = SigningKey::generate(&mut rand::rngs::OsRng);
            let pem = key.to_pkcs8_pem(LineEnding::LF)?;
            match write_secret_file(key_path, pem.as_bytes()) {
                Ok(()) => key,
                Err(err) if err.kind() == std::io::ErrorKind::AlreadyExists => {
                    // Concurrent first run of another server sharing the
                    // storage, use the key of the winner.
                    SigningKey::from_pkcs8_pem(&std::fs::read_to_string(key_path)?)?
                }
                Err(err) => return Err(err.into()),
            }
        }
        Err(err) => return Err(err.into()),
    };

    let key_pem = key.to_pkcs8_pem(LineEnding::LF)?;
    let key_pair = KeyPair::from_pkcs8_pem_and_sign_algo(&key_pem, &PKCS_ED25519)?;
    let mut cert_params = CertificateParams::new(vec!["ddnet".into()])?;
    let now = std::time::SystemTime::now();
    cert_params.not_before = now.into();
    // The certificate carries no trust by itself, clients compare the
    // public key. So it can be very long lived.
    cert_params.not_after = (now + std::time::Duration::from_secs(10 * 365 * 24 * 60 * 60)).into();
    let cert = cert_params.self_signed(&key_pair)?;
    let cert_der = cert.der().to_vec();

    let public_key_hash = x509_cert::Certificate::from_der(&cert_der)?
        .tbs_certificate
        .subject_public_key_info
        .fingerprint_bytes()
        .map_err(|err| anyhow!(err))?;

    Ok(ServerIdentity {
        cert_der,
        key_der: key.to_pkcs8_der()?.as_bytes().to_vec(),
        public_key_hash,
    })
}

/// Writes the key file via a temporary file so a crash cannot leave a
/// truncated key file behind. Fails with `AlreadyExists` if the file was
/// created concurrently, the first writer wins.
fn write_secret_file(path: &Path, data: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    let mut tmp_path = path.as_os_str().to_owned();
    tmp_path.push(format!(".{}.tmp", std::process::id()));
    let tmp_path = Path::new(&tmp_path);
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(tmp_path)?;
    let written = file
        .write_all(data)
        .and_then(|()| file.sync_all())
        .map(|()| drop(file));
    let result = written.and_then(|()| {
        // The hard link fails with AlreadyExists if another server created
        // the file in the meantime. Fall back to a plain rename on file
        // systems without hard links.
        match std::fs::hard_link(tmp_path, path) {
            Ok(()) => Ok(()),
            Err(err) if err.kind() == std::io::ErrorKind::AlreadyExists => Err(err),
            Err(_) => std::fs::rename(tmp_path, path),
        }
    });
    let _ = std::fs::remove_file(tmp_path);
    result
}
