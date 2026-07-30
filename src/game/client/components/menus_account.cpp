/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <engine/accounts.h>
#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <cinttypes>

static constexpr float ACCOUNT_VIEW_WIDTH = 500.0f;
static constexpr float ROW_HEIGHT = 25.0f;
static constexpr float ROW_SPACING = 5.0f;
static constexpr float BUTTON_WIDTH = 160.0f;
static constexpr size_t MAX_VISIBLE_PROFILES = 5;

static ColorRGBA AccountButtonColor(bool Enabled)
{
	return ColorRGBA(1.0f, 1.0f, 1.0f, Enabled ? 0.5f : 0.25f);
}

void CMenus::SetAccountWaitState(EAccountState WaitState, EAccountState ReturnState)
{
	m_AccountStateBeforeWait = ReturnState;
	m_aAccountFlowError[0] = '\0';
	m_AccountState = WaitState;
}

CMenus::EAccountState CMenus::AccountFlowHome() const
{
	switch(m_AccountFlow)
	{
	case EAccountFlow::LOGOUT_ALL:
	case EAccountFlow::DELETE:
	case EAccountFlow::LINK_EMAIL:
	case EAccountFlow::UNLINK_EMAIL:
		// These flows start on the account info page.
		return EAccountState::INFO;
	case EAccountFlow::LOGIN:
	case EAccountFlow::NONE:
		break;
	}
	return EAccountState::OVERVIEW;
}

void CMenus::ResetAccountFlow(EAccountState NextState)
{
	m_AccountFlow = EAccountFlow::NONE;
	m_AccountFlowStep = 0;
	m_AccountRequestId = 0;
	m_aAccountToken[0] = '\0';
	m_AccountTokenInput.Clear();
	m_aAccountFlowEmail[0] = '\0';
	m_aAccountFlowError[0] = '\0';
	m_AccountState = NextState;
}

void CMenus::AbortAccountFlow()
{
	ResetAccountFlow(AccountFlowHome());
}

void CMenus::StartAccountFlow(EAccountFlow Flow, const char *pEmail)
{
	IAccounts *pAccounts = Client()->Accounts();
	m_AccountFlow = Flow;
	str_copy(m_aAccountFlowEmail, pEmail);
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
	SetAccountWaitState(EAccountState::TOKEN_WAIT, m_AccountState);
}

void CMenus::StartAccountEmailEnter(EAccountFlow Flow)
{
	m_AccountFlow = Flow;
	m_AccountFlowStep = 0;
	m_AccountTokenInput.Clear();
	m_aAccountFlowError[0] = '\0';
	m_AccountState = EAccountState::EMAIL_ENTER;
}

void CMenus::StartAccountInfoRequest()
{
	m_AccountRequestId = Client()->Accounts()->RequestAccountInfo(m_aAccountInfoProfileKey);
	SetAccountWaitState(EAccountState::INFO_WAIT, EAccountState::OVERVIEW);
}

void CMenus::PopupConfirmAccountWebValidation()
{
	Client()->ViewLink(m_aAccountWebUrl);
}

void CMenus::PopupConfirmAccountDelete()
{
	m_AccountRequestId = Client()->Accounts()->Delete(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
	SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::TOKEN_ENTER);
}

const char *CMenus::AccountErrorMessage(const CAccountEvent &Event) const
{
	switch(Event.m_Error)
	{
	case CAccountEvent::EError::HTTP:
		return Localize("Could not reach the account server. Check your internet connection and try again.");
	case CAccountEvent::EError::FS:
		return Localize("Saving the account data failed.");
	case CAccountEvent::EError::RATE_LIMITED:
		return Localize("Too many attempts. Please wait a moment and try again.");
	case CAccountEvent::EError::VPN_BAN:
		return Localize("The account server rejected this connection (VPN detected).");
	case CAccountEvent::EError::WEB_VALIDATION_NEEDED:
	case CAccountEvent::EError::OTHER:
	case CAccountEvent::EError::NONE:
		break;
	}
	// The server-provided text can carry meaning, e.g. a wrong or expired code.
	return Event.m_ErrorText.c_str();
}

