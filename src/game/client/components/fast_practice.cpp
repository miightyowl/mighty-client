#include "fast_practice.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/components/camera.h>
#include <game/client/components/effects.h>
#include <game/client/components/freezebars.h>
#include <game/client/components/players.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/laser_data.h>
#include <game/client/pickup_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/door.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/pickup.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/collision.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>

void CFastPractice::ConFastPractice(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFastPractice *>(pUserData)->Toggle();
}

void CFastPractice::ConFastPracticeCheckpoint(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CFastPractice *>(pUserData)->SetCheckpoint();
}

void CFastPractice::OnConsoleInit()
{
	Console()->Register("fast_practice", "", CFGFLAG_CLIENT, ConFastPractice, this, "M-Client: play a part without moving your real tee");
	Console()->Register("fast_practice_checkpoint", "", CFGFLAG_CLIENT, ConFastPracticeCheckpoint, this, "M-Client: move the fast practice checkpoint to the practice tee");
}

void CFastPractice::Notify(const char *pMessage)
{
	if(Client()->State() == IClient::STATE_ONLINE)
		GameClient()->Echo(pMessage);
}

void CFastPractice::OnReset()
{
	m_Active = false;
	m_ClientId = -1;
	m_LastTick = 0;
	m_CheckpointTick = 0;
	m_World.Clear();
	m_RenderWorld.Clear();
	mem_zero(&m_FrozenInput, sizeof(m_FrozenInput));
}

void CFastPractice::OnMapLoad()
{
	OnReset();
}

bool CFastPractice::CanRun() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	if(!GameClient()->Predict())
		return false;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return false;

	const int ClientId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		return false;
	return GameClient()->m_PredictedWorld.GetCharacterById(ClientId) != nullptr;
}

bool CFastPractice::HasMoved() const
{
	return distance(m_Core.m_Pos, m_CheckpointCore.m_Pos) > 16.0f;
}

void CFastPractice::Toggle()
{
	if(!m_Active)
	{
		Start();
		return;
	}

	if(HasMoved())
		Reset();
	else
		Stop();
}

void CFastPractice::Start()
{
	if(!CanRun())
	{
		Notify(Localize("Fast practice needs your own tee to be alive and prediction to be on."));
		return;
	}

	m_ClientId = GameClient()->m_Snap.m_LocalClientId;

	const CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(m_ClientId);
	m_CheckpointCore = *pChar->Core();
	ClearFreeze(&m_CheckpointCore);
	m_CheckpointTick = GameClient()->m_PredictedWorld.GameTick();
	m_CheckpointTele = GameClient()->m_Snap.m_aCharacters[m_ClientId].m_HasExtendedData ?
				   GameClient()->m_Snap.m_aCharacters[m_ClientId].m_ExtendedData.m_TeleCheckpoint :
				   pChar->m_TeleCheckpoint;

	if(!Respawn())
	{
		m_ClientId = -1;
		return;
	}

	m_FrozenInput = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
	m_FrozenInput.m_Direction = 0;
	m_FrozenInput.m_Jump = 0;
	m_FrozenInput.m_Hook = 0;
	if((m_FrozenInput.m_Fire & 1) != 0)
		m_FrozenInput.m_Fire++;
	if(m_FrozenInput.m_TargetX == 0 && m_FrozenInput.m_TargetY == 0)
		m_FrozenInput.m_TargetY = -1;

	m_Active = true;
}

void CFastPractice::Stop()
{
	if(!m_Active)
		return;

	m_Active = false;

	if(m_ClientId >= 0)
	{
		CNetObj_PlayerInput &Live = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		Live.m_Fire = ((m_FrozenInput.m_Fire & INPUT_STATE_MASK) + (Live.m_Fire & 1)) & INPUT_STATE_MASK;
		Live.m_NextWeapon = m_FrozenInput.m_NextWeapon;
		Live.m_PrevWeapon = m_FrozenInput.m_PrevWeapon;
		Live.m_WantedWeapon = m_FrozenInput.m_WantedWeapon;
		GameClient()->m_Controls.m_aLastData[g_Config.m_ClDummy] = Live;
	}

	m_World.Clear();
	m_RenderWorld.Clear();

	if(m_ClientId >= 0)
	{
		if(const CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(m_ClientId))
		{
			GameClient()->m_aClients[m_ClientId].m_Predicted = *pChar->Core();
			GameClient()->m_aClients[m_ClientId].m_PrevPredicted = *pChar->Core();
			GameClient()->m_aClients[m_ClientId].m_RegularPredicted = *pChar->Core();
			GameClient()->m_PredictedChar = *pChar->Core();
			GameClient()->m_PredictedPrevChar = *pChar->Core();
		}
	}
	m_ClientId = -1;
}

