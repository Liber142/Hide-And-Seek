#include "hide_and_seek.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <insta/server/entities/ddnet_pvp/vanilla_projectile.h>

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

	m_StartingTime = 10;
}

CGameControllerHideAndSeek::~CGameControllerHideAndSeek() = default;

void CGameControllerHideAndSeek::OnCharacterSpawn(class CCharacter *pChr)
{
	CGameControllerBasePvp::OnCharacterSpawn(pChr);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, false, -1);
	pChr->GiveWeapon(WEAPON_GUN, false, -1);
	if(m_GameState == RUNNING)
	{
		pChr->Pause(true);
		if(pChr->GetPlayer())
			pChr->GetPlayer()->Pause(CPlayer::PAUSE_SPEC, true);
	}
}

void CGameControllerHideAndSeek::OnInit()
{
}

void CGameControllerHideAndSeek::Tick()
{
	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		SetSkin(pPlayer);

		if(!pPlayer->GetCharacter())
			continue;

		const float TotalTicks = Server()->TickSpeed() * Config()->m_SvAbilityCoolDown;
		const int PassedTicks = Server()->Tick() - pPlayer->m_LastUseAbilityTick;
		const int Armor = static_cast<int>((PassedTicks / TotalTicks) * 10.0f);
		pPlayer->GetCharacter()->SetArmor(Armor);
	}

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
			MakeRandomSeeker(Config()->m_SvNumSeekers);

			KillAllPlayers();
			m_StartCountigTick = Server()->Tick();
			m_GameState = COUNTING;
		}
	}
	if(m_GameState == COUNTING)
	{
		for(const int &SeekerId : m_vSeekerIds)
		{
			CPlayer *pSeaker = GameServer()->m_apPlayers[SeekerId];
			if(pSeaker && pSeaker->GetCharacter())
			{
				pSeaker->GetCharacter()->SetDeepFrozen(true);
					
			}
		}

		HidePlayers();

		if((Server()->Tick() - m_StartCountigTick) / Server()->TickSpeed() >= Config()->m_SvCountingTime)
		{
			for(const int &SeekerId : m_vSeekerIds)
			{
				CPlayer *pSeaker = GameServer()->m_apPlayers[SeekerId];
				if(pSeaker && pSeaker->GetCharacter())
					pSeaker->GetCharacter()->SetDeepFrozen(false);
			}
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
		m_GameState = WAITING;
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
				Seconds = Config()->m_SvCountingTime - (Server()->Tick() - m_StartCountigTick) / Server()->TickSpeed(); 
				break;
			case RUNNING: 
				Seconds = Config()->m_SvRoundTime - (Server()->Tick() - m_StartRoundTick) / Server()->TickSpeed(); 
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

		if((Server()->Tick() - m_StartRoundTick) >= (Config()->m_SvRoundTime * Server()->TickSpeed()))
		{
			if(pPlayer->IsPlaying() && !pPlayer->m_Seeker && !pPlayer->IsPaused() && !pPlayer->m_IsDead)
				pPlayer->IncrementScore();
		}
	}	

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
		if(pPlayer)
		{
			pPlayer->m_Seeker = false;
			pPlayer->m_LastUseAbilityTick = 0;
		} 
	m_vSeekerIds.clear();
}

bool CGameControllerHideAndSeek::DoEndRound()
{
	int CountLive = 0;
	int CountSeekers = 0;
	for(const CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer)
			continue;

		if(!pPlayer->IsPaused() && !pPlayer->m_Seeker && pPlayer->GetTeam() != TEAM_SPECTATORS)
		{
			CountLive++;
		}
		if(!pPlayer->IsPaused() && pPlayer->m_Seeker && pPlayer->GetTeam() != TEAM_SPECTATORS)
		{
			CountSeekers++;
		}
	}
		
	if(CountLive <= 0 || CountSeekers <= 0)
	{
		return true;
	}

	if((Server()->Tick() - m_StartRoundTick) >= (Config()->m_SvRoundTime * Server()->TickSpeed()))
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
		pPlayer->m_SkinInfoManager.SetSkinName(ESkinPrio::HIGH, Config()->m_SvHidenSkin);
		return;
	}

	if(pPlayer->m_Seeker)
	{
		pPlayer->m_SkinInfoManager.SetSkinName(ESkinPrio::HIGH, Config()->m_SvSeekerSkin);
		return;
	}

	char aBuf[16];
	pPlayer->m_SkinInfoManager.SkinName(aBuf, sizeof(aBuf));
	if(!str_comp(aBuf, Config()->m_SvSeekerSkin) || !str_comp(aBuf, Config()->m_SvHidenSkin))
	{
		pPlayer->m_SkinInfoManager.SetSkinName(ESkinPrio::HIGH, "default");
		return;
	}

	pPlayer->m_SkinInfoManager.UnsetAll(ESkinPrio::HIGH);
}

