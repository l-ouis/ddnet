#include "network_quic.h"

#include <base/mem.h>
#include <base/net.h>

#include <cpp/accounts.h>

#include <string>
#include <utility>

// Helpers to talk to the cxx bridge.
static rust::Slice<const uint8_t> Slice(const unsigned char *pData, int Size)
{
	return rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(pData), Size < 0 ? 0 : (size_t)Size);
}

// ---------------------------------------------------------------------------
// CQuicServer
// ---------------------------------------------------------------------------

CQuicServer::CQuicServer() = default;
CQuicServer::~CQuicServer() = default;

bool CQuicServer::Open(const NETADDR &BindAddr, const unsigned char *pCertDer, int CertSize, const unsigned char *pKeyDer, int KeySize)
{
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&BindAddr, aAddr, sizeof(aAddr), true);

	rust::Box<QuicTransport> Transport = quic_server(rust::Str(aAddr), Slice(pCertDer, CertSize), Slice(pKeyDer, KeySize));
	std::string Err = std::string(Transport->error());
	if(!Err.empty())
	{
		m_Error = Err;
		return false;
	}
	m_Error.clear();
	m_pTransport.emplace(std::move(Transport));
	return true;
}

void CQuicServer::Close()
{
	m_pTransport.reset();
	m_ConnToSlot.clear();
	m_RecvQueue.clear();
	for(auto &Slot : m_aSlots)
		Slot = CSlot();
}

void CQuicServer::SetCallbacks(NETFUNC_NEWCLIENT pfnNewClient, NETFUNC_DELCLIENT pfnDelClient, void *pUser)
{
	m_pfnNewClient = pfnNewClient;
	m_pfnDelClient = pfnDelClient;
	m_pUser = pUser;
}

int CQuicServer::AllocSlot(uint64_t ConnId, std::vector<unsigned char> &&PeerCert)
{
	for(int i = 0; i < NET_MAX_CLIENTS; i++)
	{
		if(!m_aSlots[i].m_Used)
		{
			m_aSlots[i].m_Used = true;
			m_aSlots[i].m_ConnId = ConnId;
			m_aSlots[i].m_PeerCert = std::move(PeerCert);
			m_ConnToSlot[ConnId] = i;
			return i;
		}
	}
	return -1;
}

void CQuicServer::FreeSlot(int ClientId)
{
	if(ClientId < 0 || ClientId >= NET_MAX_CLIENTS)
		return;
	m_ConnToSlot.erase(m_aSlots[ClientId].m_ConnId);
	m_aSlots[ClientId] = CSlot();
}

void CQuicServer::Update()
{
	if(!m_pTransport)
		return;
	for(const QuicEvent &Event : (*m_pTransport)->poll_events())
	{
		switch(Event.kind)
		{
		case QuicEventKind::Connected:
		{
			std::vector<unsigned char> Cert(Event.data.begin(), Event.data.end());
			int Slot = AllocSlot(Event.conn_id, std::move(Cert));
			if(Slot < 0)
			{
				// Server full: reject the connection.
				(*m_pTransport)->disconnect(Event.conn_id);
				break;
			}
			if(m_pfnNewClient)
				m_pfnNewClient(Slot, m_pUser, false);
			break;
		}
		case QuicEventKind::Disconnected:
		{
			auto It = m_ConnToSlot.find(Event.conn_id);
			if(It != m_ConnToSlot.end())
			{
				int Slot = It->second;
				std::string Reason(Event.data.begin(), Event.data.end());
				if(m_pfnDelClient)
					m_pfnDelClient(Slot, Reason.c_str(), m_pUser);
				FreeSlot(Slot);
			}
			break;
		}
		case QuicEventKind::Reliable:
		case QuicEventKind::Unreliable:
		{
			auto It = m_ConnToSlot.find(Event.conn_id);
			if(It != m_ConnToSlot.end())
			{
				CRecvChunk Chunk;
				Chunk.m_ClientId = It->second;
				Chunk.m_Flags = Event.kind == QuicEventKind::Reliable ? NETSENDFLAG_VITAL : 0;
				Chunk.m_Data.assign(Event.data.begin(), Event.data.end());
				m_RecvQueue.push_back(std::move(Chunk));
			}
			break;
		}
		default:
			break;
		}
	}
}

int CQuicServer::Recv(CNetChunk *pChunk)
{
	if(m_RecvQueue.empty())
		return 0;
	CRecvChunk Chunk = std::move(m_RecvQueue.front());
	m_RecvQueue.pop_front();
	m_RecvData = std::move(Chunk.m_Data);
	mem_zero(pChunk, sizeof(*pChunk));
	pChunk->m_ClientId = Chunk.m_ClientId;
	pChunk->m_Flags = Chunk.m_Flags;
	pChunk->m_DataSize = (int)m_RecvData.size();
	pChunk->m_pData = m_RecvData.data();
	return 1;
}

