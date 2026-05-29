#include <gtest/gtest.h>

#include <base/mem.h>
#include <base/net.h>

#include <engine/shared/network_quic.h>

#include <cpp/accounts.h>

#include <chrono>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

rust::Slice<const uint8_t> Slice(const rust::Vec<uint8_t> &Vec)
{
	return rust::Slice<const uint8_t>(Vec.data(), Vec.size());
}

// Polls a transport until `Pred` returns true for one of its events, or the
// deadline passes. Returns the matching event (or an empty one on timeout).
template<typename F>
bool WaitForEvent(const QuicTransport &Transport, QuicEvent &Out, F Pred)
{
	auto Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline)
	{
		for(const QuicEvent &Event : Transport.poll_events())
		{
			if(Pred(Event))
			{
				Out = Event;
				return true;
			}
		}
		std::this_thread::sleep_for(10ms);
	}
	return false;
}

bool IsKind(const QuicEvent &Event, QuicEventKind Kind)
{
	return Event.kind == Kind;
}

struct SServerState
{
	int m_NewClient = -1;
	int m_DelClient = -1;
};
int NewClientCb(int ClientId, void *pUser, bool Sixup)
{
	(void)Sixup;
	static_cast<SServerState *>(pUser)->m_NewClient = ClientId;
	return 0;
}
int DelClientCb(int ClientId, const char *pReason, void *pUser)
{
	(void)pReason;
	static_cast<SServerState *>(pUser)->m_DelClient = ClientId;
	return 0;
}

} // namespace

TEST(Quic, MutualTlsRoundtripWithPinning)
{
	QuicCert Server = quic_generate_self_signed();
	ASSERT_TRUE(std::string(Server.error).empty());
	ASSERT_EQ(Server.public_key_fingerprint.size(), 32u);

	rust::Box<QuicTransport> pServer = quic_server("127.0.0.1:0", Slice(Server.cert_der), Slice(Server.key_der));
	ASSERT_TRUE(std::string(pServer->error()).empty());
	uint16_t Port = pServer->local_port();
	ASSERT_NE(Port, 0);

	QuicCert Client = quic_generate_self_signed();
	ASSERT_TRUE(std::string(Client.error).empty());

	std::string Addr = "127.0.0.1:" + std::to_string(Port);
	rust::Box<QuicTransport> pClient = quic_client(
		Addr.c_str(), "ddnet", Slice(Server.public_key_fingerprint), Slice(Client.cert_der), Slice(Client.key_der));
	ASSERT_TRUE(std::string(pClient->error()).empty());

	// The server must observe the client's certificate (used to resolve the
	// account id) on connect.
	QuicEvent ServerConnected;
	ASSERT_TRUE(WaitForEvent(*pServer, ServerConnected, [](const QuicEvent &e) { return IsKind(e, QuicEventKind::Connected); }));
	ASSERT_EQ(ServerConnected.data.size(), Client.cert_der.size());

	QuicEvent ClientConnected;
	ASSERT_TRUE(WaitForEvent(*pClient, ClientConnected, [](const QuicEvent &e) { return IsKind(e, QuicEventKind::Connected); }));

	// Reliable client -> server.
	const char aReliable[] = "hello-reliable";
	pClient->send_reliable(ClientConnected.conn_id, rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(aReliable), sizeof(aReliable) - 1));
	QuicEvent GotReliable;
	ASSERT_TRUE(WaitForEvent(*pServer, GotReliable, [](const QuicEvent &e) { return IsKind(e, QuicEventKind::Reliable); }));
	EXPECT_EQ(std::string(GotReliable.data.begin(), GotReliable.data.end()), "hello-reliable");

	// Unreliable server -> client.
	const char aDgram[] = "dgram";
	pServer->send_unreliable(ServerConnected.conn_id, rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(aDgram), sizeof(aDgram) - 1));
	QuicEvent GotDgram;
	ASSERT_TRUE(WaitForEvent(*pClient, GotDgram, [](const QuicEvent &e) { return IsKind(e, QuicEventKind::Unreliable); }));
	EXPECT_EQ(std::string(GotDgram.data.begin(), GotDgram.data.end()), "dgram");
}

