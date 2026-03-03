#include "hide_and_seek.h"

#include <base/str.h>

#include <engine/shared/config.h>

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

	m_CountingTime = 5;
	m_StartingTime = 10;
	m_RoundTime = 180;
	m_AbilityCoolDown = 10;
}

CGameControllerHideAndSeek::~CGameControllerHideAndSeek() = default;

void CGameControllerHideAndSeek::OnCharacterSpawn(class CCharacter *pChr)
{
	CGameControllerBasePvp::OnCharacterSpawn(pChr);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, false, -1);
	pChr->GiveWeapon(WEAPON_GUN, false, -1);
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
			m_StartStartingTick = Server()->Tick();
			m_GameState = STARTING;
		}
	}
	if(m_GameState == STARTING)
	{
		if(((Server()->Tick() - m_StartStartingTick) * Server()->TickSpeed()) >= m_StartingTime)
		{
			MakeRandomSeeker(1); //TODO: move to Config();

			KillAllPlayers();
			m_StartCountigTick = Server()->Tick();
			m_GameState = COUNTING;
		}
	}
	if(m_GameState == COUNTING)
	{
		CPlayer *pSeaker = GameServer()->m_apPlayers[m_SeekerId];
		if(pSeaker && pSeaker->GetCharacter() && pSeaker->GetCharacter()->m_FreezeTime <= 0)
		{
			pSeaker->GetCharacter()->SetDeepFrozen(true);
				
		}

		if((Server()->Tick() - m_StartCountigTick) / Server()->TickSpeed() >= m_CountingTime)
		{
			if(pSeaker && pSeaker->GetCharacter())
				pSeaker->GetCharacter()->SetDeepFrozen(false);
			m_StartRoundTick = Server()->Tick();
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
	{
		m_StartStartingTick = Server()->Tick();
		m_GameState = STARTING;
	}

	m_CurTime = Server()->Tick() / Server()->TickSpeed();

	if(IsTimeInterval(1))
	{
		if(m_GameState == WAITING)
			GameServer()->SendBroadcast("Waiting others                                                                                                        ", -1);
		int Seconds;
		switch(m_GameState)
		{
			case STARTING: 
				Seconds = m_StartingTime - (Server()->Tick() - m_StartStartingTick) / Server()->TickSpeed(); 
				break;
			case COUNTING: 
				Seconds = m_CountingTime - (Server()->Tick() - m_StartCountigTick) / Server()->TickSpeed(); 
				break;
			case RUNNING: 
				Seconds = m_RoundTime - (Server()->Tick() - m_StartRoundTick) / Server()->TickSpeed(); 
				break;
			default: return;
		}

		if(Seconds)
		{
			char Min[8], Sec[8], aTimer[128], aBuf[256];
			const int S = Seconds % 60;
			const int M = Seconds / 60;

			str_format(Sec, sizeof(Sec), (S < 10) ? "0%d" : "%d", S);
			str_format(Min, sizeof(Min), (M < 10) ? "0%d" : "%d", M);
			str_format(aTimer, sizeof(aTimer), "%s:%s                                                                                                        ", Min, Sec);
			switch(m_GameState)
			{
				case STARTING: str_format(aBuf, sizeof(aBuf), "Starting %s", aTimer); break;
				case COUNTING: str_format(aBuf, sizeof(aBuf), "Hide!! %s", aTimer); break;
				case RUNNING: str_format(aBuf, sizeof(aBuf), "Lost %s", aTimer); break;
				default: return;
			}
			GameServer()->SendBroadcast(aBuf, -1);
		}
	}
}

void CGameControllerHideAndSeek::StartRound()
{
}

void CGameControllerHideAndSeek::EndRound()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		if((Server()->Tick() - m_StartRoundTick) >= (m_RoundTime * Server()->TickSpeed()))
		{
			if(pPlayer->IsPlaying() && !pPlayer->m_Seeker)
				pPlayer->IncrementScore();
		}
		else
		{
			if(pPlayer->m_Seeker)
				pPlayer->IncrementScore();
		}
	}	

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
		if(pPlayer)
		{
			pPlayer->m_Seeker = false;
			pPlayer->m_LastUseAbilityTick = 0;
		}
}

bool CGameControllerHideAndSeek::DoEndRound()
{
	int CountLive = 0;
	for(const CPlayer *pPlayer : GameServer()->m_apPlayers)
		if(pPlayer && !pPlayer->IsPaused() && !pPlayer->m_Seeker)
			CountLive++;
	if(CountLive <= 0)
	{
		return true;
	}

	if((Server()->Tick() - m_StartRoundTick) >= (m_RoundTime * Server()->TickSpeed()))
	{
		return true;
	}

	return false;
}