void CFastPractice::Reset()
{
	if(!m_Active)
		return;

	Respawn();
}

void CFastPractice::SetCheckpoint()
{
	if(!m_Active)
		return;
	m_CheckpointCore = m_Core;
	ClearFreeze(&m_CheckpointCore);
	m_CheckpointTick = m_World.GameTick();
	if(const CCharacter *pChar = m_World.GetCharacterById(m_ClientId))
		m_CheckpointTele = pChar->m_TeleCheckpoint;
	SetRespawn(m_ClientId, m_CheckpointCore, m_CheckpointTele, m_CheckpointTick);
	Notify(Localize("Fast practice checkpoint moved."));
}

bool CFastPractice::Respawn()
{
	if(!CanRun())
	{
		Stop();
		return false;
	}

	m_World.CopyWorldClean(&GameClient()->m_PredictedWorld);

	m_World.m_LocalClientId = m_ClientId;

	m_World.m_WorldConfig.m_PredictWeapons = true;
	m_World.m_WorldConfig.m_PredictEvents = true;
	if(m_World.m_WorldConfig.m_PredictFreeze != 1)
		m_World.m_WorldConfig.m_PredictFreeze = 1;

	CEntity *pNext = nullptr;
	for(CEntity *pEnt = m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pEnt; pEnt = pNext)
	{
		pNext = pEnt->TypeNext();
		if(((CProjectile *)pEnt)->GetOwner() != m_ClientId)
			pEnt->Destroy();
	}
	for(CEntity *pEnt = m_World.FindFirst(CGameWorld::ENTTYPE_LASER); pEnt; pEnt = pNext)
	{
		pNext = pEnt->TypeNext();
		if(((CLaser *)pEnt)->GetOwner() != m_ClientId)
			pEnt->Destroy();
	}

	CCharacter *pChar = m_World.GetCharacterById(m_ClientId);
	if(!pChar)
	{
		Stop();
		return false;
	}

	CCharacterCore Core = m_CheckpointCore;
	RebaseCore(&Core, m_World.GameTick() - m_CheckpointTick);

	pChar->SetCore(Core);
	pChar->SetCoreWorld(&m_World);
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
	pChar->m_FreezeTime = Core.m_FreezeEnd > 0 ? std::max(1, Core.m_FreezeEnd - m_World.GameTick()) : 0;
	pChar->m_FrozenLastTick = pChar->m_FreezeTime != 0;
	pChar->m_TeleCheckpoint = m_CheckpointTele;
	pChar->ResetInput();

	SetRespawn(m_ClientId, Core, m_CheckpointTele, m_World.GameTick());
	NeutraliseOtherTees();
	SpawnMapEntities();

	m_RenderWorld.CopyWorldClean(&m_World);

	m_Core = Core;
	m_PrevCore = Core;
	m_LastTick = Client()->PredGameTick(g_Config.m_ClDummy);
	return true;
}

CFastPractice::CScopedPredictEvents::CScopedPredictEvents()
{
	m_Saved = g_Config.m_ClPredictEvents;
	g_Config.m_ClPredictEvents = 1;
}

CFastPractice::CScopedPredictEvents::~CScopedPredictEvents()
{
	g_Config.m_ClPredictEvents = m_Saved;
}

void CFastPractice::NeutraliseOtherTees()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_ClientId)
			continue;
		CCharacter *pOther = m_World.GetCharacterById(i);
		if(pOther == nullptr)
			continue;
		pOther->ResetInput();
		pOther->ResetHook();
		m_World.ReleaseHooked(i);
	}
}

void CFastPractice::TeleportTo(CCharacter *pChar, const std::vector<vec2> &vOuts, bool ResetVelocity)
{
	CGameWorld *pWorld = pChar->GameWorld();
	CCharacterCore Core = pChar->GetCore();
	Core.m_Pos = vOuts[pWorld->m_Core.RandomOr0(vOuts.size())];
	if(ResetVelocity)
		Core.m_Vel = vec2(0.0f, 0.0f);
	pChar->SetCore(Core);
	pChar->SetCoreWorld(pWorld);
	pChar->ResetHook();
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
}