TEST(Quic, PinningRejectsWrongKey)
{
	QuicCert Server = quic_generate_self_signed();
	rust::Box<QuicTransport> pServer = quic_server("127.0.0.1:0", Slice(Server.cert_der), Slice(Server.key_der));
	ASSERT_TRUE(std::string(pServer->error()).empty());
	uint16_t Port = pServer->local_port();

	QuicCert Client = quic_generate_self_signed();
	uint8_t aWrongPin[32];
	mem_zero(aWrongPin, sizeof(aWrongPin));

	std::string Addr = "127.0.0.1:" + std::to_string(Port);
	rust::Box<QuicTransport> pClient = quic_client(
		Addr.c_str(), "ddnet", rust::Slice<const uint8_t>(aWrongPin, sizeof(aWrongPin)), Slice(Client.cert_der), Slice(Client.key_der));
	ASSERT_TRUE(std::string(pClient->error()).empty());

	// The client must fail to establish (a Disconnected event, never Connected).
	QuicEvent Event;
	bool Resolved = WaitForEvent(*pClient, Event, [](const QuicEvent &e) {
		return IsKind(e, QuicEventKind::Connected) || IsKind(e, QuicEventKind::Disconnected);
	});
	ASSERT_TRUE(Resolved);
	EXPECT_EQ(Event.kind, QuicEventKind::Disconnected);
}

TEST(Quic, NetcodeServerClientChunkExchange)
{
	QuicCert ServerCert = quic_generate_self_signed();
	ASSERT_TRUE(std::string(ServerCert.error).empty());

	CQuicServer Server;
	NETADDR BindAddr;
	ASSERT_FALSE(net_addr_from_str(&BindAddr, "127.0.0.1"));
	ASSERT_TRUE(Server.Open(BindAddr, ServerCert.cert_der.data(), (int)ServerCert.cert_der.size(), ServerCert.key_der.data(), (int)ServerCert.key_der.size()));
	SServerState State;
	Server.SetCallbacks(NewClientCb, DelClientCb, &State);
	uint16_t Port = Server.LocalPort();
	ASSERT_NE(Port, 0);

	QuicCert ClientCert = quic_generate_self_signed();
	CQuicClient Client;
	NETADDR ServerAddr = BindAddr;
	ServerAddr.port = Port;
	ASSERT_TRUE(Client.Connect(ServerAddr, ServerCert.public_key_fingerprint.data(), (int)ServerCert.public_key_fingerprint.size(), ClientCert.cert_der.data(), (int)ClientCert.cert_der.size(), ClientCert.key_der.data(), (int)ClientCert.key_der.size()));

	auto Pump = [&]() {
		Server.Update();
		Client.Update();
		std::this_thread::sleep_for(5ms);
	};

	auto Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline && (Client.State() != CQuicClient::STATE_ONLINE || State.m_NewClient < 0))
		Pump();
	ASSERT_EQ(Client.State(), CQuicClient::STATE_ONLINE);
	ASSERT_GE(State.m_NewClient, 0);
	int Slot = State.m_NewClient;

	// The server captured the client's certificate for account resolution.
	const std::vector<unsigned char> *pPeerCert = Server.PeerCert(Slot);
	ASSERT_NE(pPeerCert, nullptr);
	ASSERT_EQ(pPeerCert->size(), ClientCert.cert_der.size());

	// Reliable (VITAL) client -> server.
	const char aVital[] = "vital-from-client";
	CNetChunk Out;
	mem_zero(&Out, sizeof(Out));
	Out.m_Flags = NETSENDFLAG_VITAL;
	Out.m_pData = aVital;
	Out.m_DataSize = sizeof(aVital) - 1;
	ASSERT_EQ(Client.Send(&Out), 0);

	CNetChunk In;
	bool Got = false;
	Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline && !Got)
	{
		Pump();
		if(Server.Recv(&In))
			Got = true;
	}
	ASSERT_TRUE(Got);
	EXPECT_EQ(In.m_ClientId, Slot);
	EXPECT_TRUE(In.m_Flags & NETSENDFLAG_VITAL);
	EXPECT_EQ(std::string((const char *)In.m_pData, In.m_DataSize), "vital-from-client");

	// Unreliable (datagram) server -> client.
	const char aDgram[] = "dgram-from-server";
	CNetChunk Out2;
	mem_zero(&Out2, sizeof(Out2));
	Out2.m_ClientId = Slot;
	Out2.m_Flags = 0;
	Out2.m_pData = aDgram;
	Out2.m_DataSize = sizeof(aDgram) - 1;
	ASSERT_EQ(Server.Send(&Out2), 0);

	CNetChunk In2;
	bool Got2 = false;
	Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline && !Got2)
	{
		Pump();
		if(Client.Recv(&In2))
			Got2 = true;
	}
	ASSERT_TRUE(Got2);
	EXPECT_FALSE(In2.m_Flags & NETSENDFLAG_VITAL);
	EXPECT_EQ(std::string((const char *)In2.m_pData, In2.m_DataSize), "dgram-from-server");

	// Client disconnect is observed by the server.
	Client.Disconnect();
	Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline && State.m_DelClient < 0)
		Server.Update();
	EXPECT_EQ(State.m_DelClient, Slot);
}

