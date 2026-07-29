#ifndef ENGINE_ACCOUNTS_H
#define ENGINE_ACCOUNTS_H

#include <cstdint>
#include <string>
#include <vector>

// Completion of an asynchronous account operation, see IAccounts.
class CAccountEvent
{
public:
	enum class EKind
	{
		CREDENTIAL_AUTH_EMAIL_TOKEN,
		CREDENTIAL_AUTH_STEAM_TOKEN,
		ACCOUNT_EMAIL_TOKEN,
		ACCOUNT_STEAM_TOKEN,
		LOGIN,
		LOGOUT,
		LOGOUT_ALL,
		DELETE,
		LINK_CREDENTIAL,
		UNLINK_CREDENTIAL,
		ACCOUNT_INFO,
		CERT_AND_KEY,
	};

	enum class EError
	{
		NONE,
		// Connection to the account server failed.
		HTTP,
		// Reading/writing the session key pair failed.
		FS,
		// The account server rate limited the request.
		RATE_LIMITED,
		// The account server denied the request because of a VPN ban.
		VPN_BAN,
		// The user must visit the web page in m_Payload to continue.
		WEB_VALIDATION_NEEDED,
		OTHER,
	};

	class CCredential
	{
	public:
		// "email" or "steam"
		std::string m_Kind;
		// partially masked email address or steam id
		std::string m_Identifier;
	};

	uint64_t m_RequestId = 0;
	EKind m_Kind = EKind::LOGIN;
	bool m_Success = false;
	EError m_Error = EError::NONE;
	std::string m_ErrorText;
	// Operation specific: profile key for LOGIN, token for steam token
	// operations, url for WEB_VALIDATION_NEEDED errors.
	std::string m_Payload;
	// For ACCOUNT_INFO events.
	int64_t m_AccountId = 0;
	std::string m_CreationDate;
	std::vector<CCredential> m_vCredentials;
};

// A stored account profile. One profile exists per account the user logged
// in to, the current profile determines as which account the user connects
// to game servers.
class CAccountProfile
{
public:
	std::string m_Key;
	std::string m_DisplayName;
	bool m_Current = false;
};

// Client side account management, implemented on top of the account server
// of `cl_account_server`. All operations are asynchronous: they return a
// request id and their completion arrives as CAccountEvent with the same id
// via FetchEvents().
class IAccounts
{
public:
	virtual ~IAccounts() = default;

	enum class ECredentialAuthOp
	{
		LOGIN,
		LINK_CREDENTIAL,
		UNLINK_CREDENTIAL,
	};

	enum class EAccountOp
	{
		LOGOUT_ALL,
		LINK_CREDENTIAL,
		DELETE,
	};

	// Whether account support is available at all.
	virtual bool Enabled() const = 0;

	// Requests a one time token for the given operation, sent by email.
	virtual uint64_t RequestCredentialAuthEmailToken(const char *pEmail, ECredentialAuthOp Op) = 0;
	// Requests a one time token for the given operation for a steam
	// session ticket. The token arrives in the payload of the event.
	virtual uint64_t RequestCredentialAuthSteamToken(const void *pTicket, size_t TicketSize, ECredentialAuthOp Op) = 0;
	// Like RequestCredentialAuthEmailToken, for account wide operations.
	virtual uint64_t RequestAccountEmailToken(const char *pEmail, EAccountOp Op) = 0;
	// Like RequestCredentialAuthSteamToken, for account wide operations.
	virtual uint64_t RequestAccountSteamToken(const void *pTicket, size_t TicketSize, EAccountOp Op) = 0;
	// Logs in with a token that was sent by email. On success the new
	// profile key is in the payload of the event.
	virtual uint64_t LoginEmail(const char *pEmail, const char *pToken) = 0;
	// Logs in with a token obtained for a steam session ticket.
	virtual uint64_t LoginSteam(const char *pSteamName, const char *pToken) = 0;
	// Logs out the given profile and removes its session.
	virtual uint64_t Logout(const char *pProfileKey) = 0;
	// Logs out all other sessions of the account.
	virtual uint64_t LogoutAll(const char *pProfileKey, const char *pAccountToken) = 0;
	// Deletes the account.
	virtual uint64_t Delete(const char *pProfileKey, const char *pAccountToken) = 0;
	// Links another credential (email or steam) to the account.
	virtual uint64_t LinkCredential(const char *pProfileKey, const char *pAccountToken, const char *pCredentialAuthToken) = 0;
	// Unlinks a credential from the account.
	virtual uint64_t UnlinkCredential(const char *pProfileKey, const char *pCredentialAuthToken) = 0;
	// Fetches account id, creation date and linked credentials.
	virtual uint64_t RequestAccountInfo(const char *pProfileKey) = 0;

	// The stored profiles, in stable order.
	virtual std::vector<CAccountProfile> Profiles() const = 0;
	// Whether a current profile exists, i.e. the user is logged in.
	virtual bool LoggedIn() const = 0;
	// Switches the current profile.
	virtual void SetProfile(const char *pProfileKey) = 0;
	// Changes the display name of a profile.
	virtual void SetProfileDisplayName(const char *pProfileKey, const char *pDisplayName) = 0;

	// Returns all completed operations since the last call.
	virtual std::vector<CAccountEvent> FetchEvents() = 0;
};

#endif
