//! Accounts and QUIC transport support for the C++ code base.
//!
//! This crate wraps the `ddnet-account-*` crates (the client side of the
//! DDNet account server) and a quinn based QUIC transport into an API that
//! is exposed to C++ via cxx in the [`bridge`] module.
//!
//! The account system is described in `docs/accounts.md`.

#![warn(missing_docs)]

pub mod bridge;
mod client;
#[cfg(test)]
mod e2e_test;
mod game_server;
mod identity;
mod quic;
mod runtime;