TEST(Account, GameServerAccountlessLoginAndCaValidation)
{
	const char *pDbFile = "test-cpp-account-gs.sqlite";
	std::remove(pDbFile);

	rust::Box<AccountGameServer> pAcc = account_game_server_open(pDbFile);
	ASSERT_TRUE(std::string(pAcc->error()).empty());

	// An accountless (self-signed ed25519) certificate yields no account id but
	// a valid 32-byte public-key fingerprint, and creates no account row.
	QuicCert Cert = quic_generate_self_signed();
	ASSERT_TRUE(std::string(Cert.error).empty());
	AccountLogin Login = pAcc->login_by_cert(Slice(Cert.cert_der));
	EXPECT_TRUE(std::string(Login.error).empty());
	EXPECT_EQ(Login.account_id, 0);
	EXPECT_EQ(Login.public_key.size(), 32u);
	EXPECT_FALSE(Login.created);

	// An ed25519 self-signed cert is not a valid (P-256) account-server CA cert.
	EXPECT_FALSE(pAcc->add_ca_cert(Slice(Cert.cert_der)));

	std::remove(pDbFile);
}

// The combined server-side flow CServer will use: a client connects over QUIC,
// the server reads its certificate and resolves it through the account bridge.
TEST(Account, ServerResolvesConnectingQuicClient)
{
	const char *pDbFile = "test-cpp-resolve.sqlite";
	std::remove(pDbFile);
	rust::Box<AccountGameServer> pAcc = account_game_server_open(pDbFile);
	ASSERT_TRUE(std::string(pAcc->error()).empty());

	QuicCert ServerId = quic_generate_self_signed();
	CQuicServer Server;
	NETADDR BindAddr;
	ASSERT_FALSE(net_addr_from_str(&BindAddr, "127.0.0.1"));
	ASSERT_TRUE(Server.Open(BindAddr, ServerId.cert_der.data(), (int)ServerId.cert_der.size(), ServerId.key_der.data(), (int)ServerId.key_der.size()));
	SServerState State;
	Server.SetCallbacks(NewClientCb, DelClientCb, &State);
	uint16_t Port = Server.LocalPort();

	QuicCert ClientId = quic_generate_self_signed();
	CQuicClient Client;
	NETADDR ServerAddr = BindAddr;
	ServerAddr.port = Port;
	ASSERT_TRUE(Client.Connect(ServerAddr, ServerId.public_key_fingerprint.data(), (int)ServerId.public_key_fingerprint.size(), ClientId.cert_der.data(), (int)ClientId.cert_der.size(), ClientId.key_der.data(), (int)ClientId.key_der.size()));

	auto Deadline = std::chrono::steady_clock::now() + 10s;
	while(std::chrono::steady_clock::now() < Deadline && (Client.State() != CQuicClient::STATE_ONLINE || State.m_NewClient < 0))
	{
		Server.Update();
		Client.Update();
		std::this_thread::sleep_for(5ms);
	}
	ASSERT_GE(State.m_NewClient, 0);
	int Slot = State.m_NewClient;

	const std::vector<unsigned char> *pPeerCert = Server.PeerCert(Slot);
	ASSERT_NE(pPeerCert, nullptr);
	AccountLogin Login = pAcc->login_by_cert(rust::Slice<const uint8_t>(pPeerCert->data(), pPeerCert->size()));
	EXPECT_TRUE(std::string(Login.error).empty());
	EXPECT_EQ(Login.account_id, 0); // accountless client
	EXPECT_EQ(Login.public_key.size(), 32u);

	std::remove(pDbFile);
}
