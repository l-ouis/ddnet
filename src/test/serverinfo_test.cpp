#include <base/str.h>

#include <engine/external/json-parser/json.h>
#include <engine/serverbrowser.h>
#include <engine/shared/serverinfo.h>

#include <gtest/gtest.h>

TEST(ServerInfo, ParseLocation)
{
	int Result;
	EXPECT_TRUE(CServerInfo::ParseLocation(&Result, "xx"));
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "an"));
	EXPECT_EQ(Result, CServerInfo::LOC_UNKNOWN);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "af"));
	EXPECT_EQ(Result, CServerInfo::LOC_AFRICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "eu-n"));
	EXPECT_EQ(Result, CServerInfo::LOC_EUROPE);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "na"));
	EXPECT_EQ(Result, CServerInfo::LOC_NORTH_AMERICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "sa"));
	EXPECT_EQ(Result, CServerInfo::LOC_SOUTH_AMERICA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "as:e"));
	EXPECT_EQ(Result, CServerInfo::LOC_ASIA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "as:cn"));
	EXPECT_EQ(Result, CServerInfo::LOC_CHINA);
	EXPECT_FALSE(CServerInfo::ParseLocation(&Result, "oc"));
	EXPECT_EQ(Result, CServerInfo::LOC_AUSTRALIA);
}

static unsigned int ParseCrcOrDeadbeef(const char *pString)
{
	unsigned int Result;
	if(ParseCrc(&Result, pString))
	{
		Result = 0xdeadbeef;
	}
	return Result;
}

TEST(ServerInfo, Crc)
{
	EXPECT_EQ(ParseCrcOrDeadbeef("00000000"), 0);
	EXPECT_EQ(ParseCrcOrDeadbeef("00000001"), 1);
	EXPECT_EQ(ParseCrcOrDeadbeef("12345678"), 0x12345678);
	EXPECT_EQ(ParseCrcOrDeadbeef("9abcdef0"), 0x9abcdef0);

	EXPECT_EQ(ParseCrcOrDeadbeef(""), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("a"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("x"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("ç"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("😢"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("0"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("000000000"), 0xdeadbeef);
	EXPECT_EQ(ParseCrcOrDeadbeef("00000000x"), 0xdeadbeef);
}

// Parses a server info JSON with the given "quic" member (empty = absent) and
// returns the resulting QUIC port, 0 meaning the advert was ignored, -1 that
// the whole info was rejected.
static int QuicPortFromJson(const char *pQuicJson, char *pHashOut, size_t HashOutSize)
{
	char aJson[1024];
	str_format(aJson, sizeof(aJson),
		"{\"max_clients\":64,\"max_players\":64,\"passworded\":false,"
		"\"game_type\":\"DDraceNetwork\",\"name\":\"test\",\"map\":{\"name\":\"Sunny\"},"
		"\"version\":\"0.6.4\",\"clients\":[]%s%s}",
		pQuicJson[0] == '\0' ? "" : ",\"quic\":", pQuicJson);
	json_value *pParsed = json_parse(aJson, str_length(aJson));
	EXPECT_NE(pParsed, nullptr);
	if(pParsed == nullptr)
	{
		return -1;
	}
	CServerInfo2 Info;
	const bool Error = CServerInfo2::FromJsonRaw(&Info, pParsed);
	json_value_free(pParsed);
	if(Error)
	{
		return -1;
	}
	if(pHashOut != nullptr)
	{
		str_copy(pHashOut, Info.m_aQuicPubKeySha256, HashOutSize);
	}
	return Info.m_QuicPort;
}

TEST(ServerInfo, ParseQuicAdvert)
{
	const char *pValidHash = "58bfba54d20d3e7bd44a30b0cd9a7e5d3ab2f809e9d43bf223bfa1af22a44f11";
	char aQuic[256];
	char aHash[65];

	// Absent or empty adverts leave QUIC disabled without rejecting the info.
	EXPECT_EQ(QuicPortFromJson("", nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("{}", nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("null", nullptr, 0), 0);

	str_format(aQuic, sizeof(aQuic), "{\"port\":8304,\"pubkey_sha256\":\"%s\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, aHash, sizeof(aHash)), 8304);
	EXPECT_STREQ(aHash, pValidHash);

	// Invalid adverts are ignored, not treated as a malformed server info.
	str_format(aQuic, sizeof(aQuic), "{\"port\":0,\"pubkey_sha256\":\"%s\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, nullptr, 0), 0);
	str_format(aQuic, sizeof(aQuic), "{\"port\":65536,\"pubkey_sha256\":\"%s\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, nullptr, 0), 0);
	str_format(aQuic, sizeof(aQuic), "{\"port\":-8304,\"pubkey_sha256\":\"%s\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, nullptr, 0), 0);
	str_format(aQuic, sizeof(aQuic), "{\"port\":\"8304\",\"pubkey_sha256\":\"%s\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("{\"port\":8304}", nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("{\"port\":8304,\"pubkey_sha256\":\"58bfba\"}", nullptr, 0), 0);
	str_format(aQuic, sizeof(aQuic), "{\"port\":8304,\"pubkey_sha256\":\"%s00\"}", pValidHash);
	EXPECT_EQ(QuicPortFromJson(aQuic, nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("{\"port\":8304,\"pubkey_sha256\":\"58bfba54d20d3e7bd44a30b0cd9a7e5d3ab2f809e9d43bf223bfa1af22a44fxx\"}", nullptr, 0), 0);
	EXPECT_EQ(QuicPortFromJson("\"not an object\"", nullptr, 0), 0);

	// Uppercase hex is accepted, the pinning comparison is case insensitive.
	EXPECT_EQ(QuicPortFromJson("{\"port\":8304,\"pubkey_sha256\":\"58BFBA54D20D3E7BD44A30B0CD9A7E5D3AB2F809E9D43BF223BFA1AF22A44F11\"}", nullptr, 0), 8304);
}