int CQuicServer::Send(const CNetChunk *pChunk)
{
	if(!m_pTransport)
		return -1;
	if(pChunk->m_ClientId < 0 || pChunk->m_ClientId >= NET_MAX_CLIENTS || !m_aSlots[pChunk->m_ClientId].m_Used)
		return -1;
	uint64_t ConnId = m_aSlots[pChunk->m_ClientId].m_ConnId;
	rust::Slice<const uint8_t> Data = Slice(static_cast<const unsigned char *>(pChunk->m_pData), pChunk->m_DataSize);
	if(pChunk->m_Flags & NETSENDFLAG_VITAL)
		(*m_pTransport)->send_reliable(ConnId, Data);
	else
		(*m_pTransport)->send_unreliable(ConnId, Data);
	return 0;
}

void CQuicServer::Drop(int ClientId, const char *pReason)
{
	if(ClientId < 0 || ClientId >= NET_MAX_CLIENTS || !m_aSlots[ClientId].m_Used)
		return;
	if(m_pTransport)
		(*m_pTransport)->disconnect(m_aSlots[ClientId].m_ConnId);
	if(m_pfnDelClient)
		m_pfnDelClient(ClientId, pReason ? pReason : "", m_pUser);
	FreeSlot(ClientId);
}

uint16_t CQuicServer::LocalPort() const
{
	if(!m_pTransport)
		return 0;
	return (*m_pTransport)->local_port();
}

const std::vector<unsigned char> *CQuicServer::PeerCert(int ClientId) const
{
	if(ClientId < 0 || ClientId >= NET_MAX_CLIENTS || !m_aSlots[ClientId].m_Used)
		return nullptr;
	return &m_aSlots[ClientId].m_PeerCert;
}

// ---------------------------------------------------------------------------
// CQuicClient
// ---------------------------------------------------------------------------

CQuicClient::CQuicClient() = default;
CQuicClient::~CQuicClient() = default;

bool CQuicClient::Connect(const NETADDR &ServerAddr, const unsigned char *pPin, int PinSize, const unsigned char *pCertDer, int CertSize, const unsigned char *pKeyDer, int KeySize)
{
	char aAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&ServerAddr, aAddr, sizeof(aAddr), true);

	rust::Box<QuicTransport> Transport = quic_client(
		rust::Str(aAddr), rust::Str("ddnet"), Slice(pPin, PinSize), Slice(pCertDer, CertSize), Slice(pKeyDer, KeySize));
	std::string Err = std::string(Transport->error());
	if(!Err.empty())
	{
		m_Error = Err;
		m_State = STATE_ERROR;
		return false;
	}
	m_Error.clear();
	m_pTransport.emplace(std::move(Transport));
	m_State = STATE_CONNECTING;
	m_HaveConn = false;
	return true;
}

void CQuicClient::Disconnect()
{
	if(m_pTransport && m_HaveConn)
		(*m_pTransport)->disconnect(m_ConnId);
	m_pTransport.reset();
	m_RecvQueue.clear();
	m_HaveConn = false;
	m_State = STATE_OFFLINE;
}

void CQuicClient::Update()
{
	if(!m_pTransport)
		return;
	for(const QuicEvent &Event : (*m_pTransport)->poll_events())
	{
		switch(Event.kind)
		{
		case QuicEventKind::Connected:
			m_HaveConn = true;
			m_ConnId = Event.conn_id;
			m_ServerCert.assign(Event.data.begin(), Event.data.end());
			m_State = STATE_ONLINE;
			break;
		case QuicEventKind::Disconnected:
			m_Error.assign(Event.data.begin(), Event.data.end());
			m_State = STATE_ERROR;
			m_HaveConn = false;
			break;
		case QuicEventKind::Reliable:
		case QuicEventKind::Unreliable:
			if(m_HaveConn && Event.conn_id == m_ConnId)
			{
				CRecvChunk Chunk;
				Chunk.m_Flags = Event.kind == QuicEventKind::Reliable ? NETSENDFLAG_VITAL : 0;
				Chunk.m_Data.assign(Event.data.begin(), Event.data.end());
				m_RecvQueue.push_back(std::move(Chunk));
			}
			break;
		default:
			break;
		}
	}
}

int CQuicClient::Recv(CNetChunk *pChunk)
{
	if(m_RecvQueue.empty())
		return 0;
	CRecvChunk Chunk = std::move(m_RecvQueue.front());
	m_RecvQueue.pop_front();
	m_RecvData = std::move(Chunk.m_Data);
	mem_zero(pChunk, sizeof(*pChunk));
	pChunk->m_ClientId = 0; // server
	pChunk->m_Flags = Chunk.m_Flags;
	pChunk->m_DataSize = (int)m_RecvData.size();
	pChunk->m_pData = m_RecvData.data();
	return 1;
}

int CQuicClient::Send(const CNetChunk *pChunk)
{
	if(!m_pTransport || !m_HaveConn)
		return -1;
	rust::Slice<const uint8_t> Data = Slice(static_cast<const unsigned char *>(pChunk->m_pData), pChunk->m_DataSize);
	if(pChunk->m_Flags & NETSENDFLAG_VITAL)
		(*m_pTransport)->send_reliable(m_ConnId, Data);
	else
		(*m_pTransport)->send_unreliable(m_ConnId, Data);
	return 0;
}