void CMenus::AccountOpFailed(const CAccountEvent &Event)
{
	const bool ReturnToScreen = m_AccountStateBeforeWait == EAccountState::EMAIL_ENTER || m_AccountStateBeforeWait == EAccountState::TOKEN_ENTER;
	if(Event.m_Error == CAccountEvent::EError::WEB_VALIDATION_NEEDED)
	{
		str_copy(m_aAccountWebUrl, Event.m_Payload.c_str());
		PopupConfirm(Localize("Web validation needed"), Localize("The account server wants to verify you in a web browser before continuing. Try again afterwards."),
			Localize("Open browser"), Localize("Cancel"), &CMenus::PopupConfirmAccountWebValidation);
		// Return to where the request was started so it can be retried
		// after the web validation was solved.
		if(ReturnToScreen)
		{
			m_AccountRequestId = 0;
			m_AccountState = m_AccountStateBeforeWait;
		}
		else
		{
			ResetAccountFlow(m_AccountStateBeforeWait);
		}
		return;
	}

	if(Event.m_Error == CAccountEvent::EError::HTTP ||
		Event.m_Error == CAccountEvent::EError::FS ||
		Event.m_Error == CAccountEvent::EError::VPN_BAN)
	{
		// Hard errors that retrying with different input cannot fix
		// abort the flow entirely.
		PopupMessage(Localize("Account error"), AccountErrorMessage(Event), Localize("Ok"));
		AbortAccountFlow();
		return;
	}

	// Retryable errors (rate limit, wrong or expired code, ...): return to
	// the previous screen with the error shown inline, keeping the input.
	const char *pError = AccountErrorMessage(Event);
	if(ReturnToScreen)
	{
		m_AccountRequestId = 0;
		m_AccountState = m_AccountStateBeforeWait;
	}
	else
	{
		ResetAccountFlow(m_AccountStateBeforeWait);
	}
	str_copy(m_aAccountFlowError, pError);
}

void CMenus::AccountFlowTokenReceived(const CAccountEvent &Event)
{
	if(!Event.m_Success)
	{
		AccountOpFailed(Event);
		return;
	}
	m_AccountRequestId = 0;
	m_AccountTokenInput.Clear();
	m_aAccountFlowError[0] = '\0';
	m_AccountState = EAccountState::TOKEN_ENTER;
}