void CFastPractice::HandleTeleports(CCharacter *pChar)
{
	CCollision *pCollision = pChar->GameWorld()->Collision();
	if(pCollision == nullptr || pChar->Core()->m_Super || pChar->Core()->m_Invincible)
		return;

	std::vector<int> vIndices = pCollision->GetMapIndices(pChar->m_PrevPos, pChar->m_Pos);
	if(vIndices.empty())
		vIndices.push_back(pCollision->GetMapIndex(pChar->m_Pos));

	for(const int Index : vIndices)
	{
		const int Tele = pCollision->IsTeleport(Index);
		if(Tele != 0 && !pCollision->TeleOuts(Tele - 1).empty())
		{
			TeleportTo(pChar, pCollision->TeleOuts(Tele - 1), false);
			return;
		}

		const int EvilTele = pCollision->IsEvilTeleport(Index);
		if(EvilTele != 0 && !pCollision->TeleOuts(EvilTele - 1).empty())
		{
			TeleportTo(pChar, pCollision->TeleOuts(EvilTele - 1), true);
			return;
		}

		const bool CheckEvil = pCollision->IsCheckEvilTeleport(Index);
		if(CheckEvil || pCollision->IsCheckTeleport(Index))
		{
			for(int k = pChar->m_TeleCheckpoint - 1; k >= 0; k--)
			{
				if(pCollision->TeleCheckOuts(k).empty())
					continue;
				TeleportTo(pChar, pCollision->TeleCheckOuts(k), CheckEvil);
				return;
			}
			return;
		}
	}
}

void CFastPractice::EmitCoreEvents(CCharacter *pChar)
{
	const int Events = pChar->Core()->m_TriggeredEvents;
	if(Events == 0)
		return;

	const vec2 Pos = pChar->Core()->m_Pos;
	if(g_Config.m_ClPredict && (Events & COREEVENT_AIR_JUMP) != 0)
		GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);

	if(!g_Config.m_SndGame)
		return;

	if((Events & COREEVENT_GROUND_JUMP) != 0)
		GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
	if((Events & COREEVENT_HOOK_ATTACH_GROUND) != 0)
		GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
	if((Events & COREEVENT_HOOK_HIT_NOHOOK) != 0)
		GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
	if((Events & COREEVENT_HOOK_ATTACH_PLAYER) != 0)
		GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1.0f, Pos);
}

void CFastPractice::FlushPredictedEvents(CGameWorld *pWorld, bool Emit)
{
	if(!Emit)
	{
		pWorld->m_PredictedEvents.clear();
		return;
	}

	for(const CGameWorld::CPredictedEvent &Event : pWorld->m_PredictedEvents)
	{
		if(Event.m_EventId == NETEVENTTYPE_SOUNDWORLD)
			GameClient()->m_Sounds.PlayAt(CSounds::CHN_WORLD, Event.m_ExtraInfo, 1.0f, Event.m_Pos);
		else if(Event.m_EventId == NETEVENTTYPE_EXPLOSION)
			GameClient()->m_Effects.Explosion(Event.m_Pos, 1.0f);
		else if(Event.m_EventId == NETEVENTTYPE_HAMMERHIT)
			GameClient()->m_Effects.HammerHit(Event.m_Pos, 1.0f, 1.0f);
		else if(Event.m_EventId == NETEVENTTYPE_DAMAGEIND)
			GameClient()->m_Effects.DamageIndicator(Event.m_Pos, direction(Event.m_ExtraInfo / 256.0f), 1.0f);
	}
	pWorld->m_PredictedEvents.clear();
}

void CFastPractice::ApplyRenderedCharacter(CNetObj_Character *pPrev, CNetObj_Character *pCur)
{
	if(!m_Active)
		return;
	CCharacter *pChar = m_World.GetCharacterById(m_ClientId);
	if(pChar == nullptr)
		return;

	pPrev->m_Weapon = pCur->m_Weapon = pChar->GetActiveWeapon();

	const int Age = m_World.GameTick() - pChar->GetAttackTick();
	pPrev->m_AttackTick = pCur->m_AttackTick = Client()->GameTick(g_Config.m_ClDummy) - Age;
}

void CFastPractice::ClearFreeze(CCharacterCore *pCore)
{
	pCore->m_FreezeStart = 0;
	pCore->m_FreezeEnd = 0;
	pCore->m_IsInFreeze = false;
	pCore->m_DeepFrozen = false;
	pCore->m_LiveFrozen = false;
	if(pCore->m_ActiveWeapon >= 0 && pCore->m_ActiveWeapon < NUM_WEAPONS && !pCore->m_aWeapons[pCore->m_ActiveWeapon].m_Got)
		pCore->m_ActiveWeapon = WEAPON_GUN;
}

