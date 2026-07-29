/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <engine/accounts.h>
#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <cinttypes>

static constexpr float ACCOUNT_VIEW_WIDTH = 500.0f;
static constexpr float ROW_HEIGHT = 25.0f;
static constexpr float ROW_SPACING = 5.0f;

void CMenus::StartAccountFlow(EAccountFlow Flow, const char *pEmail)
{
	IAccounts *pAccounts = Client()->Accounts();
	m_AccountFlow = Flow;
	switch(Flow)
	{
	case EAccountFlow::LOGIN:
		m_AccountRequestId = pAccounts->RequestCredentialAuthEmailToken(pEmail, IAccounts::ECredentialAuthOp::LOGIN);
		break;
	case EAccountFlow::LOGOUT_ALL:
		m_AccountRequestId = pAccounts->RequestAccountEmailToken(pEmail, IAccounts::EAccountOp::LOGOUT_ALL);
		break;
	case EAccountFlow::DELETE:
		m_AccountRequestId = pAccounts->RequestAccountEmailToken(pEmail, IAccounts::EAccountOp::DELETE);
		break;
	case EAccountFlow::LINK_EMAIL:
		if(m_AccountFlowStep == 0)
		{
			m_AccountRequestId = pAccounts->RequestAccountEmailToken(pEmail, IAccounts::EAccountOp::LINK_CREDENTIAL);
		}
		else
		{
			m_AccountRequestId = pAccounts->RequestCredentialAuthEmailToken(pEmail, IAccounts::ECredentialAuthOp::LINK_CREDENTIAL);
		}
		break;
	case EAccountFlow::UNLINK_EMAIL:
		m_AccountRequestId = pAccounts->RequestCredentialAuthEmailToken(pEmail, IAccounts::ECredentialAuthOp::UNLINK_CREDENTIAL);
		break;
	case EAccountFlow::NONE:
		return;
	}
	m_AccountState = EAccountState::TOKEN_WAIT;
}

void CMenus::AbortAccountFlow()
{
	m_AccountFlow = EAccountFlow::NONE;
	m_AccountFlowStep = 0;
	m_AccountRequestId = 0;
	m_aAccountToken[0] = '\0';
	m_AccountTokenInput.Clear();
	m_AccountState = EAccountState::OVERVIEW;
}

void CMenus::PopupConfirmAccountWebValidation()
{
	Client()->ViewLink(m_aAccountWebUrl);
}

void CMenus::AccountOpFailed(const CAccountEvent &Event)
{
	if(Event.m_Error == CAccountEvent::EError::WEB_VALIDATION_NEEDED)
	{
		str_copy(m_aAccountWebUrl, Event.m_Payload.c_str());
		PopupConfirm(Localize("Web validation needed"), Localize("The account server wants to verify you in a web browser before continuing."),
			Localize("Open browser"), Localize("Cancel"), &CMenus::PopupConfirmAccountWebValidation);
	}
	else
	{
		PopupMessage(Localize("Account error"), Event.m_ErrorText.c_str(), Localize("Ok"));
	}
	AbortAccountFlow();
}

void CMenus::AccountFlowTokenReceived(const CAccountEvent &Event)
{
	if(!Event.m_Success)
	{
		AccountOpFailed(Event);
		return;
	}
	m_AccountTokenInput.Clear();
	m_AccountState = EAccountState::TOKEN_ENTER;
}

