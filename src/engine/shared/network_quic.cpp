#include "network_quic.h"

#include "network.h"

#include <base/str.h>

#include <string>

void CQuicEvent::FromBridge(const accounts::SQuicEvent &Event)
{
	*this = CQuicEvent();
	switch(Event.m_Kind)
	{
	case accounts::EQuicEventKind::CONNECTED:
		m_Type = EType::CONNECTED;
		break;
	case accounts::EQuicEventKind::CHUNK:
		m_Type = EType::CHUNK;
		break;
	case accounts::EQuicEventKind::DISCONNECTED:
		m_Type = EType::DISCONNECTED;
		break;
	default:
		m_Type = EType::NONE;
		return;
	}
	m_PeerId = Event.m_PeerId;
	if(m_Type == EType::CONNECTED)
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		str_copy(aAddr, std::string(Event.m_Addr).c_str());
		if(net_addr_from_str(&m_Addr, aAddr) != 0)
		{
			m_Addr = NETADDR_ZEROED;
		}
		m_vCertDer.assign(Event.m_aCertDer.begin(), Event.m_aCertDer.end());
	}
	else if(m_Type == EType::CHUNK)
	{
		m_vData.assign(Event.m_aData.begin(), Event.m_aData.end());
		m_Unreliable = Event.m_Unreliable;
	}
	else if(m_Type == EType::DISCONNECTED)
	{
		str_copy(m_aReason, std::string(Event.m_Reason).c_str());
		m_Remote = Event.m_Remote;
	}
}

bool CQuicNetServer::Open(const char *pBindAddr, const accounts::SServerIdentity &Identity, int IdleTimeoutMs, int MaxPeers)
{
	Close();
	rust::Box<accounts::CQuicServer> pServer = accounts::CreateQuicServer(
		pBindAddr,
		rust::Slice<const uint8_t>(Identity.m_aCertDer.data(), Identity.m_aCertDer.size()),
		rust::Slice<const uint8_t>(Identity.m_aKeyDer.data(), Identity.m_aKeyDer.size()),
		IdleTimeoutMs,
		MaxPeers);
	const std::string Error = std::string(pServer->Error());
	if(!Error.empty())
	{
		str_copy(m_aErrorString, Error.c_str());
		return false;
	}
	m_aErrorString[0] = '\0';
	m_pServer.emplace(std::move(pServer));
	return true;
}

void CQuicNetServer::Close()
{
	m_pServer.reset();
}

int CQuicNetServer::Port() const
{
	return m_pServer.has_value() ? (*m_pServer)->Port() : 0;
}

bool CQuicNetServer::Recv(CQuicEvent *pEvent)
{
	if(!m_pServer.has_value())
	{
		return false;
	}
	const accounts::SQuicEvent Event = (*m_pServer)->PollEvent();
	if(!Event.m_Valid)
	{
		return false;
	}
	pEvent->FromBridge(Event);
	return pEvent->m_Type != CQuicEvent::EType::NONE;
}

bool CQuicNetServer::Send(uint64_t PeerId, const void *pData, int DataSize, bool Unreliable)
{
	if(!m_pServer.has_value())
	{
		return false;
	}
	return (*m_pServer)->Send(PeerId, rust::Slice<const uint8_t>((const uint8_t *)pData, DataSize), Unreliable);
}

void CQuicNetServer::ClosePeer(uint64_t PeerId, const char *pReason)
{
	if(m_pServer.has_value())
	{
		(*m_pServer)->ClosePeer(PeerId, pReason == nullptr ? "" : pReason);
	}
}

void CQuicNetServer::SetAcceptConnections(bool Accept)
{
	if(m_pServer.has_value())
	{
		(*m_pServer)->SetAcceptConnections(Accept);
	}
}

int CQuicNetServer::Rtt(uint64_t PeerId) const
{
	return m_pServer.has_value() ? (*m_pServer)->RttMillis(PeerId) : 0;
}

void CQuicNetClient::Connect(const char *pAddr, const unsigned char *pServerPubKeyHash, const std::vector<unsigned char> &vCertDer, const std::vector<unsigned char> &vKeyDer, int IdleTimeoutMs)
{
	Disconnect("");
	m_pClient.emplace(accounts::CreateQuicClient(
		pAddr,
		rust::Slice<const uint8_t>(pServerPubKeyHash, 32),
		rust::Slice<const uint8_t>(vCertDer.data(), vCertDer.size()),
		rust::Slice<const uint8_t>(vKeyDer.data(), vKeyDer.size()),
		IdleTimeoutMs));
	m_State = EState::CONNECTING;
	m_aErrorString[0] = '\0';
}

void CQuicNetClient::Disconnect(const char *pReason)
{
	if(m_pClient.has_value())
	{
		(*m_pClient)->Close(pReason == nullptr ? "" : pReason);
		m_pClient.reset();
	}
	m_State = EState::OFFLINE;
}

bool CQuicNetClient::Recv(CQuicEvent *pEvent)
{
	if(!m_pClient.has_value())
	{
		return false;
	}
	const accounts::SQuicEvent Event = (*m_pClient)->PollEvent();
	if(!Event.m_Valid)
	{
		return false;
	}
	pEvent->FromBridge(Event);
	if(pEvent->m_Type == CQuicEvent::EType::CONNECTED)
	{
		m_State = EState::ONLINE;
	}
	else if(pEvent->m_Type == CQuicEvent::EType::DISCONNECTED)
	{
		str_copy(m_aErrorString, pEvent->m_aReason);
		m_State = EState::ERROR;
	}
	return pEvent->m_Type != CQuicEvent::EType::NONE;
}

bool CQuicNetClient::Send(const void *pData, int DataSize, bool Unreliable)
{
	if(!m_pClient.has_value())
	{
		return false;
	}
	return (*m_pClient)->Send(rust::Slice<const uint8_t>((const uint8_t *)pData, DataSize), Unreliable);
}

int64_t CQuicNetClient::MillisSinceReceive() const
{
	return m_pClient.has_value() ? (int64_t)(*m_pClient)->MillisSinceReceive() : 0;
}

int CQuicNetClient::Rtt() const
{
	return m_pClient.has_value() ? (*m_pClient)->RttMillis() : 0;
}