void CFastPractice::RebaseCore(CCharacterCore *pCore, int Delta) const
{
	if(Delta == 0)
		return;
	if(pCore->m_FreezeStart != 0)
		pCore->m_FreezeStart += Delta;
	if(pCore->m_FreezeEnd > 0)
		pCore->m_FreezeEnd += Delta;
	if(pCore->m_Ninja.m_ActivationTick != 0)
		pCore->m_Ninja.m_ActivationTick += Delta;
	for(auto &Weapon : pCore->m_aWeapons)
	{
		if(Weapon.m_AmmoRegenStart != 0)
			Weapon.m_AmmoRegenStart += Delta;
	}
}

void CFastPractice::SetRespawn(int ClientId, const CCharacterCore &Core, int TeleCheckpoint, int Tick)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aRespawnCore[ClientId] = Core;
	m_aRespawnTele[ClientId] = TeleCheckpoint;
	m_aRespawnTick[ClientId] = Tick;
	m_aHasRespawn[ClientId] = true;
}

bool CFastPractice::IsDead(CCharacter *pChar) const
{
	const CCollision *pCollision = pChar->GameWorld()->Collision();
	if(pCollision == nullptr)
		return false;

	const vec2 Pos = pChar->m_Pos;
	const float Radius = pChar->GetProximityRadius() / 3.0f;
	for(int Corner = 0; Corner < 4; Corner++)
	{
		const float x = Pos.x + ((Corner & 1) != 0 ? Radius : -Radius);
		const float y = Pos.y + ((Corner & 2) != 0 ? Radius : -Radius);
		if(pCollision->GetCollisionAt(x, y) == TILE_DEATH || pCollision->GetFrontCollisionAt(x, y) == TILE_DEATH)
			return true;
	}

	return pChar->GameLayerClipped(Pos);
}

bool CFastPractice::HidesTee(int ClientId) const
{
	if(!m_Active || ClientId < 0)
		return false;
	if(ClientId == m_ClientId)
	{
		return Suspended();
	}
	return true;
}

bool CFastPractice::Suspended() const
{
	return GameClient()->m_Snap.m_SpecInfo.m_Active ||
	       m_ClientId < 0 ||
	       !GameClient()->m_Snap.m_aCharacters[m_ClientId].m_Active ||
	       !GameClient()->Predict();
}

void CFastPractice::ApplyAction(CCharacter *pChar, int Action)
{
	const auto &&Give = [&](int Weapon, bool Remove) {
		pChar->GiveWeapon(Weapon, Remove);
		if(!Remove && Weapon != WEAPON_NINJA)
			pChar->SetWeaponAmmo(Weapon, -1);
	};

	switch(Action)
	{
	case ACTION_KILL:
		ApplyRespawn(pChar);
		break;

	case ACTION_TELEPORT:
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_Pos = m_TeleportTarget;
		Core.m_Vel = vec2(0.0f, 0.0f);
		ClearFreeze(&Core);
		pChar->SetCore(Core);
		pChar->SetCoreWorld(pChar->GameWorld());
		pChar->ResetHook();
		pChar->m_Pos = Core.m_Pos;
		pChar->m_PrevPos = Core.m_Pos;
		pChar->m_PrevPrevPos = Core.m_Pos;
		pChar->m_FreezeTime = 0;
		pChar->m_FrozenLastTick = false;
		break;
	}
	case ACTION_UNFREEZE:
	{
		CCharacterCore Core = pChar->GetCore();
		ClearFreeze(&Core);
		pChar->SetCore(Core);
		pChar->SetCoreWorld(pChar->GameWorld());
		pChar->m_FreezeTime = 0;
		pChar->m_FrozenLastTick = false;
		break;
	}
	case ACTION_GIVE_HAMMER: Give(WEAPON_HAMMER, false); break;
	case ACTION_GIVE_GUN: Give(WEAPON_GUN, false); break;
	case ACTION_GIVE_SHOTGUN: Give(WEAPON_SHOTGUN, false); break;
	case ACTION_GIVE_GRENADE: Give(WEAPON_GRENADE, false); break;
	case ACTION_GIVE_LASER: Give(WEAPON_LASER, false); break;
	case ACTION_GIVE_NINJA: pChar->GiveNinja(); break;
	case ACTION_GIVE_ALL: pChar->GiveAllWeapons(); break;
	case ACTION_TAKE_SHOTGUN: Give(WEAPON_SHOTGUN, true); break;
	case ACTION_TAKE_GRENADE: Give(WEAPON_GRENADE, true); break;
	case ACTION_TAKE_LASER: Give(WEAPON_LASER, true); break;
	case ACTION_TAKE_NINJA: pChar->RemoveNinja(); break;
	default: break;
	}

	if(pChar->GetCid() == m_ClientId)
	{
		m_Core = pChar->GetCore();
		m_PrevCore = m_Core;
	}
	m_RenderWorld.CopyWorldClean(&m_World);
}