void CMenus::ProcessAccountEvents()
{
	for(const CAccountEvent &Event : Client()->Accounts()->FetchEvents())
	{
		if(Event.m_RequestId == 0)
		{
			// Unsolicited event: the bridge changed state on its own.
			if(Event.m_Kind == CAccountEvent::EKind::LOGOUT)
			{
				PopupMessage(Localize("Logged out"), Localize("Your session is no longer valid and the account profile was removed."), Localize("Ok"));
				// The payload is the key of the removed profile. Leave any
				// state that operates on it.
				if(str_comp(m_aAccountInfoProfileKey, Event.m_Payload.c_str()) == 0 &&
					(m_AccountState == EAccountState::INFO || m_AccountState == EAccountState::INFO_WAIT || AccountFlowHome() == EAccountState::INFO))
				{
					ResetAccountFlow(EAccountState::OVERVIEW);
				}
			}
			continue;
		}
		if(m_AccountRequestId == 0 || Event.m_RequestId != m_AccountRequestId)
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
				ResetAccountFlow(EAccountState::OVERVIEW);
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
				ResetAccountFlow(EAccountState::OVERVIEW);
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
				ResetAccountFlow(EAccountState::OVERVIEW);
				StartAccountInfoRequest();
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
				ResetAccountFlow(EAccountState::OVERVIEW);
				m_aAccountInfoProfileKey[0] = '\0';
				m_AccountInfoId = 0;
				m_aAccountInfoCreationDate[0] = '\0';
				m_vAccountInfoCredentials.clear();
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
				m_AccountLinkEmailInput.Clear();
				ResetAccountFlow(EAccountState::OVERVIEW);
				StartAccountInfoRequest();
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
				ResetAccountFlow(EAccountState::OVERVIEW);
				StartAccountInfoRequest();
			}
			else
			{
				AccountOpFailed(Event);
			}
			break;
		case CAccountEvent::EKind::ACCOUNT_INFO:
			if(Event.m_Success)
			{
				m_AccountRequestId = 0;
				m_AccountInfoId = Event.m_AccountId;
				str_copy(m_aAccountInfoCreationDate, Event.m_CreationDate.c_str());
				m_vAccountInfoCredentials = Event.m_vCredentials;
				m_aAccountFlowError[0] = '\0';
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

void CMenus::RenderAccountFlowError(CUIRect *pMainView)
{
	if(m_aAccountFlowError[0] == '\0')
	{
		return;
	}
	CUIRect Row;
	pMainView->HSplitTop(2.0f * ROW_HEIGHT, &Row, pMainView);
	pMainView->HSplitTop(ROW_SPACING, nullptr, pMainView);
	SLabelProperties Props;
	Props.m_MaxWidth = Row.w;
	Props.SetColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
	Ui()->DoLabel(&Row, m_aAccountFlowError, 12.0f, TEXTALIGN_ML, Props);
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

		while(m_vAccountProfileButtons.size() < vProfiles.size())
		{
			m_vAccountProfileButtons.emplace_back();
		}

		const float ProfileRowHeight = ROW_HEIGHT + ROW_SPACING;
		const float ListHeight = std::min(vProfiles.size(), MAX_VISIBLE_PROFILES) * ProfileRowHeight;
		CUIRect List;
		MainView.HSplitTop(ListHeight, &List, &MainView);
		CScrollRegionParams ScrollParams;
		ScrollParams.m_ScrollUnit = ProfileRowHeight;
		m_AccountProfilesScrollRegion.Begin(&List, &ScrollParams);
		for(size_t i = 0; i < vProfiles.size(); i++)
		{
			List.HSplitTop(ROW_HEIGHT, &Row, &List);
			List.HSplitTop(ROW_SPACING, nullptr, &List);
			if(m_AccountProfilesScrollRegion.AddRect(Row) &&
				DoButton_Menu(&m_vAccountProfileButtons[i], vProfiles[i].m_DisplayName.c_str(), vProfiles[i].m_Current, &Row))
			{
				Client()->Accounts()->SetProfile(vProfiles[i].m_Key.c_str());
			}
		}
		m_AccountProfilesScrollRegion.End();

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
				StartAccountInfoRequest();
			}
			static CButtonContainer s_LogoutButton;
			if(DoButton_Menu(&s_LogoutButton, Localize("Log out"), 0, &LogoutButton))
			{
				m_AccountRequestId = Client()->Accounts()->Logout(pCurrentProfile->m_Key.c_str());
				SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::OVERVIEW);
			}
		}

		MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);
	}

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Ui()->DoLabel(&Row, vProfiles.empty() ? Localize("Log in with your email address") : Localize("Add another account"), 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitRight(BUTTON_WIDTH, &Row, &Button);
	Row.VSplitRight(ROW_SPACING, &Row, nullptr);
	m_AccountEmailInput.SetEmptyText(Localize("Email address"));
	Ui()->DoEditBox(&m_AccountEmailInput, &Row, 14.0f);
	static CButtonContainer s_LoginButton;
	const bool SendEnabled = !m_AccountEmailInput.IsEmpty();
	if((DoButton_Menu(&s_LoginButton, Localize("Send login code"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, AccountButtonColor(SendEnabled)) ||
		   (m_AccountEmailInput.IsActive() && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))) &&
		SendEnabled)
	{
		StartAccountFlow(EAccountFlow::LOGIN, m_AccountEmailInput.GetString());
	}

	RenderAccountFlowError(&MainView);

	MainView.HSplitTop(2 * ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(2 * ROW_HEIGHT, &Row, &MainView);
	SLabelProperties Props;
	Props.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, Localize("A one time code is sent to your email address, no password is needed. The login is used when you join a server that supports accounts."), 10.0f, TEXTALIGN_ML, Props);
}

void CMenus::RenderAccountEmailEnter(CUIRect MainView)
{
	CUIRect Row, Button;

	const char *pPrompt;
	switch(m_AccountFlow)
	{
	case EAccountFlow::LOGOUT_ALL:
		pPrompt = Localize("To log out all other sessions, enter the email address of your account:");
		break;
	case EAccountFlow::DELETE:
		pPrompt = Localize("To delete your account, enter the email address of your account:");
		break;
	case EAccountFlow::LINK_EMAIL:
		if(m_AccountFlowStep == 0)
		{
			pPrompt = Localize("To link another email address, first enter an email address that is already linked to your account:");
		}
		else
		{
			pPrompt = Localize("Enter the new email address to link to your account:");
		}
		break;
	case EAccountFlow::UNLINK_EMAIL:
		pPrompt = Localize("Enter the email address to unlink from your account:");
		break;
	default:
		pPrompt = Localize("Enter the email address of your account:");
		break;
	}
	MainView.HSplitTop(2 * ROW_HEIGHT, &Row, &MainView);
	SLabelProperties PromptProps;
	PromptProps.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, pPrompt, 14.0f, TEXTALIGN_ML, PromptProps);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitRight(BUTTON_WIDTH, &Row, &Button);
	Row.VSplitRight(ROW_SPACING, &Row, nullptr);
	CLineInput &Input = m_AccountFlow == EAccountFlow::LINK_EMAIL && m_AccountFlowStep == 1 ? m_AccountLinkEmailInput : m_AccountEmailInput;
	Input.SetEmptyText(Localize("Email address"));
	Ui()->DoEditBox(&Input, &Row, 14.0f);
	static CButtonContainer s_SendButton;
	const bool SendEnabled = !Input.IsEmpty();
	if((DoButton_Menu(&s_SendButton, Localize("Send code"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, AccountButtonColor(SendEnabled)) ||
		   Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)) &&
		SendEnabled)
	{
		StartAccountFlow(m_AccountFlow, Input.GetString());
	}

	RenderAccountFlowError(&MainView);

	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(BUTTON_WIDTH, &Button, nullptr);
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &Button))
	{
		AbortAccountFlow();
	}
}

