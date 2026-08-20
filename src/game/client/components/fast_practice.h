#ifndef GAME_CLIENT_COMPONENTS_FAST_PRACTICE_H
#define GAME_CLIENT_COMPONENTS_FAST_PRACTICE_H

#include <engine/console.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/prediction/gameworld.h>

#include <vector>

class CFastPractice : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnMapLoad() override;
	void OnRender() override;

	bool IsActive() const { return m_Active; }

	bool HidesTee(int ClientId) const;

	bool OnChatCommand(const char *pLine);

	bool OnKill();

	const CNetObj_PlayerInput &FrozenInput() const { return m_FrozenInput; }

	CGameWorld *RenderWorld() { return &m_RenderWorld; }

	int RenderTick() const { return m_RenderWorld.GameTick(); }

	int SwitcherTeam() const { return m_ClientId < 0 ? 0 : m_World.m_Teams.Team(m_ClientId); }

	static constexpr int MAP_PICKUP_ID_OFFSET = 1 << 20;

	void OnUpdatePositions();

	void ApplyRenderedCharacter(CNetObj_Character *pPrev, CNetObj_Character *pCur);

private:
	static constexpr int MAX_CATCHUP_TICKS = 10;

	bool m_Active = false;
	int m_ClientId = -1;
	int m_LastTick = 0;

	CGameWorld m_World;
	CGameWorld m_RenderWorld;

	CCharacterCore m_Core;
	CCharacterCore m_PrevCore;

	CCharacterCore m_CheckpointCore;
	int m_CheckpointTick = 0;
	int m_CheckpointTele = 0;
	vec2 m_TeleportTarget = vec2(0.0f, 0.0f);

	CCharacterCore m_aRespawnCore[MAX_CLIENTS];
	int m_aRespawnTele[MAX_CLIENTS] = {0};
	int m_aRespawnTick[MAX_CLIENTS] = {0};
	bool m_aHasRespawn[MAX_CLIENTS] = {false};

	CNetObj_PlayerInput m_FrozenInput;

	enum EAction
	{
		ACTION_NONE = 0,
		ACTION_KILL,
		ACTION_TELEPORT,
		ACTION_UNFREEZE,
		ACTION_GIVE_HAMMER,
		ACTION_GIVE_GUN,
		ACTION_GIVE_SHOTGUN,
		ACTION_GIVE_GRENADE,
		ACTION_GIVE_LASER,
		ACTION_GIVE_NINJA,
		ACTION_GIVE_ALL,
		ACTION_TAKE_SHOTGUN,
		ACTION_TAKE_GRENADE,
		ACTION_TAKE_LASER,
		ACTION_TAKE_NINJA,
	};

	void Notify(const char *pMessage);

	bool CanRun() const;
	void Toggle();
	void Start();
	void Stop();
	void SetCheckpoint();
	bool Respawn();
	void Advance(int Ticks);

	class CScopedPredictEvents
	{
	public:
		CScopedPredictEvents();
		~CScopedPredictEvents();

	private:
		int m_Saved;
	};

	void RemoveOtherTees();
	void HandleTeleports(class CCharacter *pChar);
	bool IsDead(class CCharacter *pChar) const;
	void SetRespawn(int ClientId, const CCharacterCore &Core, int TeleCheckpoint, int Tick);
	void ApplyRespawn(class CCharacter *pChar);
	void ApplyAction(class CCharacter *pChar, int Action);
	bool Suspended() const;
	void RebaseCore(CCharacterCore *pCore, int Delta) const;

	static void ClearFreeze(CCharacterCore *pCore);
	void TeleportTo(class CCharacter *pChar, const std::vector<vec2> &vOuts, bool ResetVelocity);
	void FlushPredictedEvents(CGameWorld *pWorld, bool Emit);

	void EmitCoreEvents(class CCharacter *pChar);

	bool RealTeePos(int ClientId, vec2 *pOut) const;

	void SpawnMapEntities();

	void RenderRealTee();
	void RenderPracticeTee();

	CCharacterCore ShownCore() const;

	static void ConFastPractice(IConsole::IResult *pResult, void *pUserData);
	static void ConFastPracticeCheckpoint(IConsole::IResult *pResult, void *pUserData);
};

#endif
