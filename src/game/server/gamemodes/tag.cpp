#include "tag.h"
#include "game/server/player.h"
#include "game/server/entities/character.h"

// Exchange this to a string that identifies your game mode.
// DM, TDM and CTF are reserved for teeworlds original modes.
// DDraceNetwork and TestDDraceNetwork are used by DDNet.
#define GAME_TYPE_NAME "Tag"
#define TEST_TYPE_NAME "TestTag"

CGameControllerTag::CGameControllerTag(class CGameContext *pGameServer) :
	IGameController(pGameServer), m_Teams(pGameServer)
{
	m_pGameType = g_Config.m_SvTestingCommands ? TEST_TYPE_NAME : GAME_TYPE_NAME;
}

CGameControllerTag::~CGameControllerTag() = default;

void CGameControllerTag::OnPlayerConnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerConnect(pPlayer);
	int ClientId = pPlayer->GetCid();
	DoTeamChange(pPlayer, TEAM_SPECTATORS, 0);

	// init the player
	// Score()->PlayerData(ClientId)->Reset();

	// Can't set score here as LoadScore() is threaded, run it in
	// LoadScoreThreaded() instead
	// Score()->LoadPlayerData(ClientId);

	if(!Server()->ClientPrevIngame(ClientId))
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientId), GetTeamName(pPlayer->GetTeam()));
		GameServer()->SendChat(-1, TEAM_ALL, aBuf, -1, CGameContext::FLAG_SIX);
	}
}

void CGameControllerTag::OnPlayerDisconnect(CPlayer *pPlayer, const char *pReason)
{
	IGameController::OnPlayerDisconnect(pPlayer, pReason);
}

void CGameControllerTag::DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	CCharacter *pCharacter = pPlayer->GetCharacter();


	if(m_Teams.Count(Team) >= 2)
	{
		dbg_msg("tag", "%d", Team);
		dbg_msg("tag", "%d", m_Teams.Count(Team));
		return;
	}
	if(Team == TEAM_SPECTATORS)
	{
		if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO && pCharacter)
		{
			int DDRTeam = pCharacter->Team();
			Teams().SetForceCharacterTeam(pPlayer->GetCid(), TEAM_FLOCK);
			Teams().CheckTeamFinished(DDRTeam);
		}
	}

	IGameController::DoTeamChange(pPlayer, Team, DoChatMsg);
}

int CGameControllerTag::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// add one to the score of the killer
	if(pKiller)
	{
		if(pKiller->m_Score)
		{
			(*pKiller->m_Score)++;
		}
		GameServer()->SendBroadcast("You killed someone", pKiller->GetCid());

	}

	return 0;
}

void CGameControllerTag::OnCharacterSpawn(class CCharacter *pChr)
{
	int ClientId = pChr->GetPlayer()->GetCid();
	IGameController::OnCharacterSpawn(pChr);
	pChr->SetTeams(&m_Teams);
	m_Teams.OnCharacterSpawn(ClientId);

	pChr->SetWeapon(WEAPON_HAMMER);
}

void CGameControllerTag::Tick()
{
	// this is the main part of the gamemode, this function is run every tick

	IGameController::Tick();
}
