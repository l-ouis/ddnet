/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "emoticon.h"

#include "chat.h"

#include <algorithm>
#include <cmath>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/mapitems.h>

CEmoticon::CEmoticon()
{
	OnReset();
}

void CEmoticon::ConKeyEmoticon(IConsole::IResult *pResult, void *pUserData)
{
	CEmoticon *pSelf = (CEmoticon *)pUserData;

	if(pSelf->GameClient()->m_Scoreboard.IsActive())
		return;

	if(!pSelf->GameClient()->m_Snap.m_SpecInfo.m_Active && pSelf->Client()->State() != IClient::STATE_DEMOPLAYBACK)
		pSelf->m_Active = pResult->GetInteger(0) != 0;
}

void CEmoticon::ConEmote(IConsole::IResult *pResult, void *pUserData)
{
	((CEmoticon *)pUserData)->Emote(pResult->GetInteger(0));
}

void CEmoticon::OnConsoleInit()
{
	Console()->Register("+emote", "", CFGFLAG_CLIENT, ConKeyEmoticon, this, "Open emote selector");
	Console()->Register("emote", "i[emote-id]", CFGFLAG_CLIENT, ConEmote, this, "Use emote");
}

void CEmoticon::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_TilePickerWasActive = false;
	m_SelectedEmote = -1;
	m_SelectedEyeEmote = -1;
	m_TouchPressedOutside = false;
	m_TilePickerSelection = -1;
	m_TilePickerCursorMovement = vec2(0.0f, 0.0f);
}

void CEmoticon::OnRelease()
{
	m_Active = false;
}

bool CEmoticon::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);

	if(ShouldUseTilePicker())
	{
		m_TilePickerCursorMovement += vec2(x, y);
		const float MaxRadius = 170.0f;
		const float Dist = length(m_TilePickerCursorMovement);
		if(Dist > MaxRadius)
		{
			m_TilePickerCursorMovement = normalize(m_TilePickerCursorMovement) * MaxRadius;
		}
		return true;
	}

	m_SelectorMouse += vec2(x, y);
	return true;
}

bool CEmoticon::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		return true;
	}
	return false;
}

