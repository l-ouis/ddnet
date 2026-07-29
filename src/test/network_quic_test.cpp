#include "test.h"

#include <base/fs.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/shared/network_quic.h>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

static bool WaitForEvent(std::function<bool(CQuicEvent *)> Recv, CQuicEvent *pEvent)
{
	const auto Start = std::chrono::steady_clock::now();
	while(std::chrono::steady_clock::now() - Start < std::chrono::seconds(10))
	{
		if(Recv(pEvent))
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return false;
}

class NetworkQuic : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	accounts::SServerIdentity m_ServerIdentity;
	accounts::SServerIdentity m_ClientIdentity;
	CQuicNetServer m_Server;
	CQuicNetClient m_Client;

	void SetUp() override
	{
		char aKeyPath[IO_MAX_PATH_LENGTH];
		str_format(aKeyPath, sizeof(aKeyPath), "%s_server_key.pem", m_Info.m_aFilename);
		m_ServerIdentity = accounts::LoadOrGenerateServerIdentity(aKeyPath);
		ASSERT_TRUE(m_ServerIdentity.m_Error.empty()) << std::string(m_ServerIdentity.m_Error);
		str_format(aKeyPath, sizeof(aKeyPath), "%s_client_key.pem", m_Info.m_aFilename);
		m_ClientIdentity = accounts::LoadOrGenerateServerIdentity(aKeyPath);
		ASSERT_TRUE(m_ClientIdentity.m_Error.empty()) << std::string(m_ClientIdentity.m_Error);

		ASSERT_TRUE(m_Server.Open("127.0.0.1:0", m_ServerIdentity, 5000, 16)) << m_Server.ErrorString();
	}

	void TearDown() override
	{
		m_Client.Disconnect("");
		m_Server.Close();
		char aKeyPath[IO_MAX_PATH_LENGTH];
		str_format(aKeyPath, sizeof(aKeyPath), "%s_server_key.pem", m_Info.m_aFilename);
		EXPECT_FALSE(fs_remove(aKeyPath));
		str_format(aKeyPath, sizeof(aKeyPath), "%s_client_key.pem", m_Info.m_aFilename);
		EXPECT_FALSE(fs_remove(aKeyPath));
	}

	// Connects the client to the server and returns the peer id of the
	// client on the server side.
	uint64_t ConnectClient()
	{
		char aAddr[32];
		str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", m_Server.Port());
		std::vector<unsigned char> vCertDer(m_ClientIdentity.m_aCertDer.begin(), m_ClientIdentity.m_aCertDer.end());
		std::vector<unsigned char> vKeyDer(m_ClientIdentity.m_aKeyDer.begin(), m_ClientIdentity.m_aKeyDer.end());
		m_Client.Connect(aAddr, m_ServerIdentity.m_aPublicKeyHash.data(), vCertDer, vKeyDer, 5000);

		CQuicEvent Event;
		EXPECT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Client.Recv(pEvent); }, &Event));
		EXPECT_EQ(Event.m_Type, CQuicEvent::EType::CONNECTED);
		EXPECT_EQ(m_Client.State(), CQuicNetClient::EState::ONLINE);

		EXPECT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Server.Recv(pEvent); }, &Event));
		EXPECT_EQ(Event.m_Type, CQuicEvent::EType::CONNECTED);
		EXPECT_EQ(Event.m_vCertDer.size(), m_ClientIdentity.m_aCertDer.size());
		NETADDR Localhost;
		net_addr_from_str(&Localhost, "127.0.0.1");
		EXPECT_EQ(net_addr_comp_noport(&Event.m_Addr, &Localhost), 0);
		return Event.m_PeerId;
	}
};

TEST_F(NetworkQuic, ChunkRoundtrip)
{
	const uint64_t PeerId = ConnectClient();

	const unsigned char aChunk[] = "hello over quic";
	ASSERT_TRUE(m_Client.Send(aChunk, sizeof(aChunk), false));
	CQuicEvent Event;
	ASSERT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Server.Recv(pEvent); }, &Event));
	ASSERT_EQ(Event.m_Type, CQuicEvent::EType::CHUNK);
	ASSERT_EQ(Event.m_PeerId, PeerId);
	ASSERT_EQ(Event.m_vData.size(), sizeof(aChunk));
	ASSERT_TRUE(mem_comp(Event.m_vData.data(), aChunk, sizeof(aChunk)) == 0);
	ASSERT_FALSE(Event.m_Unreliable);

	const unsigned char aReply[] = "welcome";
	ASSERT_TRUE(m_Server.Send(PeerId, aReply, sizeof(aReply), false));
	ASSERT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Client.Recv(pEvent); }, &Event));
	ASSERT_EQ(Event.m_Type, CQuicEvent::EType::CHUNK);
	ASSERT_EQ(Event.m_vData.size(), sizeof(aReply));
	ASSERT_TRUE(mem_comp(Event.m_vData.data(), aReply, sizeof(aReply)) == 0);
}

TEST_F(NetworkQuic, ServerDropReasonReachesClient)
{
	const uint64_t PeerId = ConnectClient();

	m_Server.ClosePeer(PeerId, "you shall not pass");
	CQuicEvent Event;
	ASSERT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Client.Recv(pEvent); }, &Event));
	ASSERT_EQ(Event.m_Type, CQuicEvent::EType::DISCONNECTED);
	ASSERT_STREQ(Event.m_aReason, "you shall not pass");
	ASSERT_TRUE(Event.m_Remote);
	ASSERT_EQ(m_Client.State(), CQuicNetClient::EState::ERROR);
	ASSERT_STREQ(m_Client.ErrorString(), "you shall not pass");
}

TEST_F(NetworkQuic, WrongServerKeyHashFails)
{
	char aAddr[32];
	str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", m_Server.Port());
	unsigned char aWrongHash[32];
	std::fill(std::begin(aWrongHash), std::end(aWrongHash), 0x42);
	std::vector<unsigned char> vCertDer(m_ClientIdentity.m_aCertDer.begin(), m_ClientIdentity.m_aCertDer.end());
	std::vector<unsigned char> vKeyDer(m_ClientIdentity.m_aKeyDer.begin(), m_ClientIdentity.m_aKeyDer.end());
	m_Client.Connect(aAddr, aWrongHash, vCertDer, vKeyDer, 5000);

	CQuicEvent Event;
	ASSERT_TRUE(WaitForEvent([&](CQuicEvent *pEvent) { return m_Client.Recv(pEvent); }, &Event));
	ASSERT_EQ(Event.m_Type, CQuicEvent::EType::DISCONNECTED);
	ASSERT_EQ(m_Client.State(), CQuicNetClient::EState::ERROR);
}

TEST_F(NetworkQuic, IdentityIsPersistent)
{
	char aKeyPath[IO_MAX_PATH_LENGTH];
	str_format(aKeyPath, sizeof(aKeyPath), "%s_server_key.pem", m_Info.m_aFilename);
	const accounts::SServerIdentity Reloaded = accounts::LoadOrGenerateServerIdentity(aKeyPath);
	ASSERT_TRUE(Reloaded.m_Error.empty()) << std::string(Reloaded.m_Error);
	ASSERT_EQ(Reloaded.m_aPublicKeyHash.size(), m_ServerIdentity.m_aPublicKeyHash.size());
	ASSERT_TRUE(std::equal(Reloaded.m_aPublicKeyHash.begin(), Reloaded.m_aPublicKeyHash.end(), m_ServerIdentity.m_aPublicKeyHash.begin()));
}
