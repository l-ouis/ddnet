#ifndef ENGINE_CLIENT_ACCOUNTS_H
#define ENGINE_CLIENT_ACCOUNTS_H

#include <engine/accounts.h>

#include <accounts/bridge.h>

#include <cstdint>
#include <optional>
#include <vector>

class IStorage;

// Engine side account manager, wrapping the Rust account client. Besides
// the IAccounts operations for the UI it provides the certificate and
// session key that the QUIC transport presents to game servers.
class CAccountsManager : public IAccounts
{
	std::optional<rust::Box<accounts::CAccountsClient>> m_pClient;
	std::vector<CAccountEvent> m_vEvents;

	// certificate for connecting to game servers
	uint64_t m_CertRequestId = 0;
	bool m_CertReady = false;
	std::vector<unsigned char> m_vCertDer;
	std::vector<unsigned char> m_vKeyDer;
	char m_aCertWarning[256] = "";

	int64_t m_LastCertRefreshTime = 0;

public:
	void Init(IStorage *pStorage);
	// Polls completed operations from the bridge. Must be called
	// regularly, e.g. once per frame.
	void Update();

	bool Enabled() const override { return m_pClient.has_value(); }

	uint64_t RequestCredentialAuthEmailToken(const char *pEmail, ECredentialAuthOp Op) override;
	uint64_t RequestCredentialAuthSteamToken(const void *pTicket, size_t TicketSize, ECredentialAuthOp Op) override;
	uint64_t RequestAccountEmailToken(const char *pEmail, EAccountOp Op) override;
	uint64_t RequestAccountSteamToken(const void *pTicket, size_t TicketSize, EAccountOp Op) override;
	uint64_t LoginEmail(const char *pEmail, const char *pToken) override;
	uint64_t LoginSteam(const char *pSteamName, const char *pToken) override;
	uint64_t Logout(const char *pProfileKey) override;
	uint64_t LogoutAll(const char *pProfileKey, const char *pAccountToken) override;
	uint64_t Delete(const char *pProfileKey, const char *pAccountToken) override;
	uint64_t LinkCredential(const char *pProfileKey, const char *pAccountToken, const char *pCredentialAuthToken) override;
	uint64_t UnlinkCredential(const char *pProfileKey, const char *pCredentialAuthToken) override;
	uint64_t RequestAccountInfo(const char *pProfileKey) override;

	std::vector<CAccountProfile> Profiles() const override;
	bool LoggedIn() const override;
	void SetProfile(const char *pProfileKey) override;
	void SetProfileDisplayName(const char *pProfileKey, const char *pDisplayName) override;

	std::vector<CAccountEvent> FetchEvents() override;

	// Requests the certificate and session key used to connect to game
	// servers. Completion is checked with ConnectCertReady().
	void RequestConnectCert();
	bool ConnectCertReady() const { return m_CertReady; }
	const std::vector<unsigned char> &ConnectCertDer() const { return m_vCertDer; }
	const std::vector<unsigned char> &ConnectKeyDer() const { return m_vKeyDer; }
	// Warning of the last certificate request, e.g. that the account
	// server was unreachable and an anonymous certificate is used.
	const char *CertWarning() const { return m_aCertWarning; }

	// Refreshes the account certificate in the background if it is about
	// to expire. Called regularly, cheap if nothing is to do.
	void TryRefreshCert();
};

#endif
