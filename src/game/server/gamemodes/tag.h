#ifndef GAME_SERVER_GAMEMODES_TAG_H
#define GAME_SERVER_GAMEMODES_TAG_H

#include <game/server/gamecontroller.h>

class CGameControllerTag : public IGameController
{
public:
	CGameControllerTag(class CGameContext *pGameServer);
	~CGameControllerTag();

	void OnPlayerConnect(class CPlayer *pPlayer) override;
	void OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason) override;
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterSpawn(class CCharacter *pChr) override;

	// void OnReset() override;

	// void OnNewRound();
	// void OnNewGame();

	void Tick() override;

	CGameTeams m_Teams;

	int m_RoundActive;
	
};
#endif // GAME_SERVER_GAMEMODES_TAG_H
