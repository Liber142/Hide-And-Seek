#ifndef GAME_SERVER_GAMEMODES_HIDE_AND_SEEK_HIDE_AND_SEEK_H
#define GAME_SERVER_GAMEMODES_HIDE_AND_SEEK_HIDE_AND_SEEK_H

#include <insta/server/gamemodes/base_pvp/base_pvp.h>

#include <random>

class CGameControllerHideAndSeek : public CGameControllerBasePvp
{
public:
	enum EGameState : int
	{
		WAITING,
		STARTING,
		COUNTING,
		RUNNING,
		ENDING,
		NUM_STATES
	} m_GameState;

	CGameControllerHideAndSeek(CGameContext *pGameServer);
	~CGameControllerHideAndSeek() override;

	void OnInit() override;
	void Tick() override;
	void OnCharacterSpawn(class CCharacter *pChr) override;
	bool SkipDamage(int Dmg, int From, int Weapon, const CCharacter *pCharacter, bool &ApplyForce) override;
	bool OnCharacterTakeDamage(vec2 &Force, int &Dmg, int &From, int &Weapon, CCharacter &Character) override;
	bool OnFireWeapon(CCharacter &Character, int &Weapon, vec2 &Direction, vec2 &MouseTarget, vec2 &ProjStartPos) override;
	void HandleCharacterTiles(class CCharacter *pChr, int MapIndex) override;
	bool ForceNetworkClipping(const CEntity *pEntity, int SnappingClient, vec2 CheckPos) override;
private:
	int m_AbilityCoolDown;

	int m_CurTime;

	int m_SeekerId = -1; //TODO: In future we need one more seekers
	
	int m_StartStartingTick;
	int m_StartingTime; // TODO: Move in to Config();
	
	int m_StartCountigTick;
	int m_CountingTime; // TODO: Move in to Config();
	
	int m_StartRoundTick;
	int m_RoundTime; //TODO: Move in to Config();
	void StartRound();
	void EndRound();
	bool DoEndRound();

	void KillAllPlayers();

	void HidePlayers();
	void MakeRandomSeeker(int Count);
	
	void SetSkin(class CPlayer *pPlayer);

	bool IsTimeInterval(const int Seconds) const { return Server()->Tick() % (Server()->TickSpeed() * Seconds) == 0; }
	static std::mt19937 M_S_RANDOM_ENGINE;
};
#endif