void CMenus::RenderAccountTokenEnter(CUIRect MainView)
{
	CUIRect Row, Button;
	char aBuf[512];

	const char *pPromptFormat;
	switch(m_AccountFlow)
	{
	case EAccountFlow::LOGIN:
		pPromptFormat = Localize("A login code was sent to '%s'. Enter the code to log in:");
		break;
	case EAccountFlow::LOGOUT_ALL:
		pPromptFormat = Localize("To log out all other sessions, enter the confirmation code that was sent to '%s':");
		break;
	case EAccountFlow::DELETE:
		pPromptFormat = Localize("To delete your account, enter the confirmation code that was sent to '%s':");
		break;
	case EAccountFlow::LINK_EMAIL:
		if(m_AccountFlowStep == 0)
		{
			pPromptFormat = Localize("To link another email address, enter the confirmation code that was sent to your account email address '%s':");
		}
		else
		{
			pPromptFormat = Localize("Enter the code that was sent to the new email address '%s':");
		}
		break;
	case EAccountFlow::UNLINK_EMAIL:
		pPromptFormat = Localize("To unlink this email address, enter the confirmation code that was sent to '%s':");
		break;
	default:
		pPromptFormat = Localize("A confirmation code was sent to '%s'. Enter the code:");
		break;
	}
	str_format(aBuf, sizeof(aBuf), pPromptFormat, m_aAccountFlowEmail);
	MainView.HSplitTop(2 * ROW_HEIGHT, &Row, &MainView);
	SLabelProperties PromptProps;
	PromptProps.m_MaxWidth = Row.w;
	Ui()->DoLabel(&Row, aBuf, 14.0f, TEXTALIGN_ML, PromptProps);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitRight(BUTTON_WIDTH, &Row, &Button);
	Row.VSplitRight(ROW_SPACING, &Row, nullptr);
	m_AccountTokenInput.SetEmptyText(Localize("Code"));
	Ui()->DoEditBox(&m_AccountTokenInput, &Row, 14.0f);
	static CButtonContainer s_ContinueButton;
	const bool ContinueEnabled = !m_AccountTokenInput.IsEmpty();
	if((DoButton_Menu(&s_ContinueButton, Localize("Continue"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, AccountButtonColor(ContinueEnabled)) ||
		   Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER)) &&
		ContinueEnabled)
	{
		IAccounts *pAccounts = Client()->Accounts();
		switch(m_AccountFlow)
		{
		case EAccountFlow::LOGIN:
			m_AccountRequestId = pAccounts->LoginEmail(m_aAccountFlowEmail, m_AccountTokenInput.GetString());
			SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::TOKEN_ENTER);
			break;
		case EAccountFlow::LOGOUT_ALL:
			m_AccountRequestId = pAccounts->LogoutAll(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
			SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::TOKEN_ENTER);
			break;
		case EAccountFlow::DELETE:
			PopupConfirm(Localize("Delete account"), Localize("This permanently deletes your account and all linked credentials. This cannot be undone."),
				Localize("Delete account"), Localize("Cancel"), &CMenus::PopupConfirmAccountDelete);
			break;
		case EAccountFlow::LINK_EMAIL:
			if(m_AccountFlowStep == 0)
			{
				// The account token is ready, continue by asking for the
				// new email address to link.
				str_copy(m_aAccountToken, m_AccountTokenInput.GetString());
				m_AccountFlowStep = 1;
				m_AccountTokenInput.Clear();
				m_aAccountFlowError[0] = '\0';
				m_AccountState = EAccountState::EMAIL_ENTER;
			}
			else
			{
				m_AccountRequestId = pAccounts->LinkCredential(m_aAccountInfoProfileKey, m_aAccountToken, m_AccountTokenInput.GetString());
				SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::TOKEN_ENTER);
			}
			break;
		case EAccountFlow::UNLINK_EMAIL:
			m_AccountRequestId = pAccounts->UnlinkCredential(m_aAccountInfoProfileKey, m_AccountTokenInput.GetString());
			SetAccountWaitState(EAccountState::OP_WAIT, EAccountState::TOKEN_ENTER);
			break;
		case EAccountFlow::NONE:
			m_AccountState = EAccountState::OVERVIEW;
			break;
		}
	}

	RenderAccountFlowError(&MainView);

	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(BUTTON_WIDTH, &Button, nullptr);
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &Button))
	{
		AbortAccountFlow();
	}
}