void CEmoticon::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const bool TilePickerActive = m_Active && ShouldUseTilePicker();

	if(!m_Active)
	{
		if(m_TouchPressedOutside)
		{
			m_SelectedEmote = -1;
			m_SelectedEyeEmote = -1;
			m_TouchPressedOutside = false;
		}

		if(m_TilePickerWasActive)
		{
			FinalizeTilePickerSelection();
			m_TilePickerWasActive = false;
			m_TilePickerCursorMovement = vec2(0.0f, 0.0f);
		}

		if(m_WasActive && m_SelectedEmote != -1)
			Emote(m_SelectedEmote);
		if(m_WasActive && m_SelectedEyeEmote != -1)
			EyeEmote(m_SelectedEyeEmote);
		m_WasActive = false;
		return;
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		m_Active = false;
		m_WasActive = false;
		m_TilePickerWasActive = false;
		m_TilePickerCursorMovement = vec2(0.0f, 0.0f);
		return;
	}

	if(TilePickerActive)
	{
		if(!m_TilePickerWasActive)
		{
			m_TilePickerCursorMovement = vec2(0.0f, 0.0f);
		}
		m_TilePickerWasActive = true;
		RenderTilePicker();
		return;
	}

	if(m_TilePickerWasActive)
	{
		FinalizeTilePickerSelection();
		m_TilePickerWasActive = false;
		m_TilePickerCursorMovement = vec2(0.0f, 0.0f);
	}

	m_WasActive = true;

	const CUIRect Screen = *Ui()->Screen();

	const bool WasTouchPressed = m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(m_TouchState);
	if(m_TouchState.m_AnyPressed)
	{
		const vec2 TouchPos = (m_TouchState.m_PrimaryPosition - vec2(0.5f, 0.5f)) * Screen.Size();
		const float TouchCenterDistance = length(TouchPos);
		if(TouchCenterDistance <= 170.0f)
		{
			m_SelectorMouse = TouchPos;
		}
		else if(TouchCenterDistance > 190.0f)
		{
			m_TouchPressedOutside = true;
		}
	}
	else if(WasTouchPressed)
	{
		m_Active = false;
		return;
	}

	if(length(m_SelectorMouse) > 170.0f)
		m_SelectorMouse = normalize(m_SelectorMouse) * 170.0f;

	float SelectedAngle = angle(m_SelectorMouse) + 2 * pi / 24;
	if(SelectedAngle < 0)
		SelectedAngle += 2 * pi;

	m_SelectedEmote = -1;
	m_SelectedEyeEmote = -1;
	if(length(m_SelectorMouse) > 110.0f)
		m_SelectedEmote = (int)(SelectedAngle / (2 * pi) * NUM_EMOTICONS);
	else if(length(m_SelectorMouse) > 40.0f)
		m_SelectedEyeEmote = (int)(SelectedAngle / (2 * pi) * NUM_EMOTES);

	const vec2 ScreenCenter = Screen.Center();

	Ui()->MapScreen();

	Graphics()->BlendNormal();

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0, 0, 0, 0.3f);
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, 190.0f, 64);
	Graphics()->QuadsEnd();

	Graphics()->WrapClamp();
	for(int Emote = 0; Emote < NUM_EMOTICONS; Emote++)
	{
		float Angle = 2 * pi * Emote / NUM_EMOTICONS;
		if(Angle > pi)
			Angle -= 2 * pi;

		Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[Emote]);
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
		Graphics()->QuadsBegin();
		const vec2 Nudge = direction(Angle) * 150.0f;
		const float Size = m_SelectedEmote == Emote ? 80.0f : 50.0f;
		IGraphics::CQuadItem QuadItem(ScreenCenter.x + Nudge.x, ScreenCenter.y + Nudge.y, Size, Size);
		Graphics()->QuadsDraw(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}
	Graphics()->WrapNormal();

	if(GameClient()->m_GameInfo.m_AllowEyeWheel && g_Config.m_ClEyeWheel && GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0, 1.0, 1.0, 0.3f);
		Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, 100.0f, 64);
		Graphics()->QuadsEnd();

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_RenderInfo;

		for(int Emote = 0; Emote < NUM_EMOTES; Emote++)
		{
			float Angle = 2 * pi * Emote / NUM_EMOTES;
			if(Angle > pi)
				Angle -= 2 * pi;

			const vec2 Nudge = direction(Angle) * 70.0f;
			TeeInfo.m_Size = m_SelectedEyeEmote == Emote ? 64.0f : 48.0f;
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Emote, vec2(-1, 0), ScreenCenter + Nudge);
		}

		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(0, 0, 0, 0.3f);
		Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, 30.0f, 64);
		Graphics()->QuadsEnd();
	}
	else
		m_SelectedEyeEmote = -1;

	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 24.0f);
}

bool CEmoticon::ShouldUseTilePicker() const
{
	return GameClient()->IsLocalTileToolEquipped();
}