bool CFastPractice::RealTeePos(int ClientId, vec2 *pOut) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	if(const CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId))
	{
		*pOut = pChar->Core()->m_Pos;
		return true;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		*pOut = vec2(GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_X,
			GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur.m_Y);
		return true;
	}
	return false;
}

void CFastPractice::SpawnMapEntities()
{
	for(const CSnapEntities &Entity : GameClient()->SnapEntities())
	{
		const int Type = Entity.m_Item.m_Type;
		const int Id = Entity.m_Item.m_Id;

		if(Type == NETOBJTYPE_PICKUP || Type == NETOBJTYPE_DDNETPICKUP)
		{
			if(m_World.GetEntity(Id, CGameWorld::ENTTYPE_PICKUP) != nullptr)
				continue;

			CPickupData Data = ExtractPickupInfo(Type, Entity.m_Item.m_pData, Entity.m_pDataEx);
			if((Data.m_Flags & PICKUPFLAG_NO_PREDICT) != 0)
				continue;

			auto *pPickup = new CPickup(&m_World, Id, &Data);
			m_World.InsertEntity(pPickup);
		}
		else if(Type == NETOBJTYPE_LASER || Type == NETOBJTYPE_DDNETLASER)
		{
			CLaserData Data = ExtractLaserInfo(Type, Entity.m_Item.m_pData, &m_World, Entity.m_pDataEx);
			if(Data.m_Type != LASERTYPE_DOOR)
				continue;
			if(GameClient()->m_GameWorld.GetEntity(Id, CGameWorld::ENTTYPE_DOOR) != nullptr)
				continue;
			if(m_World.GetEntity(Id, CGameWorld::ENTTYPE_DOOR) != nullptr)
				continue;

			auto *pDoor = new CDoor(&m_World, Id, &Data);
			m_World.InsertEntity(pDoor);
			pDoor->ResetCollision();
		}
	}

	CCollision *pCollision = m_World.Collision();
	if(pCollision == nullptr)
		return;

	for(int y = 0; y < pCollision->GetHeight(); y++)
	{
		for(int x = 0; x < pCollision->GetWidth(); x++)
		{
			const int Index = y * pCollision->GetWidth() + x;
			int Type;
			int Subtype = 0;
			switch(pCollision->GetTileIndex(Index))
			{
			case ENTITY_OFFSET + ENTITY_ARMOR_1: Type = POWERUP_ARMOR; break;
			case ENTITY_OFFSET + ENTITY_HEALTH_1: Type = POWERUP_HEALTH; break;
			case ENTITY_OFFSET + ENTITY_WEAPON_SHOTGUN:
				Type = POWERUP_WEAPON;
				Subtype = WEAPON_SHOTGUN;
				break;
			case ENTITY_OFFSET + ENTITY_WEAPON_GRENADE:
				Type = POWERUP_WEAPON;
				Subtype = WEAPON_GRENADE;
				break;
			case ENTITY_OFFSET + ENTITY_WEAPON_LASER:
				Type = POWERUP_WEAPON;
				Subtype = WEAPON_LASER;
				break;
			case ENTITY_OFFSET + ENTITY_POWERUP_NINJA:
				Type = POWERUP_NINJA;
				Subtype = WEAPON_NINJA;
				break;
			default: continue;
			}

			CPickupData Data;
			Data.m_Pos = vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
			Data.m_Type = Type;
			Data.m_Subtype = Subtype;
			Data.m_SwitchNumber = 0;
			Data.m_Flags = 0;

			bool Existing = false;
			for(CEntity *pEnt = m_World.FindFirst(CGameWorld::ENTTYPE_PICKUP); pEnt; pEnt = pEnt->TypeNext())
			{
				if(distance(pEnt->GetPos(), Data.m_Pos) < 16.0f)
				{
					Existing = true;
					break;
				}
			}
			if(Existing)
				continue;

			auto *pPickup = new CPickup(&m_World, MAP_PICKUP_ID_OFFSET + Index, &Data);
			m_World.InsertEntity(pPickup);
		}
	}
}

