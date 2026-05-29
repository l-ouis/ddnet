#ifndef ENGINE_SHARED_NETWORK_QUIC_H
#define ENGINE_SHARED_NETWORK_QUIC_H

#include "network.h"

// The cxx-bridged Rust QUIC transport. Included (rather than forward-declared)
// because std::optional<rust::Box<QuicTransport>> below requires a complete
// type under libstdc++.
#include <cpp/accounts.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// QUIC-backed server transport. Presents the same higher-level contract as
// CNetServer (slot-indexed clients, NewClient/DelClient callbacks, CNetChunk
// Send/Recv) but runs over the mutually-authenticated QUIC transport. The
// client's certificate (used to resolve its account) is captured on connect
// and exposed via PeerCert().
class CQuicServer
{
	struct CSlot
	{
		bool m_Used = false;
		uint64_t m_ConnId = 0;
		std::vector<unsigned char> m_PeerCert;
	};

	struct CRecvChunk
	{
		int m_ClientId;
		int m_Flags;
		std::vector<unsigned char> m_Data;
	};

	std::optional<rust::Box<QuicTransport>> m_pTransport;
	std::string m_Error;
	CSlot m_aSlots[NET_MAX_CLIENTS];
	std::unordered_map<uint64_t, int> m_ConnToSlot;
	std::deque<CRecvChunk> m_RecvQueue;
	std::vector<unsigned char> m_RecvData; // backing storage for the last Recv

	NETFUNC_NEWCLIENT m_pfnNewClient = nullptr;
	NETFUNC_DELCLIENT m_pfnDelClient = nullptr;
	void *m_pUser = nullptr;

	int AllocSlot(uint64_t ConnId, std::vector<unsigned char> &&PeerCert);
	void FreeSlot(int ClientId);

public:
	CQuicServer();
	~CQuicServer();

	// Binds and starts listening with the given self-signed identity. Returns
	// false on error (see Error()).
	bool Open(const NETADDR &BindAddr, const unsigned char *pCertDer, int CertSize, const unsigned char *pKeyDer, int KeySize);
	void Close();
	void SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser);

	// Drains transport events: fires NewClient/DelClient and queues data.
	void Update();
	// Pops one received data chunk (returns 1) or 0 if none are queued.
	int Recv(CNetChunk *pChunk);
	// Sends a chunk to its client (NETSENDFLAG_VITAL -> reliable, else datagram).
	int Send(const CNetChunk *pChunk);
	void Drop(int ClientId, const char *pReason);

	bool IsActive() const { return m_pTransport.has_value() && m_Error.empty(); }
	const char *Error() const { return m_Error.c_str(); }
	uint16_t LocalPort() const;
	// The connecting client's leaf certificate (DER), for account resolution.
	const std::vector<unsigned char> *PeerCert(int ClientId) const;
};

// QUIC-backed client transport (single connection).
class CQuicClient
{
public:
	enum EState
	{
		STATE_OFFLINE = 0,
		STATE_CONNECTING,
		STATE_ONLINE,
		STATE_ERROR,
	};

private:
	struct CRecvChunk
	{
		int m_Flags;
		std::vector<unsigned char> m_Data;
	};

	std::optional<rust::Box<QuicTransport>> m_pTransport;
	std::string m_Error;
	EState m_State = STATE_OFFLINE;
	bool m_HaveConn = false;
	uint64_t m_ConnId = 0;
	std::vector<unsigned char> m_ServerCert;
	std::deque<CRecvChunk> m_RecvQueue;
	std::vector<unsigned char> m_RecvData;

public:
	CQuicClient();
	~CQuicClient();

	// Connects to the server. If PinSize == 32 the server's public-key
	// fingerprint is pinned; if PinSize == 0 the server cert is accepted
	// unconditionally (insecure). Presents the given client identity.
	bool Connect(const NETADDR &ServerAddr, const unsigned char *pPin, int PinSize, const unsigned char *pCertDer, int CertSize, const unsigned char *pKeyDer, int KeySize);
	void Disconnect();

	void Update();
	int Recv(CNetChunk *pChunk);
	int Send(const CNetChunk *pChunk);

	EState State() const { return m_State; }
	const char *Error() const { return m_Error.c_str(); }
	const std::vector<unsigned char> &ServerCert() const { return m_ServerCert; }
};

#endif // ENGINE_SHARED_NETWORK_QUIC_H
