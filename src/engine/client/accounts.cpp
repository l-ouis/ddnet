#include "accounts.h"

#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <string>

static accounts::ECredentialAuthOp CredentialAuthOp(IAccounts::ECredentialAuthOp Op)
{
	switch(Op)
	{
	case IAccounts::ECredentialAuthOp::LINK_CREDENTIAL:
		return accounts::ECredentialAuthOp::LINK_CREDENTIAL;
	case IAccounts::ECredentialAuthOp::UNLINK_CREDENTIAL:
		return accounts::ECredentialAuthOp::UNLINK_CREDENTIAL;
	default:
		return accounts::ECredentialAuthOp::LOGIN;
	}
}

static accounts::EAccountOp AccountOp(IAccounts::EAccountOp Op)
{
	switch(Op)
	{
	case IAccounts::EAccountOp::LINK_CREDENTIAL:
		return accounts::EAccountOp::LINK_CREDENTIAL;
	case IAccounts::EAccountOp::DELETE:
		return accounts::EAccountOp::DELETE;
	default:
		return accounts::EAccountOp::LOGOUT_ALL;
	}
}

static CAccountEvent EventFromBridge(const accounts::SAccountEvent &Event)
{
	CAccountEvent Result;
	Result.m_RequestId = Event.m_RequestId;
	switch(Event.m_Kind)
	{
	case accounts::EAccountEventKind::CREDENTIAL_AUTH_EMAIL_TOKEN:
		Result.m_Kind = CAccountEvent::EKind::CREDENTIAL_AUTH_EMAIL_TOKEN;
		break;
	case accounts::EAccountEventKind::CREDENTIAL_AUTH_STEAM_TOKEN:
		Result.m_Kind = CAccountEvent::EKind::CREDENTIAL_AUTH_STEAM_TOKEN;
		break;
	case accounts::EAccountEventKind::ACCOUNT_EMAIL_TOKEN:
		Result.m_Kind = CAccountEvent::EKind::ACCOUNT_EMAIL_TOKEN;
		break;
	case accounts::EAccountEventKind::ACCOUNT_STEAM_TOKEN:
		Result.m_Kind = CAccountEvent::EKind::ACCOUNT_STEAM_TOKEN;
		break;
	case accounts::EAccountEventKind::LOGIN:
		Result.m_Kind = CAccountEvent::EKind::LOGIN;
		break;
	case accounts::EAccountEventKind::LOGOUT:
		Result.m_Kind = CAccountEvent::EKind::LOGOUT;
		break;
	case accounts::EAccountEventKind::LOGOUT_ALL:
		Result.m_Kind = CAccountEvent::EKind::LOGOUT_ALL;
		break;
	case accounts::EAccountEventKind::DELETE:
		Result.m_Kind = CAccountEvent::EKind::DELETE;
		break;
	case accounts::EAccountEventKind::LINK_CREDENTIAL:
		Result.m_Kind = CAccountEvent::EKind::LINK_CREDENTIAL;
		break;
	case accounts::EAccountEventKind::UNLINK_CREDENTIAL:
		Result.m_Kind = CAccountEvent::EKind::UNLINK_CREDENTIAL;
		break;
	case accounts::EAccountEventKind::ACCOUNT_INFO:
		Result.m_Kind = CAccountEvent::EKind::ACCOUNT_INFO;
		break;
	default:
		Result.m_Kind = CAccountEvent::EKind::CERT_AND_KEY;
		break;
	}
	Result.m_Success = Event.m_Success;
	switch(Event.m_ErrorKind)
	{
	case accounts::EAccountErrorKind::HTTP:
		Result.m_Error = CAccountEvent::EError::HTTP;
		break;
	case accounts::EAccountErrorKind::FS:
		Result.m_Error = CAccountEvent::EError::FS;
		break;
	case accounts::EAccountErrorKind::RATE_LIMITED:
		Result.m_Error = CAccountEvent::EError::RATE_LIMITED;
		break;
	case accounts::EAccountErrorKind::VPN_BAN:
		Result.m_Error = CAccountEvent::EError::VPN_BAN;
		break;
	case accounts::EAccountErrorKind::WEB_VALIDATION_NEEDED:
		Result.m_Error = CAccountEvent::EError::WEB_VALIDATION_NEEDED;
		break;
	case accounts::EAccountErrorKind::OTHER:
		Result.m_Error = CAccountEvent::EError::OTHER;
		break;
	default:
		Result.m_Error = CAccountEvent::EError::NONE;
		break;
	}
	Result.m_ErrorText = std::string(Event.m_Error);
	Result.m_Warning = std::string(Event.m_Warning);
	Result.m_Payload = std::string(Event.m_Payload);
	Result.m_AccountId = Event.m_AccountId;
	Result.m_CreationDate = std::string(Event.m_CreationDate);
	for(const accounts::SAccountCredential &Credential : Event.m_vCredentials)
	{
		CAccountEvent::CCredential Result_Credential;
		Result_Credential.m_Kind = std::string(Credential.m_Kind);
		Result_Credential.m_Identifier = std::string(Credential.m_Identifier);
		Result.m_vCredentials.push_back(Result_Credential);
	}
	return Result;
}