CCharacterCore CFastPractice::ShownCore() const
{
	CCharacterCore Core = m_Core;
	RebaseCore(&Core, Client()->GameTick(g_Config.m_ClDummy) - m_World.GameTick());
	return Core;
}

void CFastPractice::RenderPracticeTee()
{
	if(m_ClientId < 0 || !Suspended())
		return;

	const CCharacterCore Core = ShownCore();

	CTeeRenderInfo TeeRenderInfo = GameClient()->m_aClients[m_ClientId].m_RenderInfo;
	TeeRenderInfo.m_Size = 64.0f;
	TeeRenderInfo.m_TeeRenderFlags = 0;

	const bool Frozen = Core.m_FreezeEnd != 0;
	if(Frozen || Core.m_LiveFrozen)
		TeeRenderInfo.m_TeeRenderFlags |= TEE_EFFECT_FROZEN;
	if(Core.m_Invincible)
		TeeRenderInfo.m_TeeRenderFlags |= TEE_EFFECT_SPARKLE;

	const bool IsTeamPlay = GameClient()->IsTeamPlay();
	if((Core.m_ActiveWeapon == WEAPON_NINJA || (Frozen && !GameClient()->m_GameInfo.m_NoSkinChangeForFrozen)) && g_Config.m_ClShowNinja)
	{
		TeeRenderInfo.m_aSixup[g_Config.m_ClDummy].Reset();
		TeeRenderInfo.ApplySkin(GameClient()->m_Players.NinjaTeeRenderInfo()->TeeRenderInfo());
		TeeRenderInfo.m_CustomColoredSkin = IsTeamPlay;
		if(!IsTeamPlay)
		{
			TeeRenderInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
			TeeRenderInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
		}
	}

	CNetObj_Character Render = {};
	Core.Write(&Render);
	Render.m_Tick = Client()->GameTick(g_Config.m_ClDummy);
	Render.m_Weapon = Core.m_ActiveWeapon;
	Render.m_Emote = EMOTE_NORMAL;
	if(const CCharacter *pChar = m_World.GetCharacterById(m_ClientId))
		Render.m_AttackTick = Client()->GameTick(g_Config.m_ClDummy) - (m_World.GameTick() - pChar->GetAttackTick());

	CScreenRect ScreenRect = Graphics()->GetScreen();
	ScreenRect.Expand(100.0f);

	const int SavedGhostAlpha = g_Config.m_ClRaceGhostAlpha;
	g_Config.m_ClRaceGhostAlpha = 100;
	GameClient()->m_Players.RenderHook(ScreenRect, &Render, &Render, &TeeRenderInfo, -2);
	GameClient()->m_Players.RenderPlayer(ScreenRect, &Render, &Render, &TeeRenderInfo, -2);
	g_Config.m_ClRaceGhostAlpha = SavedGhostAlpha;

	if(!g_Config.m_ClShowFreezeBars)
		return;

	if(Core.m_FreezeEnd <= 0 || Core.m_FreezeStart == 0 || Core.m_FreezeEnd <= Core.m_FreezeStart)
		return;
	if(Core.m_IsInFreeze && g_Config.m_ClFreezeBarsAlphaInsideFreeze == 0)
		return;

	const int64_t Max = (int64_t)Core.m_FreezeEnd - Core.m_FreezeStart;
	const float Progress = std::clamp<int64_t>(Max - ((int64_t)Client()->GameTick(g_Config.m_ClDummy) - Core.m_FreezeStart), 0, Max) / (float)Max;
	if(Progress <= 0.0f)
		return;

	float Alpha = 1.0f;
	if(Core.m_IsInFreeze)
		Alpha *= g_Config.m_ClFreezeBarsAlphaInsideFreeze / 100.0f;

	GameClient()->m_FreezeBars.RenderFreezeBarPos(m_Core.m_Pos.x - 32.0f, m_Core.m_Pos.y + 32.0f, 64.0f, 16.0f, Progress, Alpha);
}

