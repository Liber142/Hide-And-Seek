#include "hide_and_seek.h"

#include <base/str.h>

#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <random>

std::mt19937 CGameControllerHideAndSeek::M_S_RANDOM_ENGINE(std::random_device{}());

CGameControllerHideAndSeek::CGameControllerHideAndSeek(CGameContext *pGameServer) :
	CGameControllerBasePvp(pGameServer)
{
	// if you do not need team red/blue or the red and blue flag from ctf
	// just do m_GameFlags = 0;
	m_GameFlags = 0;
	m_pGameType = "h&s";
	m_DefaultWeapon = WEAPON_GUN;

	m_GameState = WAITING;
	
	m_pStatsTable = "hide_and_seek";
	m_pExtraColumns = nullptr; // new CHideAndSeekColumns();
	m_pSqlStats->SetExtraColumns(m_pExtraColumns);
	m_pSqlStats->CreateTable(m_pStatsTable);

	m_CountingTime = 20 * SERVER_TICK_SPEED;
	m_WaitingTime = 10;
}

CGameControllerHideAndSeek::~CGameControllerHideAndSeek() = default;

void CGameControllerHideAndSeek::OnCharacterSpawn(class CCharacter *pChr)
{
	CGameControllerBasePvp::OnCharacterSpawn(pChr);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, false, -1);
	pChr->GiveWeapon(WEAPON_GUN, false, 10);
}

void CGameControllerHideAndSeek::OnInit()
{
}

void CGameControllerHideAndSeek::Tick()
{
	if(m_GameState == WAITING)
	{
		int CountPlayer = 0;
		for(const CPlayer *pPlayer : GameServer()->m_apPlayers)
			if(pPlayer && pPlayer->IsPlaying())
				CountPlayer++;

		if(CountPlayer > 1)
		{
			if(m_StartWaitingTick == 0)
				m_StartWaitingTick = Server()->Tick();
			if((Server()->Tick() - m_StartWaitingTick) * Server()->TickSpeed() >= m_WaitingTime)
			{
				MakeRandomSeeker(1); //TODO: move to Config();

				KillAllPlayers();
				m_StartCountigTick = Server()->Tick();
				m_GameState = COUNTING;
			}
		}
	}
	if(m_GameState == COUNTING)
	{
		CPlayer *pSeaker = GameServer()->m_apPlayers[m_SeekerId];
		if(pSeaker && pSeaker->GetCharacter() && pSeaker->GetCharacter()->m_FreezeTime <= 0)
		{
			pSeaker->GetCharacter()->SetDeepFrozen(true);
		}
		if((Server()->Tick() - m_StartCountigTick) >= m_CountingTime)
		{
			if(pSeaker && pSeaker->GetCharacter())
				pSeaker->GetCharacter()->SetDeepFrozen(false);
			m_GameState = RUNNING;
		}
	}
	if(m_GameState == RUNNING)
	{
		HidePlayers();

		if(DoEndRound())
		{
			EndRound();
			m_GameState = ENDING;
		}
	}
	if(m_GameState == ENDING)
		m_GameState = WAITING;
}

void CGameControllerHideAndSeek::StartRound()
{
}

void CGameControllerHideAndSeek::EndRound()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
		if(pPlayer)
			pPlayer->m_Seeker = false;
}

bool CGameControllerHideAndSeek::DoEndRound()
{
	int CountLive = 0;
	for(const CPlayer *pPlayer : GameServer()->m_apPlayers)
		if(pPlayer && !pPlayer->IsPaused() && !pPlayer->m_Seeker)
			CountLive++;
	if(CountLive <= 0)
		return true;

	return false;
}

void CGameControllerHideAndSeek::HidePlayers()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		if(pPlayer->m_Hide > 0)
		{
			pPlayer->m_Hide--;
		}
		else if(pPlayer->GetCharacter())
		{
	 		pPlayer->GetCharacter()->SetCollisionDisabled(false);
		}
	}
}

void CGameControllerHideAndSeek::KillAllPlayers()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		if(pPlayer->IsPaused())
			pPlayer->Pause(CPlayer::PAUSE_NONE, true);

		pPlayer->KillCharacter();
	}
}

bool CGameControllerHideAndSeek::SkipDamage(int Dmg, int From, int Weapon, const CCharacter *pCharacter, bool &ApplyForce) 
{
	return true;
}

bool CGameControllerHideAndSeek::OnCharacterTakeDamage(vec2 &Force, int &Dmg, int &From, int &Weapon, CCharacter &Character) 
{
	bool ApplyForce = true;
	if(SkipDamage(Dmg, From, Weapon, &Character, ApplyForce))
	{
		Dmg = 0;
	}

	const CPlayer *pPlayer = GetPlayerOrNullptr(From);
	if(Weapon == WEAPON_HAMMER && pPlayer && pPlayer->m_Seeker && Character.GetPlayer())
	{
		if(!Character.IsPaused())
		{
			Character.Pause(true);
			Character.GetPlayer()->Pause(CPlayer::PAUSE_SPEC, true);
		}
	}

	return false;
}

void CGameControllerHideAndSeek::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	if(pChr && pChr->GetPlayer() && GameServer()->Collision()->GetSwitchType(MapIndex) == TILE_FREEZE)
	{
		if(!pChr->GetPlayer()->m_Seeker)
		{
			if(str_comp(Server()->ClientName(pChr->GetPlayer()->GetCid()), "Markus777777"))
				pChr->GetPlayer()->m_Hide = 2;
		}
		pChr->SetCollisionDisabled(true);
	}
}

bool CGameControllerHideAndSeek::ForceNetworkClipping(const CEntity *pEntity, int SnappingClient, vec2 CheckPos) 
{
    const CCharacter *pChr = dynamic_cast<const CCharacter*>(pEntity);
    if(pChr && pChr->GetPlayer() && pChr->GetPlayer()->GetCid() != SnappingClient)
	{
		const CPlayer *pPlayer = GameServer()->m_apPlayers[SnappingClient];
		if(m_GameState == COUNTING && pPlayer && pPlayer->m_Seeker)
		{
			return true;	
		}
		if(pPlayer && pPlayer->m_Seeker && pPlayer->GetCharacter() && pPlayer->GetCharacter()->HookedPlayer() != pChr->GetPlayer()->GetCid())
		{
			return pChr->GetPlayer()->m_Hide > 0;
		}
	}

	return CGameControllerBasePvp::ForceNetworkClipping(pEntity, SnappingClient, CheckPos);
}

void CGameControllerHideAndSeek::MakeRandomSeeker(int Count)
{
	int aPlayingIds[MAX_CLIENTS] = {-1};
	int Players = 0;

	for(const auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		if(!pPlayer->m_IsDead)
			aPlayingIds[Players++] = pPlayer->GetCid();
	}

	std::shuffle(aPlayingIds, aPlayingIds + Players, M_S_RANDOM_ENGINE);
	if(Count > Players)
		Count = Players;

	for(int i = 0; i < Count; i++)
	{
		GameServer()->m_apPlayers[aPlayingIds[i]]->m_Seeker = true;
		m_SeekerId = aPlayingIds[i];
	}
}

REGISTER_GAMEMODE(hide_and_seek, CGameControllerHideAndSeek(pGameServer));