bool CGameControllerHideAndSeek::OnFireWeapon(CCharacter &Character, int &Weapon, vec2 &Direction, vec2 &MouseTarget, vec2 &ProjStartPos) 
{
	if(Weapon == WEAPON_GUN && Character.GetPlayer() && !Character.GetPlayer()->m_Seeker)
	{
		if(Character.GetPlayer() && Server()->Tick() - Character.GetPlayer()->m_LastUseAbilityTick < Config()->m_SvAbilityCoolDown * Server()->TickSpeed())
			return CGameControllerBasePvp::OnFireWeapon(Character, Weapon, Direction, MouseTarget, ProjStartPos);
		Character.GetPlayer()->m_HideTime = 3 * Server()->TickSpeed();
		GameServer()->CreateSound(Character.GetPos(), SOUND_PLAYER_PAIN_LONG);
		Character.GetPlayer()->m_LastUseAbilityTick = Server()->Tick();
		return true;
	}

	if(Weapon == WEAPON_GUN && Character.GetPlayer() && Character.GetPlayer()->m_Seeker)
	{
		if(Character.GetPlayer() && Server()->Tick() - Character.GetPlayer()->m_LastUseAbilityTick < Config()->m_SvAbilityCoolDown * Server()->TickSpeed())
			return CGameControllerBasePvp::OnFireWeapon(Character, Weapon, Direction, MouseTarget, ProjStartPos);
		SeekerAbility(Character.GetPlayer());
		return true;
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

	CPlayer *pPlayer = GetPlayerOrNullptr(From);
	if(Weapon == WEAPON_HAMMER && pPlayer && pPlayer->m_Seeker && Character.GetPlayer())
	{
		if(!Character.IsPaused())
		{
			pPlayer->IncrementScore();
			Character.Pause(true);
			Character.GetPlayer()->Pause(CPlayer::PAUSE_SPEC, true);
		}
	}
	if(Weapon == WEAPON_SHOTGUN && pPlayer && pPlayer->m_Seeker && Character.GetPlayer())
	{
		Character.Freeze(1);
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
		if(m_GameState == COUNTING && pPlayer && pPlayer->m_Seeker && pChr->GetPlayer() && !pChr->GetPlayer()->m_Seeker)
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

void CGameControllerHideAndSeek::SeekerAbility(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	CPlayer *pClosest = nullptr;
	float MinDist = 999999.0f;
	vec2 Pos = pChr->GetPos();

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
		
		vec2 TargetPos = pClosest->GetCharacter()->GetPos();
		vec2 NewDirection = normalize(TargetPos - pChr->GetPos());
		
		new CVanillaProjectile(
			pChr->GameWorld(),
			WEAPON_SHOTGUN, //Type
			pPlayer->GetCid(), //Owner
			pChr->GetPos(), //Pos
			NewDirection, //Dir
			999, //Span
			false, //Freeze
			false, //Explosive
			-1, //SoundImpact
			NewDirection //InitDir
		);
		
		pChr->GetPlayer()->m_LastUseAbilityTick = Server()->Tick();
	}
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
		m_vSeekerIds.emplace_back(aPlayingIds[i]);
	}
}

REGISTER_GAMEMODE(hide_and_seek, CGameControllerHideAndSeek(pGameServer));