void CFastPractice::RenderRealTee()
{
	if(m_ClientId < 0 || !GameClient()->m_Snap.m_aCharacters[m_ClientId].m_Active)
		return;

	CTeeRenderInfo TeeRenderInfo = GameClient()->m_aClients[m_ClientId].m_RenderInfo;
	TeeRenderInfo.m_Size = 64.0f;
	TeeRenderInfo.m_TeeRenderFlags = 0;

	CScreenRect ScreenRect = Graphics()->GetScreen();
	ScreenRect.Expand(100.0f);

	const CNetObj_Character &Prev = GameClient()->m_Snap.m_aCharacters[m_ClientId].m_Prev;
	const CNetObj_Character &Cur = GameClient()->m_Snap.m_aCharacters[m_ClientId].m_Cur;

	const int SavedGhostAlpha = g_Config.m_ClRaceGhostAlpha;
	g_Config.m_ClRaceGhostAlpha = 35;
	GameClient()->m_Players.RenderPlayer(ScreenRect, &Prev, &Cur, &TeeRenderInfo, -2, Client()->IntraGameTick(g_Config.m_ClDummy));
	g_Config.m_ClRaceGhostAlpha = SavedGhostAlpha;
}

void CFastPractice::ApplyRespawn(CCharacter *pChar)
{
	const int Id = pChar->GetCid();
	if(Id < 0 || Id >= MAX_CLIENTS || !m_aHasRespawn[Id])
		return;

	CGameWorld *pWorld = pChar->GameWorld();
	CCharacterCore Core = m_aRespawnCore[Id];
	RebaseCore(&Core, pWorld->GameTick() - m_aRespawnTick[Id]);

	const CCharacterCore Current = pChar->GetCore();
	for(int Weapon = 0; Weapon < NUM_WEAPONS; Weapon++)
		Core.m_aWeapons[Weapon] = Current.m_aWeapons[Weapon];
	Core.m_ActiveWeapon = Current.m_ActiveWeapon;
	Core.m_Jetpack = Current.m_Jetpack;

	pChar->SetCore(Core);
	pChar->SetCoreWorld(pWorld);
	pChar->ResetHook();
	pWorld->ReleaseHooked(Id);
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
	pChar->m_FreezeTime = Core.m_FreezeEnd > 0 ? std::max(1, Core.m_FreezeEnd - pWorld->GameTick()) : 0;
	pChar->m_FrozenLastTick = pChar->m_FreezeTime != 0;
	pChar->m_TeleCheckpoint = m_aRespawnTele[Id];
	pChar->ResetInput();
}

void CFastPractice::Advance(int Ticks)
{
	const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];

	const CScopedPredictEvents ScopedEvents;

	for(int i = 0; i < Ticks; i++)
	{
		CCharacter *pChar = m_World.GetCharacterById(m_ClientId);
		if(!pChar)
		{
			Stop();
			return;
		}

		m_PrevCore = pChar->GetCore();

		bool aHadWeapon[NUM_WEAPONS];
		for(int Weapon = 0; Weapon < NUM_WEAPONS; Weapon++)
			aHadWeapon[Weapon] = pChar->GetWeaponGot(Weapon);

		pChar->OnDirectInput(&Input);
		m_World.m_GameTick++;
		pChar->OnPredictedInput(&Input);
		m_RenderWorld.CopyWorldClean(&m_World);
		m_World.Tick();

		pChar = m_World.GetCharacterById(m_ClientId);
		if(!pChar)
		{
			Stop();
			return;
		}

		EmitCoreEvents(pChar);
		HandleTeleports(pChar);

		if(g_Config.m_ClAutoswitchWeapons)
		{
			for(int Weapon = 0; Weapon < NUM_WEAPONS; Weapon++)
			{
				if(Weapon == WEAPON_NINJA || aHadWeapon[Weapon] || !pChar->GetWeaponGot(Weapon))
					continue;
				GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy].m_WantedWeapon = Weapon + 1;
			}
		}
		if(IsDead(pChar))
			ApplyRespawn(pChar);
		FlushPredictedEvents(&m_World, true);
		m_Core = pChar->GetCore();
	}
}

void CFastPractice::OnUpdatePositions()
{
	if(!m_Active)
		return;

	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_LocalClientId != m_ClientId)
	{
		Stop();
		return;
	}

	const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);

	if(!Suspended())
	{
		const int Elapsed = Tick - m_LastTick;
		if(Elapsed > 0)
		{
			const int Simulate = std::min(Elapsed, MAX_CATCHUP_TICKS);
			m_World.m_GameTick += Elapsed - Simulate;
			Advance(Simulate);
		}
	}
	m_LastTick = Tick;

	if(!m_Active)
		return;

	const int TickOffset = m_World.GameTick() - Client()->GameTick(g_Config.m_ClDummy);
	const CCharacterCore Shown = ShownCore();
	CCharacterCore ShownPrev = m_PrevCore;
	RebaseCore(&ShownPrev, -TickOffset);

	GameClient()->m_aClients[m_ClientId].m_Predicted = Shown;
	GameClient()->m_aClients[m_ClientId].m_PrevPredicted = ShownPrev;
	GameClient()->m_aClients[m_ClientId].m_RegularPredicted = Shown;
	GameClient()->m_PredictedChar = Shown;
	GameClient()->m_PredictedPrevChar = ShownPrev;
}