void CEmoticon::RenderTilePicker()
{
	const CUIRect Screen = *Ui()->Screen();
	const bool WasTouchPressed = m_TouchState.m_AnyPressed;
	Ui()->UpdateTouchState(m_TouchState);
	if(!m_TouchState.m_AnyPressed && WasTouchPressed)
	{
		m_Active = false;
	}

	// Ui()->MapScreen();
	// Graphics()->BlendNormal();

	// Graphics()->TextureClear();
	// Graphics()->QuadsBegin();
	// Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.45f);
	// IGraphics::CQuadItem FadeBackground(Screen.x, Screen.y, Screen.w, Screen.h);
	// Graphics()->QuadsDrawTL(&FadeBackground, 1);
	// Graphics()->QuadsEnd();

	Ui()->MapScreen();

	const vec2 ScreenCenter = Screen.Center();
	Graphics()->BlendNormal();

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0, 0, 0, 0.3f);
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, 190.0f, 64);
	Graphics()->QuadsEnd();

	vec2 WheelCenter = Screen.Center();
	float aWorldRect[4] = {};
	if(GameClient()->m_Snap.m_pLocalCharacter)
	{
		Graphics()->MapScreenToWorld(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
			100.0f, 100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aWorldRect);
		const float WorldWidth = aWorldRect[2] - aWorldRect[0];
		const float WorldHeight = aWorldRect[3] - aWorldRect[1];
		if(WorldWidth > 0.0f && WorldHeight > 0.0f)
		{
			const float RelX = (GameClient()->m_LocalCharacterPos.x - aWorldRect[0]) / WorldWidth;
			const float RelY = (GameClient()->m_LocalCharacterPos.y - aWorldRect[1]) / WorldHeight;
			WheelCenter.x = Screen.x + std::clamp(RelX, 0.08f, 0.92f) * Screen.w;
			WheelCenter.y = Screen.y + std::clamp(RelY, 0.1f, 0.9f) * Screen.h;
		}
	}

	const auto &Palette = GameClient()->TileToolPalette();
	const int EntryCount = static_cast<int>(Palette.size());
	const vec2 CursorPos = WheelCenter + m_TilePickerCursorMovement;
	if(EntryCount == 0)
	{
		RenderTools()->RenderCursor(CursorPos, 24.0f);
		return;
	}

	const float CircleRadius = 150.0f;
	const float ActivationRadius = 110.0f;
	const float IconBaseSize = std::min(32.0f * 1.0f, CircleRadius * 0.9f);
	const float HoverDetectRadius = IconBaseSize * 0.55f;
	const float StartAngle = -pi / 2.0f;
	const float AngleStep = 2.0f * pi / EntryCount;

	const auto IconCenterForIndex = [&](int Index) {
		const float Angle = StartAngle + Index * AngleStep;
		return WheelCenter + direction(Angle) * CircleRadius;
	};

	const vec2 CursorVector = CursorPos - WheelCenter;
	const float CursorLength = length(CursorVector);
	int AngularSelection = -1;
	if(CursorLength > ActivationRadius)
	{
		float CursorAngle = angle(CursorVector);
		if(CursorAngle < 0.0f)
			CursorAngle += 2.0f * pi;
		float AngleFromStart = CursorAngle - StartAngle;
		while(AngleFromStart < 0.0f)
			AngleFromStart += 2.0f * pi;
		while(AngleFromStart >= 2.0f * pi)
			AngleFromStart -= 2.0f * pi;
		float CenteredAngle = AngleFromStart + AngleStep * 0.5f;
		while(CenteredAngle >= 2.0f * pi)
			CenteredAngle -= 2.0f * pi;
		AngularSelection = static_cast<int>(CenteredAngle / AngleStep) % EntryCount;
	}

	int HoverSelection = -1;
	float BestHoverDist = 0.0f;
	for(int Index = 0; Index < EntryCount; ++Index)
	{
		const vec2 IconCenter = IconCenterForIndex(Index);
		const float Dist = distance(CursorPos, IconCenter);
		if(Dist <= HoverDetectRadius && (HoverSelection == -1 || Dist < BestHoverDist))
		{
			HoverSelection = Index;
			BestHoverDist = Dist;
		}
	}

	const int ActiveSelection = HoverSelection != -1 ? HoverSelection : AngularSelection;
	m_TilePickerSelection = ActiveSelection;

	const IGraphics::CTextureHandle EntitiesTexture = GameClient()->m_MapImages.GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_ALL_EXCEPT_SWITCH);
	const int SelectedIndex = GameClient()->TileToolSelectionIndex(g_Config.m_ClDummy);

	for(int Index = 0; Index < EntryCount; ++Index)
	{
		const vec2 IconCenter = IconCenterForIndex(Index);
		const bool IsHover = ActiveSelection >= 0 && Index == ActiveSelection;
		const bool IsCurrentSelection = Index == SelectedIndex;
		const float IconScale = IsHover ? 1.15f : 1.0f;
		const float IconSize = IconBaseSize * IconScale;
		const float BackgroundRadius = IconSize * 0.55f;

		// Graphics()->TextureClear();
		// Graphics()->QuadsBegin();
		// const ColorRGBA OutlineColor = IsHover ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f) : (IsCurrentSelection ? ColorRGBA(1.0f, 0.82f, 0.35f, 0.85f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.28f));
		// Graphics()->SetColor(OutlineColor.r, OutlineColor.g, OutlineColor.b, OutlineColor.a);
		// Graphics()->DrawCircle(IconCenter.x, IconCenter.y, BackgroundRadius, 40);
		// Graphics()->QuadsEnd();

		const CGameClient::STileToolPaletteEntry &Entry = Palette[Index];
		const CGameClient::STileToolLayer GameLayer = GameClient()->TileToolLayerForEntry(Index, false);
		Graphics()->TextureSet(EntitiesTexture);
		Graphics()->BlendNormal();
		RenderMap()->RenderTile(IconCenter.x - IconSize / 2.0f, IconCenter.y - IconSize / 2.0f, static_cast<unsigned char>(GameLayer.m_Index), IconSize, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		if(Entry.m_SetFrontLayer)
		{
			const CGameClient::STileToolLayer FrontLayer = GameClient()->TileToolLayerForEntry(Index, true);
			Graphics()->TextureSet(EntitiesTexture);
			Graphics()->BlendNormal();
			RenderMap()->RenderTile(IconCenter.x - IconSize / 2.0f, IconCenter.y - IconSize / 2.0f, static_cast<unsigned char>(FrontLayer.m_Index), IconSize, ColorRGBA(1.0f, 1.0f, 1.0f, 0.65f));
		}

		vec2 LabelDir = IconCenter - WheelCenter;
		const float LabelDirLength = length(LabelDir);
		if(LabelDirLength > 0.001f)
		{
			LabelDir /= LabelDirLength;
		}
		else
		{
			LabelDir = vec2(0.0f, -1.0f);
		}
		const vec2 LabelAnchor = WheelCenter + LabelDir * (CircleRadius + IconBaseSize * 0.75f);
		CUIRect LabelRect;
		LabelRect.w = 150.0f;
		LabelRect.h = 16.0f;
		LabelRect.x = LabelAnchor.x - LabelRect.w / 2.0f;
		LabelRect.y = LabelAnchor.y - LabelRect.h / 2.0f;
		SLabelProperties LabelProps;
		LabelProps.m_MaxWidth = LabelRect.w;
		Ui()->DoLabel(&LabelRect, Entry.m_pLabel, 12.0f, TEXTALIGN_MC, LabelProps);
	}

	RenderTools()->RenderCursor(CursorPos, 32.0f);
}

