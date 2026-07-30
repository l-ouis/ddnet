#ifndef ENGINE_SHARED_NETWORK_QUIC_H
#define ENGINE_SHARED_NETWORK_QUIC_H

#include <base/net.h>
#include <base/types.h>

#include <accounts/bridge.h>

#include <cstdint>
#include <optional>
#include <vector>

// Time without any packets after which a QUIC connection is dead. QUIC
// sends keep alives, so this only triggers on actual connection loss.
inline constexpr int QUIC_IDLE_TIMEOUT_MS = 30 * 1000;

// Transport event of a QUIC endpoint, translated from the Rust bridge.
class CQuicEvent
{
public:
	enum class EType
	{
		NONE,
		CONNECTED,
		CHUNK,
		DISCONNECTED,
	};

	EType m_Type = EType::NONE;
	uint64_t m_PeerId = 0;
	// CONNECTED
	NETADDR m_Addr = {};
	std::vector<unsigned char> m_vCertDer;
	// CHUNK
	std::vector<unsigned char> m_vData;
	bool m_Unreliable = false;
	// DISCONNECTED
	char m_aReason[256] = "";
	bool m_Remote = false;

	void FromBridge(const accounts::SQuicEvent &Event);
};

// Server side of the QUIC transport. Peers are identified by ids that never
// repeat, the mapping to client slots is up to the user.
class CQuicNetServer
{
	std::optional<rust::Box<accounts::CQuicServer>> m_pServer;
	char m_aErrorString[256] = "";

public:
	bool Open(const char *pBindAddr, const accounts::SServerIdentity &Identity, int IdleTimeoutMs, int MaxPeers);
	void Close();
	bool IsOpen() const { return m_pServer.has_value(); }
	const char *ErrorString() const { return m_aErrorString; }
	int Port() const;

	bool Recv(CQuicEvent *pEvent);
	bool Send(uint64_t PeerId, const void *pData, int DataSize, bool Unreliable);
	void ClosePeer(uint64_t PeerId, const char *pReason);
	int Rtt(uint64_t PeerId) const;
	// Milliseconds since the last stream frame or datagram arrived from
	// the peer, -1 if the peer is unknown. QUIC keep alives do not count,
	// so this is an application level liveness signal.
	int64_t MillisSinceReceive(uint64_t PeerId) const;
};

// Client side of the QUIC transport. The connection to the server uses the
// account certificate of the client, the server certificate is verified
// against the public key hash the server advertised.
class CQuicNetClient
{
public:
	enum class EState
	{
		OFFLINE,
		CONNECTING,
		ONLINE,
		ERROR,
	};

private:
	std::optional<rust::Box<accounts::CQuicClient>> m_pClient;
	EState m_State = EState::OFFLINE;
	char m_aErrorString[256] = "";

public:
	// pBindAddr is the local IP without port to bind to, nullptr/empty for
	// the unspecified address of the target's address family. An invalid or
	// family mismatching bind address fails the connect.
	void Connect(const char *pAddr, const char *pBindAddr, const unsigned char *pServerPubKeyHash, const std::vector<unsigned char> &vCertDer, const std::vector<unsigned char> &vKeyDer, int IdleTimeoutMs);
	void Disconnect(const char *pReason);

	// Also updates the connection state on CONNECTED/DISCONNECTED events.
	bool Recv(CQuicEvent *pEvent);
	bool Send(const void *pData, int DataSize, bool Unreliable);

	EState State() const { return m_State; }
	const char *ErrorString() const { return m_aErrorString; }
	// Milliseconds since the last time data arrived from the server.
	int64_t MillisSinceReceive() const;
	int Rtt() const;
};

#endif