void CAccountsManager::Init(IStorage *pStorage)
{
	if(g_Config.m_ClAccountServer[0] == '\0')
	{
		return;
	}
	pStorage->CreateFolder("accounts", IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, "accounts", aPath, sizeof(aPath));
	m_pClient.emplace(accounts::CreateAccountsClient(aPath, g_Config.m_ClAccountServer));
	const std::string Error = std::string((*m_pClient)->Error());
	if(!Error.empty())
	{
		log_error("accounts", "accounts disabled: %s", Error.c_str());
		m_pClient.reset();
	}
	else
	{
		log_info("accounts", "using account server '%s'", g_Config.m_ClAccountServer);
	}
}

void CAccountsManager::Update()
{
	if(!m_pClient.has_value())
	{
		return;
	}
	while(true)
	{
		const accounts::SAccountEvent Event = (*m_pClient)->PollEvent();
		if(!Event.m_Valid)
		{
			break;
		}
		if(Event.m_Kind == accounts::EAccountEventKind::CERT_AND_KEY && Event.m_RequestId != 0)
		{
			// All CERT_AND_KEY requests are internal, their completions
			// never reach FetchEvents. Only the latest request is
			// honored, completions of superseded requests are dropped.
			if(Event.m_RequestId != m_CertRequestId)
			{
				continue;
			}
			m_CertRequestId = 0;
			if(Event.m_Success && !Event.m_aCertDer.empty() && !Event.m_aKeyDer.empty())
			{
				m_vCertDer.assign(Event.m_aCertDer.begin(), Event.m_aCertDer.end());
				m_vKeyDer.assign(Event.m_aKeyDer.begin(), Event.m_aKeyDer.end());
				str_copy(m_aCertWarning, std::string(Event.m_Warning).c_str());
				m_CertReady = true;
				m_CertFailed = false;
			}
			else if(m_CertReady)
			{
				// A background refresh failed, keep the old certificate
				// and try again on the next refresh interval.
				log_warn("accounts", "certificate refresh failed: %s", std::string(Event.m_Error).c_str());
			}
			else
			{
				str_copy(m_aCertError, std::string(Event.m_Error).c_str());
				if(m_aCertError[0] == '\0')
				{
					str_copy(m_aCertError, "received an empty certificate");
				}
				m_CertFailed = true;
			}
			continue;
		}
		m_vEvents.push_back(EventFromBridge(Event));
	}
}

uint64_t CAccountsManager::RequestCredentialAuthEmailToken(const char *pEmail, ECredentialAuthOp Op)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->CredentialAuthEmailToken(pEmail, CredentialAuthOp(Op), "");
}

uint64_t CAccountsManager::RequestCredentialAuthSteamToken(const void *pTicket, size_t TicketSize, ECredentialAuthOp Op)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->CredentialAuthSteamToken(rust::Slice<const uint8_t>((const uint8_t *)pTicket, TicketSize), CredentialAuthOp(Op), "");
}

uint64_t CAccountsManager::RequestAccountEmailToken(const char *pEmail, EAccountOp Op)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->AccountEmailToken(pEmail, AccountOp(Op), "");
}

uint64_t CAccountsManager::RequestAccountSteamToken(const void *pTicket, size_t TicketSize, EAccountOp Op)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->AccountSteamToken(rust::Slice<const uint8_t>((const uint8_t *)pTicket, TicketSize), AccountOp(Op), "");
}