void CMenus::RenderAccountWait(CUIRect MainView)
{
	CUIRect Row, Spinner, Button;
	const char *pLabel;
	switch(m_AccountState)
	{
	case EAccountState::TOKEN_WAIT:
		pLabel = Localize("Requesting a confirmation code…");
		break;
	case EAccountState::INFO_WAIT:
		pLabel = Localize("Loading account info…");
		break;
	default:
		pLabel = Localize("Please wait…");
		break;
	}

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(ROW_HEIGHT, &Spinner, &Row);
	Row.VSplitLeft(ROW_SPACING, nullptr, &Row);
	Ui()->RenderProgressSpinner(Spinner.Center(), 8.0f);
	Ui()->DoLabel(&Row, pLabel, 14.0f, TEXTALIGN_ML);

	MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(BUTTON_WIDTH, &Button, nullptr);
	static CButtonContainer s_CancelButton;
	if(DoButton_Menu(&s_CancelButton, Localize("Cancel"), 0, &Button))
	{
		// The completion of the in-flight request is ignored because
		// AbortAccountFlow resets the request id it is filtered with.
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

	RenderAccountFlowError(&MainView);

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitMid(&Button, &Row, ROW_SPACING);
	static CButtonContainer s_LogoutAllButton;
	if(DoButton_Menu(&s_LogoutAllButton, Localize("Log out everywhere else"), 0, &Button))
	{
		StartAccountEmailEnter(EAccountFlow::LOGOUT_ALL);
	}
	static CButtonContainer s_LinkEmailButton;
	if(DoButton_Menu(&s_LinkEmailButton, Localize("Link another email"), 0, &Row))
	{
		StartAccountEmailEnter(EAccountFlow::LINK_EMAIL);
	}

	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	MainView.HSplitTop(ROW_SPACING, nullptr, &MainView);
	Row.VSplitMid(&Button, &Row, ROW_SPACING);
	static CButtonContainer s_UnlinkEmailButton;
	if(DoButton_Menu(&s_UnlinkEmailButton, Localize("Unlink email"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, AccountButtonColor(HasEmail)) && HasEmail)
	{
		StartAccountEmailEnter(EAccountFlow::UNLINK_EMAIL);
	}
	static CButtonContainer s_DeleteButton;
	if(DoButton_Menu(&s_DeleteButton, Localize("Delete account"), 0, &Row))
	{
		StartAccountEmailEnter(EAccountFlow::DELETE);
	}

	MainView.HSplitTop(3 * ROW_SPACING, nullptr, &MainView);
	MainView.HSplitTop(ROW_HEIGHT, &Row, &MainView);
	Row.VSplitLeft(BUTTON_WIDTH, &Button, nullptr);
	static CButtonContainer s_BackButton;
	if(DoButton_Menu(&s_BackButton, Localize("Back"), 0, &Button))
	{
		ResetAccountFlow(EAccountState::OVERVIEW);
	}
}

void CMenus::RenderAccount(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_SETTINGS_GENERAL);

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
		RenderAccountEmailEnter(MainView);
		break;
	case EAccountState::TOKEN_WAIT:
	case EAccountState::OP_WAIT:
	case EAccountState::INFO_WAIT:
		RenderAccountWait(MainView);
		break;
	case EAccountState::TOKEN_ENTER:
		RenderAccountTokenEnter(MainView);
		break;
	case EAccountState::INFO:
		RenderAccountInfo(MainView);
		break;
	}
}