void CGameControllerHideAndSeek::HidePlayers()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		SetSkin(pPlayer);

		if(pPlayer->m_HideTime > 0)
		{
			if(pPlayer->GetCharacter())
				pPlayer->GetCharacter()->SetCollisionDisabled(true);
			pPlayer->m_Hiden = true;
			pPlayer->m_HideTime--;
		}
		else 
		{
			if(pPlayer->GetCharacter())
				pPlayer->GetCharacter()->SetCollisionDisabled(false);
			pPlayer->m_Hiden = false;
		}

		for(CPlayer *pOtherPlayer : GameServer()->m_apPlayers)
		{
			if(!pOtherPlayer || pOtherPlayer->GetCid() == pPlayer->GetCid())
				continue;

			if(pOtherPlayer->GetCharacter() && pOtherPlayer->m_Seeker)
			{
				CPlayer *pHookedPlayer = GetPlayerOrNullptr(pOtherPlayer->GetCharacter()->HookedPlayer());
				if(pHookedPlayer && pHookedPlayer->GetCid() == pPlayer->GetCid())
				{
					pPlayer->GetCharacter()->SetCollisionDisabled(false);
					pPlayer->m_Hiden = false;
				}
			}
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

void CGameControllerHideAndSeek::SetSkin(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;

	if(pPlayer->m_Hiden)
	{
		pPlayer->m_SkinInfoManager.SetSkinName(ESkinPrio::HIGH, "ghost");
		return;
	}

	pPlayer->m_SkinInfoManager.UnsetAll(ESkinPrio::HIGH);
}

bool CGameControllerHideAndSeek::OnFireWeapon(CCharacter &Character, int &Weapon, vec2 &Direction, vec2 &MouseTarget, vec2 &ProjStartPos) 
{
	if(Weapon == WEAPON_GUN && Character.GetPlayer() && !Character.GetPlayer()->m_Seeker)
	{
		if(Character.GetPlayer() && Server()->Tick() - Character.GetPlayer()->m_LastUseAbilityTick < m_AbilityCoolDown * Server()->TickSpeed())
			return CGameControllerBasePvp::OnFireWeapon(Character, Weapon, Direction, MouseTarget, ProjStartPos);
		Character.GetPlayer()->m_HideTime = 3 * Server()->TickSpeed();
		GameServer()->CreateSound(Character.GetPos(), SOUND_PLAYER_PAIN_LONG);
		Character.GetPlayer()->m_LastUseAbilityTick = Server()->Tick();
	}

	if(Weapon == WEAPON_GUN && Character.GetPlayer() && Character.GetPlayer()->m_Seeker)
	{
		if(Character.GetPlayer() && Server()->Tick() - Character.GetPlayer()->m_LastUseAbilityTick < m_AbilityCoolDown * Server()->TickSpeed())
			return CGameControllerBasePvp::OnFireWeapon(Character, Weapon, Direction, MouseTarget, ProjStartPos);
		CPlayer *pClosest = nullptr;
		float MinDist = 999999.0f;
		vec2 Pos = Character.GetPos();

		for(CPlayer *p : GameServer()->m_apPlayers)
		{
			if(!p || p->m_Seeker || !p->IsPlaying() || !p->GetCharacter() || p->IsPaused())
				continue;
			
			float Dist = distance(p->GetCharacter()->GetPos(), Pos);
			if(Dist < MinDist)
			{
				MinDist = Dist;
				pClosest = p;
			}
		}

		if(pClosest)
		{
			GameServer()->CreatePlayerSpawn(pClosest->GetCharacter()->GetPos());
			GameServer()->CreateSound(pClosest->GetCharacter()->GetPos(), SOUND_NINJA_FIRE);
			Character.GetPlayer()->m_LastUseAbilityTick = Server()->Tick();
		}
	}

	return CGameControllerBasePvp::OnFireWeapon(Character, Weapon, Direction, MouseTarget, ProjStartPos);
}

bool CGameControllerHideAndSeek::SkipDamage(int Dmg, int From, int Weapon, const CCharacter *pCharacter, bool &ApplyForce) 
{
	const CPlayer *pPlayer = GetPlayerOrNullptr(From);
	if(m_GameState == RUNNING || m_GameState == COUNTING)
	{
		if(pPlayer && !pPlayer->m_Seeker && pCharacter->GetPlayer() && !pCharacter->GetPlayer()->m_Seeker && Weapon == WEAPON_HAMMER)
		{
			ApplyForce = true;
		}
	}
	return true;
}

bool CGameControllerHideAndSeek::OnCharacterTakeDamage(vec2 &Force, int &Dmg, int &From, int &Weapon, CCharacter &Character) 
{
	bool ApplyForce = false;

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
	return ApplyForce;
}


void CGameControllerHideAndSeek::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	if(pChr && pChr->GetPlayer() && GameServer()->Collision()->GetSwitchType(MapIndex) == TILE_FREEZE)
	{
		if(!pChr->GetPlayer()->m_Seeker)
		{
			if(pChr->GetPlayer()->m_HideTime < 5)
				pChr->GetPlayer()->m_HideTime = 2;
		}
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
		if(pPlayer && pPlayer->m_Seeker)
		{
			return pChr->GetPlayer()->m_Hiden;
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