uint64_t CAccountsManager::LoginEmail(const char *pEmail, const char *pToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->LoginEmail(pEmail, pToken);
}

uint64_t CAccountsManager::LoginSteam(const char *pSteamName, const char *pToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->LoginSteam(pSteamName, pToken);
}

uint64_t CAccountsManager::Logout(const char *pProfileKey)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->Logout(pProfileKey);
}

uint64_t CAccountsManager::LogoutAll(const char *pProfileKey, const char *pAccountToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->LogoutAll(pProfileKey, pAccountToken);
}

uint64_t CAccountsManager::Delete(const char *pProfileKey, const char *pAccountToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->Delete(pProfileKey, pAccountToken);
}

uint64_t CAccountsManager::LinkCredential(const char *pProfileKey, const char *pAccountToken, const char *pCredentialAuthToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->LinkCredential(pProfileKey, pAccountToken, pCredentialAuthToken);
}

uint64_t CAccountsManager::UnlinkCredential(const char *pProfileKey, const char *pCredentialAuthToken)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->UnlinkCredential(pProfileKey, pCredentialAuthToken);
}

uint64_t CAccountsManager::RequestAccountInfo(const char *pProfileKey)
{
	if(!m_pClient.has_value())
		return 0;
	return (*m_pClient)->AccountInfo(pProfileKey);
}

std::vector<CAccountProfile> CAccountsManager::Profiles() const
{
	std::vector<CAccountProfile> vProfiles;
	if(!m_pClient.has_value())
	{
		return vProfiles;
	}
	for(const accounts::SAccountProfile &Profile : (*m_pClient)->Profiles())
	{
		CAccountProfile Result;
		Result.m_Key = std::string(Profile.m_Key);
		Result.m_DisplayName = std::string(Profile.m_DisplayName);
		Result.m_Current = Profile.m_Current;
		vProfiles.push_back(Result);
	}
	return vProfiles;
}

bool CAccountsManager::LoggedIn() const
{
	for(const CAccountProfile &Profile : Profiles())
	{
		if(Profile.m_Current)
		{
			return true;
		}
	}
	return false;
}

void CAccountsManager::SetProfile(const char *pProfileKey)
{
	if(m_pClient.has_value())
	{
		(*m_pClient)->SetProfile(pProfileKey);
	}
}

void CAccountsManager::SetProfileDisplayName(const char *pProfileKey, const char *pDisplayName)
{
	if(m_pClient.has_value())
	{
		(*m_pClient)->SetProfileDisplayName(pProfileKey, pDisplayName);
	}
}

std::vector<CAccountEvent> CAccountsManager::FetchEvents()
{
	std::vector<CAccountEvent> vEvents;
	std::swap(vEvents, m_vEvents);
	return vEvents;
}

void CAccountsManager::RequestConnectCert()
{
	m_CertReady = false;
	m_CertFailed = false;
	m_vCertDer.clear();
	m_vKeyDer.clear();
	m_aCertWarning[0] = '\0';
	m_aCertError[0] = '\0';
	if(!m_pClient.has_value())
	{
		return;
	}
	m_CertRequestId = (*m_pClient)->RequestCertAndKey();
}

void CAccountsManager::TryRefreshCert()
{
	if(!m_pClient.has_value() || !m_CertReady || m_CertRequestId != 0)
	{
		return;
	}
	// Checking often is unnecessary, the certificate lives for an hour.
	const int64_t Now = time_get();
	if(m_LastCertRefreshTime != 0 && Now - m_LastCertRefreshTime < 60 * time_freq())
	{
		return;
	}
	m_LastCertRefreshTime = Now;
	// Refresh in the background before the certificate expires, so a
	// dummy connect or reconnect always presents a valid certificate.
	constexpr int64_t REFRESH_MARGIN_SECONDS = 10 * 60;
	if(accounts::CertExpiresInSeconds(rust::Slice<const uint8_t>(m_vCertDer.data(), m_vCertDer.size())) > REFRESH_MARGIN_SECONDS)
	{
		return;
	}
	m_CertRequestId = (*m_pClient)->RequestCertAndKey();
}