void CFastPractice::OnRender()
{
	if(!m_Active)
		return;

	RenderRealTee();

	RenderPracticeTee();

	const CScreenRect WorldScreen = Graphics()->GetScreen();
	Ui()->MapScreen();

	const CUIRect *pScreen = Ui()->Screen();
	const char *pLabel = Localize("Fast practice");
	const float FontSize = 8.0f;
	TextRender()->TextColor(1.0f, 0.75f, 0.2f, 0.9f);
	TextRender()->Text(pScreen->w / 2.0f - TextRender()->TextWidth(FontSize, pLabel) / 2.0f, 4.0f, FontSize, pLabel);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Graphics()->MapScreen(WorldScreen);
}

bool CFastPractice::OnKill()
{
	if(!m_Active)
		return false;
	CCharacter *pChar = m_World.GetCharacterById(m_ClientId);
	if(pChar == nullptr)
		return false;
	ApplyAction(pChar, ACTION_KILL);
	return true;
}

bool CFastPractice::OnChatCommand(const char *pLine)
{
	if(!m_Active || pLine == nullptr || pLine[0] != '/')
		return false;

	char aCommand[32];
	const char *pRest = pLine + 1;
	int Length = 0;
	while(pRest[Length] != '\0' && pRest[Length] != ' ' && Length < (int)sizeof(aCommand) - 1)
	{
		aCommand[Length] = pRest[Length];
		Length++;
	}
	aCommand[Length] = '\0';
	const char *pArgument = str_skip_whitespaces_const(pRest + Length);

	static const struct
	{
		const char *m_pName;
		int m_Action;
	} s_aSimple[] = {
		{"kill", ACTION_KILL},
		{"unfreeze", ACTION_UNFREEZE},
		{"hammer", ACTION_GIVE_HAMMER},
		{"gun", ACTION_GIVE_GUN},
		{"shotgun", ACTION_GIVE_SHOTGUN},
		{"grenade", ACTION_GIVE_GRENADE},
		{"rifle", ACTION_GIVE_LASER},
		{"laser", ACTION_GIVE_LASER},
		{"ninja", ACTION_GIVE_NINJA},
		{"weapons", ACTION_GIVE_ALL},
		{"unshotgun", ACTION_TAKE_SHOTGUN},
		{"ungrenade", ACTION_TAKE_GRENADE},
		{"unrifle", ACTION_TAKE_LASER},
		{"unlaser", ACTION_TAKE_LASER},
		{"unninja", ACTION_TAKE_NINJA},
	};

	CCharacter *pChar = m_World.GetCharacterById(m_ClientId);
	if(pChar == nullptr)
		return false;

	for(const auto &Simple : s_aSimple)
	{
		if(str_comp_nocase(aCommand, Simple.m_pName) != 0)
			continue;
		ApplyAction(pChar, Simple.m_Action);
		return true;
	}

	if(str_comp_nocase(aCommand, "tp") == 0 || str_comp_nocase(aCommand, "tele") == 0)
	{
		int Target = -1;
		if(pArgument[0] == '\0')
		{
			if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			{
				if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
				{
					Target = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
				}
				else
				{
					m_TeleportTarget = GameClient()->m_Camera.m_Center;
					ApplyAction(pChar, ACTION_TELEPORT);
					return true;
				}
			}
			else
			{
				Target = m_ClientId;
			}
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!GameClient()->m_aClients[i].m_Active)
					continue;
				if(str_comp_nocase(GameClient()->m_aClients[i].m_aName, pArgument) == 0 ||
					str_comp_nocase(GameClient()->m_aClients[i].m_aRealName, pArgument) == 0)
				{
					Target = i;
					break;
				}
			}
		}

		if(Target < 0)
		{
			Notify(Localize("Fast practice: no such player."));
			return true;
		}

		if(!RealTeePos(Target, &m_TeleportTarget))
		{
			Notify(Localize("Fast practice: that player has no tee right now."));
			return true;
		}
		ApplyAction(pChar, ACTION_TELEPORT);
		return true;
	}

	return false;
}
