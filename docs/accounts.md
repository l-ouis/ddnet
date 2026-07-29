# Accounts

DDNet accounts let players prove a persistent identity to game servers,
without a password prompt on every server and without the game server ever
talking to a central service during gameplay. This document describes the
design and the implementation in this repository.

The design is the one agreed on in [#3411](https://github.com/ddnet/ddnet/issues/3411)
(the combination of Jupeyy's and heinrich5991's proposals) and is compatible
with the [ddnet-accounts](https://github.com/ddnet/ddnet-accounts) account
server, which is also used by [ddnet-rs](https://github.com/ddnet/ddnet-rs).

## Overview

There are three parties:

- The **account server** (`ddnet-accounts`, upstream, unmodified): a small
  HTTPS service backed by MySQL. It manages accounts (email one time codes,
  optionally Steam) and signs certificates. `cl_account_server` /
  `sv_account_server` point to it.
- The **client**: owns an ed25519 *session key pair* per account, stored on
  disk. Logging in does not create a password: the user enters their email
  address, receives a one time code, and the client exchanges it for a
  session that the account server remembers.
- The **game server**: identifies connecting clients by the certificate they
  present in the TLS handshake of a QUIC connection.

The central trick: after login, the client periodically asks the account
server to *sign* its session public key (`/sign`). The result is a short
lived (1 hour) X.509 certificate over the session key that carries the
account id in an X.509 extension. The client uses this certificate as its
TLS client certificate when connecting to game servers. The game server
verifies the signature against the account server's public signing keys
(downloaded once from `/certs` and cached, refreshed in the background) and
extracts the account id — no per-connection round trip to the account
server, and the login is bound to the encrypted connection, so it cannot be
forwarded or replayed by a malicious server.

Players without an account are unaffected: the client then presents a
*self signed* certificate over a locally generated key. The game server
falls back to identifying such clients by the sha256 fingerprint of their
public key (a stable pseudonymous identity that can later be upgraded to an
account, since logging in keeps the key).

## Transport: QUIC

Account logins require an encrypted, authenticated connection, which the
legacy UDP protocol cannot provide. This implementation adds QUIC as an
*additional* transport next to the legacy protocol; nothing about 0.6/0.7
changes.

- The game server opens a QUIC endpoint (`sv_quic 1`) on its own UDP port
  (`sv_quic_port`, 0 picks a free port). Its TLS identity is a self signed
  certificate over a persistent ed25519 key (`quic_identity.pem` in the
  server storage). There is no CA: the server advertises the sha256
  fingerprint of its certificate's public key via the server browser
  (`"quic": {"port": ..., "pubkey_sha256": ...}` in the server info JSON,
  which the master server forwards verbatim), and clients pin exactly that
  fingerprint. The trust chain is: HTTPS master server → server info →
  certificate hash → QUIC TLS handshake.
- The client automatically prefers QUIC when the server browser info of the
  target server advertises it and `cl_quic` is enabled (default). If the
  QUIC connection cannot be established, it falls back to the legacy
  transport. `connect_quic <host> <port> <cert-sha256-hex>` connects to
  servers that are not in the browser (e.g. LAN).
- Game traffic maps onto QUIC as follows: vital chunks go over a single
  bidirectional stream with 2 byte length prefixed frames (QUIC provides
  reliability and ordering); non-vital chunks (snapshots, inputs) are sent
  as unreliable QUIC datagrams. Connless packets (server info, master
  pings) stay on the legacy UDP socket.
- Dummy connects open a second QUIC connection with the same certificate.

The QUIC/TLS implementation is quinn + rustls in the Rust crate described
below, not a new C++ TLS stack.

## Code layout

### Rust crate `src/accounts` (`ddnet-accounts-bridge`)

Wraps the upstream `ddnet-account-*` crates and quinn behind a poll based
API. C++ talks to it through the cxx bridge in `src/accounts/bridge.rs`
(generated glue: `src/rust-bridge/accounts/`, regenerate with
`scripts/generate_rust_bridge.py`). All operations are asynchronous on an
internal tokio runtime; C++ polls completion events once per frame/tick, so
no callbacks cross the FFI boundary.

- `quic.rs`: client and server QUIC endpoints (mTLS, fingerprint pinning,
  stream/datagram chunk mapping, bounded queues; a send queue overflow
  closes the connection).
- `client.rs`: wraps the upstream `Profiles` manager, which owns the
  session keys and certificate cache on disk (`accounts/` in the client
  storage; `profiles.json`, one `acc_<account id>/` directory per account,
  `accountless_keys_and_cert.json` for anonymous play). Exposes every
  account operation (email/steam token requests, login, logout, logout
  everywhere, link/unlink credential, delete, account info) plus
  `cert_and_key` for connecting. The account server URL must be https
  (loopback exempt, for local testing), because the certificates downloaded
  from it are a trust root.
- `game_server.rs`: downloads and caches the account server signing
  certificates, resolves `user_id_from_cert` (rejecting expired account
  certificates), and auto-registers accounts in a per-server sqlite
  database (`accounts.sqlite` in the server storage).
- `identity.rs`: the persistent server TLS identity.

### C++ engine

- `src/engine/shared/network_quic.{h,cpp}`: `CQuicNetServer` /
  `CQuicNetClient`, thin translations of the bridge endpoints to
  DDNet types (`NETADDR`, chunk flags).
- `src/engine/client/accounts.{h,cpp}`: `CAccountsManager`, the engine side
  account manager (implements the `IAccounts` interface from
  `src/engine/accounts.h` that the game code uses). It also owns the
  certificate used for connecting and refreshes it in the background while
  playing.
- `src/engine/client/client.cpp`: transport selection on connect (server
  browser lookup → request certificate → QUIC connect → UDP fallback),
  chunk routing per connection, connection state unification
  (`NetState`/`NetErrorString`).
- `src/engine/server/server.cpp`: `InitQuic`/`UpdateQuic`. QUIC clients get
  a regular client slot (reserved in `CNetServer` so the legacy transport
  does not reuse it), go through ban checks and the per-IP limit, and then
  through the normal `NewClientCallback`/`ProcessClientPacket`/
  `DelClientCallback` paths. The account resolves asynchronously shortly
  after connect onto `CServer::CClient::m_AccountId`, readable via
  `IServer::ClientAccountId()` (0 = not logged in). Binding the account id
  into the ranks database is a follow-up.

### Account UI

`src/game/client/components/menus_account.cpp` implements an **Account**
page reachable from the start menu (the button is highlighted when logged
in). It offers:

- Email login: enter the address → receive a one time code by email →
  enter the code. No password, no registration step: an unknown email
  address creates the account on first login.
- Multiple profiles (one per account), switching the active profile.
- Account info (account id, creation date, linked credentials) and the
  account wide operations: log out, log out everywhere else, link/unlink an
  email address, delete the account. Operations on the account itself are
  confirmed with a one time code as well.
- Web validation: if the account server demands a browser captcha
  (anti-spam), the UI offers to open the link.

Steam login is prepared in the bridge and the engine interface
(`RequestCredentialAuthSteamToken` etc.) but not reachable from the UI yet,
because the Steam integration does not expose auth session tickets.

## Config

| Variable | Default | Meaning |
| --- | --- | --- |
| `cl_account_server` | `https://pg.ddnet.org:5555/` | Account server URL of the client (read at startup) |
| `cl_quic` | `1` | Prefer QUIC when the server advertises it |
| `sv_quic` | `0` | Open the QUIC endpoint |
| `sv_quic_port` | `0` | QUIC UDP port, 0 = pick a free one |
| `sv_account_server` | `https://pg.ddnet.org:5555/` | Account server whose certificates the game server accepts, empty disables account resolution |

## Security notes

- The login is bound to the TLS session; a malicious game server cannot
  replay or forward it, and it never sees a password or long lived secret —
  only the short lived certificate.
- The game server never learns the email address; the account server never
  learns which game servers a player joins (except for cert download
  patterns of servers).
- The client pins the exact server certificate fingerprint from the
  (HTTPS delivered) server browser info, so QUIC connections cannot be
  intercepted even without a CA.
- Certificate lifetime is 1 hour, expired account certificates degrade to
  the anonymous key identity on the game server.
- Per-IP connection limits and bans apply to QUIC clients using their real
  remote address. A mixed transport client counts against the same limit,
  checked across both transports on the QUIC accept path.

## Testing

- `cargo test -p ddnet-accounts-bridge`: transport unit tests (chunk
  roundtrips, datagram delivery, pinning failure, disconnect reasons,
  identity persistence).
- `testrunner --gtest_filter='NetworkQuic.*'`: the same through the C++
  wrappers and generated glue.
- Live end to end tests (`cargo test -p ddnet-accounts-bridge -- --ignored`)
  run against a locally running account server and need:
  - MariaDB and [mailpit](https://github.com/axllent/mailpit) (receives the
    token emails, queried via its HTTP API),
  - a TLS wrapper in front of mailpit's SMTP port (e.g.
    `socat OPENSSL-LISTEN:4465,cert=...,verify=0,fork TCP:127.0.0.1:1025`),
    because the account server requires TLS SMTP; run the account server
    with `SSL_CERT_FILE` pointing at the self signed certificate,
  - the upstream `account-server` binary with a `settings.json` pointing at
    both.
  `e2e_live` exercises token → login → sign → game server resolution →
  account info → logout; `e2e_cpp_server_login` connects to a running
  `DDNet-Server` with `sv_quic 1` over QUIC as a logged in account.