void CMenus::ProcessAccountEvents()
{
	for(const CAccountEvent &Event : Client()->Accounts()->FetchEvents())
	{
		if(Event.m_RequestId != m_AccountRequestId)
		{
			continue;
		}
		switch(Event.m_Kind)
		{
		case CAccountEvent::EKind::CREDENTIAL_AUTH_EMAIL_TOKEN:
		case CAccountEvent::EKind::ACCOUNT_EMAIL_TOKEN:
			AccountFlowTokenReceived(Event);
			break;
		case CAccountEvent::EKind::LOGIN:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("You are now logged in. The login takes effect the next time you join a server."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::LOGOUT:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("You are now logged out."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::LOGOUT_ALL:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("All other sessions of your account were logged out."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::DELETE:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("Your account was deleted."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::LINK_CREDENTIAL:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("The email address was linked to your account."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::UNLINK_CREDENTIAL:
			if(Event.m_Success)
			{
				PopupMessage(Localize("Account"), Localize("The email address was unlinked from your account."), Localize("Ok"));
				AbortAccountFlow();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::ACCOUNT_INFO:
			if(Event.m_Success)
			{
				m_AccountInfoId = Event.m_AccountId;
				str_copy(m_aAccountInfoCreationDate, Event.m_CreationDate.c_str());
				m_vAccountInfoCredentials = Event.m_vCredentials;
				m_AccountState = EAccountState::INFO;
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		default:
			break;
		}
	}
}

void CMenus::RenderAccountOverview(CUIRect MainView)
{
	CUIRect Row, Button;
	const std::vector<CAccountProfile> vProfiles = Client()->Accounts()->Profiles();
	const CAccountProfile *pCurrentProfile = nullptr;
	for(const CAccountProfile &Profile : vProfiles)
	{
		if(Profile.m_Current)
		{
			pCurrentProfile = &Profile;
		}
	}

	if(!vProfiles.empty())
	{
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Profiles"), 14.0f, TEXTALIGN_ML);
		static std::vector<CButtonContainer> s_vProfileButtons;
		s_vProfileButtons.resize(vProfiles.size());
		for(size_t i = 0; i < vProfiles.size(); i++)
		{
			MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
			MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
			if(DoButton_Menu(&s_vProfileButtons[i], vProfiles[i].m_DisplayName.c_str(), vProfiles[i].m_Current, &Row))
			{
				Client()->Accounts()->SetProfile(vProfiles[i].m_Key.c_str());
			}
		}

		if(pCurrentProfile != nullptr)
		{
			MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
			MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
			CUIRect InfoButton, LogoutButton;
			Row.VSplitMid(&InfoButton, &LogoutButton, ROW_SPACING);
			static CButtonContainer s_InfoButton;
			if(DoButton_Menu(&s_InfoButton, Localize("Account info"), 0, &InfoButton))
			{
				str_copy(m_aAccountInfoProfileKey, pCurrentProfile->m_Key.c_str());
				m_AccountRequestId = Client()->Accounts()->RequestAccountInfo(m_aAccountInfoProfileKey);
				m_AccountState = EAccountState::INFO_WAIT;
			}
			static CButtonContainer s_LogoutButton;
			if(DoButton_Menu(&s_LogoutButton, Localize("Log out"), 0, &LogoutButton))
			{
				m_AccountRequestId = Client()->Accounts()->Logout(pCurrentProfile->m_Key.c_str());
				m_AccountState = EAccountState::OP_WAIT;
			}
		}

		MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);
	}

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Ui()->DoLabel(&Row, vProfiles.empty() ? Localize("Log in with your email address") : Localize("Add another account"), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitRight(160.0f, &Row, &Button);
	Row.VSplitRight(ROW_SPACING, &Row, nullptr);
	m_AccountEmailInput.SetEmptyText(Localize("Email address"));
	Ui()->DoEditBox(&m_AccountEmailInput, &Row, 14.0f);
	static CButtonContainer s_LoginButton;
	if(DoButton_Menu(&s_LoginButton, Localize("Send login code"), 0, &Button) && !m_AccountEmailInput.IsEmpty())
	{
		StartAccountFlow(EAccountFlow::LOGIN, m_AccountEmailInput.GetString());
	}

	MainView.HSplitTop(2 * ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(2 * ROW_HEIGHT, &Row, &MainView);
	SLabelProperties Props;
	Props.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, Localize("A one time code is sent to your email address, no password is needed. The login is used when you join a server that supports accounts."), 10.0f, TEXTALIGN_ML, Props);
}

void CMenus::RenderAccountTokenEnter(CUIRect MainView)
{
	CUIRect Row, Button;
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Ui()->DoLabel(&Row, m_AccountFlow == EAccountFlow::LOGIN ? Localize("A login code was sent to your email address.") : Localize("A confirmation code was sent to your email address."), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitRight(160.0f, &Row, &Button);
	Row.VSplitRight(ROW_SPACING, &Row, nullptr);
	m_AccountTokenInput.SetEmptyText(Localize("Code"));
	Ui()->DoEditBox(&m_AccountTokenInput, &Row, 14.0f);
	static CButtonContainer s_ContinueButton;
	if((DoButton_Menu(&s_ContinueButton, Localize("Continue"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)) && !m_AccountTokenInput.IsEmpty())
	{
		IAccounts *pAccounts = Client()->Accounts();
		switch(m_AccountFlow)
		{
		case EAccountFlow::LOGIN:
			m_AccountRequestId = pAccounts->LoginEmail(m_AccountEmailInput.GetString(), m_AccountTokenInput.GetString());
			m_AccountState = EAccountState::OP_WAIT;
			break;
		case EAccountFlow::LOGOUT_ALL:
			m_AccountRequestId = pAccounts->LogoutAll(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
			m_AccountState = EAccountState::OP_WAIT;
			break;
		case EAccountFlow::DELETE:
			m_AccountRequestId = pAccounts->Delete(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
			m_AccountState = EAccountState::OP_WAIT;
			break;
		case EAccountFlow::LINK_EMAIL:
			if(m_AccountFlowStep == 0)
			{
				// The account token is ready, continue with the
				// credential auth token of the new email address.
				str_copy(m_aAccountToken, m_AccountTokenInput.GetString());
				m_AccountFlowStep = 1;
				StartAccountFlow(EAccountFlow::LINK_EMAIL, m_AccountLinkEmailInput.GetString());
			}
			else
			{
				m_AccountRequestId = pAccounts->LinkCredential(m_aAccountInfoProfileKey, m_aAccountToken, m_AccountTokenInput.GetString());
				m_AccountState = EAccountState::OP_WAIT;
			}
			break;
		case EAccountFlow::UNLINK_EMAIL:
			m_AccountRequestId = pAccounts->UnlinkCredential(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
			m_AccountState = EAccountState::OP_WAIT;
			break;
		case EAccountFlow::NONE:
			m_AccountState = EAccountState::OVERVIEW;
			break;
		}
	}

	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(160.0f, &Button, nullptr);
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &Button))
	{
		AbortAccountFlow();
	}
}

void CMenus::RenderAccountInfo(CUIRect MainView)
{
	CUIRect Row, Button;
	char aBuf[256];

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s: %" PRId64, Localize("Account id"), m_AccountInfoId);
	Ui()->DoLabel(&Row, aBuf, 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	str_format(aBuf, sizeof(aBuf), "%s: %s", Localize("Created"), m_aAccountInfoCreationDate);
	Ui()->DoLabel(&Row, aBuf, 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("Linked credentials:"), 14.0f, TEXTALIGN_ML);
	bool HasEmail = false;
	for(const CAccountEvent::CCredential &Credential : m_vAccountInfoCredentials)
	{
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		if(Credential.m_Kind == "email")
		{
			HasEmail = true;
			str_format(aBuf, sizeof(aBuf), "- %s: %s", Localize("Email"), Credential.m_Identifier.c_str());
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "- Steam: %s", Credential.m_Identifier.c_str());
		}
		Ui()->DoLabel(&Row, aBuf, 14.0f, TEXTALIGN_ML);
	}

	MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitMid(&Button, &Row, ROW_SPACING);
	static CButtonContainer s_LogoutAllButton;
	if(DoButton_Menu(&s_LogoutAllButton, Localize("Log out everywhere else"), 0, &Button))
	{
		m_AccountFlow = EAccountFlow::LOGOUT_ALL;
		m_AccountState = EAccountState::EMAIL_ENTER;
	}
	static CButtonContainer s_LinkEmailButton;
	if(DoButton_Menu(&s_LinkEmailButton, Localize("Link another email"), 0, &Row))
	{
		m_AccountFlow = EAccountFlow::LINK_EMAIL;
		m_AccountFlowStep = 0;
		m_AccountState = EAccountState::EMAIL_ENTER;
	}

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitMid(&Button, &Row, ROW_SPACING);
	static CButtonContainer s_UnlinkEmailButton;
	if(DoButton_Menu(&s_UnlinkEmailButton, Localize("Unlink email"), 0, &Button) && HasEmail)
	{
		m_AccountFlow = EAccountFlow::UNLINK_EMAIL;
		m_AccountState = EAccountState::EMAIL_ENTER;
	}
	static CButtonContainer s_DeleteButton;
	if(DoButton_Menu(&s_DeleteButton, Localize("Delete account"), 0, &Row))
	{
		m_AccountFlow = EAccountFlow::DELETE;
		m_AccountState = EAccountState::EMAIL_ENTER;
	}

	MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(160.0f, &Button, nullptr);
	static CButtonContainer s_BackButton;
	if(DoButton_Menu(&s_BackButton, Localize("Back"), 0, &Button))
	{
		m_AccountState = EAccountState::OVERVIEW;
	}
}

void CMenus::RenderAccount(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);
	ProcessAccountEvents();

	MainView.Draw(ms_ColorTabbarActive, IGraphics::CORNER_B, 10.0f);
	MainView.Margin(20.0f, &MainView);
	if(MainView.w > ACCOUNT_VIEW_WIDTH)
	{
		MainView.VMargin((MainView.w - ACCOUNT_VIEW_WIDTH) / 2.0f, &MainView);
	}

	CUIRect Row;
	MainView.HSplitTop(30.0f, &Row, &MainView);
	Ui()->DoLabel(&Row, Localize("Account"), 20.0f, TEXTALIGN_MC);
	MainView.HSplitTop(10.0f, nullptr, &MainView);

	if(!Client()->Accounts()->Enabled())
	{
		SLabelProperties Props;
		Props.m_MaxWidth = MainView.w;
		Ui()->DoLabel(&MainView, Localize("Accounts are unavailable. Set cl_account_server to the URL of an account server and restart to enable them."), 14.0f, TEXTALIGN_MC, Props);
		return;
	}

	switch(m_AccountState)
	{
	case EAccountState::OVERVIEW:
		RenderAccountOverview(MainView);
		break;
	case EAccountState::EMAIL_ENTER:
	{
		CUIRect Button;
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		const char *pPrompt;
		if(m_AccountFlow == EAccountFlow::LINK_EMAIL && m_AccountFlowStep == 1)
		{
			pPrompt = Localize("Enter the new email address to link:");
		}
		else if(m_AccountFlow == EAccountFlow::UNLINK_EMAIL)
		{
			pPrompt = Localize("Enter the email address to unlink:");
		}
		else
		{
			pPrompt = Localize("Enter the email address of your account:");
		}
		Ui()->DoLabel(&Row, pPrompt, 14.0f, TEXTALIGN_ML);
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
		Row.VSplitRight(160.0f, &Row, &Button);
		Row.VSplitRight(ROW_SPACING, &Row, nullptr);
		CLineInput &Input = m_AccountFlow == EAccountFlow::LINK_EMAIL && m_AccountFlowStep == 1 ? m_AccountLinkEmailInput : m_AccountEmailInput;
		Input.SetEmptyText(Localize("Email address"));
		Ui()->DoEditBox(&Input, &Row, 14.0f);
		static CButtonContainer s_SendButton;
		if((DoButton_Menu(&s_SendButton, Localize("Send code"), 0, &Button) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)) && !Input.IsEmpty())
		{
			StartAccountFlow(m_AccountFlow, Input.GetString());
		}
		MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		Row.VSplitLeft(160.0f, &Button, nullptr);
		static CButtonContainer s_CancelButton;
		if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &Button))
		{
			AbortAccountFlow();
		}
		break;
	}
	case EAccountState::TOKEN_WAIT:
	case EAccountState::OP_WAIT:
	case EAccountState::INFO_WAIT:
		MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
		Ui()->DoLabel(&Row, Localize("Please wait…"), 14.0f, TEXTALIGN_MC);
		break;
	case EAccountState::TOKEN_ENTER:
		RenderAccountTokenEnter(MainView);
		break;
	case EAccountState::INFO:
		RenderAccountInfo(MainView);
		break;
	}
}