void CEmoticon::FinalizeTilePickerSelection()
{
	if(m_TilePickerSelection >= 0)
	{
		GameClient()->SetTileToolSelectionIndex(m_TilePickerSelection, g_Config.m_ClDummy);
	}
	m_TilePickerSelection = -1;
}

void CEmoticon::Emote(int Emoticon)
{
	CNetMsg_Cl_Emoticon Msg;
	Msg.m_Emoticon = Emoticon;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);

	if(g_Config.m_ClDummyCopyMoves)
	{
		CMsgPacker MsgDummy(NETMSGTYPE_CL_EMOTICON, false);
		MsgDummy.AddInt(Emoticon);
		Client()->SendMsg(!g_Config.m_ClDummy, &MsgDummy, MSGFLAG_VITAL);
	}
}

void CEmoticon::EyeEmote(int Emote)
{
	char aBuf[32];
	switch(Emote)
	{
	case EMOTE_NORMAL:
		str_format(aBuf, sizeof(aBuf), "/emote normal %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_PAIN:
		str_format(aBuf, sizeof(aBuf), "/emote pain %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_HAPPY:
		str_format(aBuf, sizeof(aBuf), "/emote happy %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_SURPRISE:
		str_format(aBuf, sizeof(aBuf), "/emote surprise %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_ANGRY:
		str_format(aBuf, sizeof(aBuf), "/emote angry %d", g_Config.m_ClEyeDuration);
		break;
	case EMOTE_BLINK:
		str_format(aBuf, sizeof(aBuf), "/emote blink %d", g_Config.m_ClEyeDuration);
		break;
	}
	GameClient()->m_Chat.SendChat(0, aBuf);
}
