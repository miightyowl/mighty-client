#include "petting.h"

#include <base/math.h>
#include <base/vmath.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

namespace {
// gesture detection
constexpr float OSC_WINDOW = 1.5f; // how far to look for up/down
constexpr int OSC_MIN_REVERSALS = 4; // need several up/down reversals
constexpr float OSC_MIN_DELTA = 0.05f;
constexpr float OSC_MIN_AMPLITUDE = 0.12f;

constexpr float OSC_UP_THRESHOLD = -0.64f;
constexpr float OSC_DOWN_LIMIT = -0.45f;

// standing beside target
constexpr float PET_RANGE_X = 80.0f;
constexpr float PET_RANGE_Y = 48.0f;

constexpr float PET_LINGER = 2.0f;
constexpr float PET_FADE = 0.4f;

// hand rendering
constexpr float HAND_SIZE = 60.0f;
constexpr float HAND_Y_OFFSET = -18.0f;
constexpr float FRAME_TIME = 0.06f;
}

void CPetting::OnInit()
{
	m_NumFrames = 0;
	if(Storage()->FileExists("pet.png", IStorage::TYPE_ALL))
	{
		m_PetTexture = Graphics()->LoadTexture("pet.png", IStorage::TYPE_ALL);
		m_NumFrames = 5;
	}
}

void CPetting::OnReset()
{
	for(auto &State : m_aStates)
		State = CPetState();
}

void CPetting::ResetOscillation(CPetState &State)
{
	State.m_LastDir = 0;
	State.m_ReversalCount = 0;
	State.m_MinAimY = 0.0f;
	State.m_MaxAimY = 0.0f;
	State.m_AimRangeResetTime = 0.0f;
}

void CPetting::UpdatePetting()
{
	const float Now = LocalTime();

	for(int A = 0; A < MAX_CLIENTS; A++)
	{
		CPetState &PetterState = m_aStates[A];

		const bool ActiveA = GameClient()->m_Snap.m_aCharacters[A].m_Active && GameClient()->m_aClients[A].m_Active;
		if(!ActiveA)
		{
			ResetOscillation(PetterState);
			continue;
		}

		const CNetObj_Character &CurA = GameClient()->m_Snap.m_aCharacters[A].m_Cur;
		const vec2 AimDir = direction(CurA.m_Angle / 256.0f);

		const float AimY = AimDir.y;
		if(Now - PetterState.m_AimRangeResetTime > OSC_WINDOW)
		{
			PetterState.m_AimRangeResetTime = Now;
			PetterState.m_MinAimY = AimY;
			PetterState.m_MaxAimY = AimY;
		}
		PetterState.m_MinAimY = std::min(PetterState.m_MinAimY, AimY);
		PetterState.m_MaxAimY = std::max(PetterState.m_MaxAimY, AimY);

		const float Delta = AimY - PetterState.m_LastAimY;
		if(absolute(Delta) > OSC_MIN_DELTA)
		{
			const int Dir = Delta > 0.0f ? 1 : -1;
			if(PetterState.m_LastDir != 0 && Dir != PetterState.m_LastDir)
			{
				if(PetterState.m_ReversalCount >= (int)std::size(PetterState.m_aReversalTimes))
				{
					for(int i = 1; i < (int)std::size(PetterState.m_aReversalTimes); i++)
						PetterState.m_aReversalTimes[i - 1] = PetterState.m_aReversalTimes[i];
					PetterState.m_ReversalCount = (int)std::size(PetterState.m_aReversalTimes) - 1;
				}
				PetterState.m_aReversalTimes[PetterState.m_ReversalCount++] = Now;
			}
			PetterState.m_LastDir = Dir;
			PetterState.m_LastAimY = AimY;
		}

		int RecentReversals = 0;
		for(int i = 0; i < PetterState.m_ReversalCount; i++)
			if(Now - PetterState.m_aReversalTimes[i] <= OSC_WINDOW)
				RecentReversals++;

		const bool Bobbing =
			RecentReversals >= OSC_MIN_REVERSALS &&
			PetterState.m_MaxAimY - PetterState.m_MinAimY >= OSC_MIN_AMPLITUDE &&
			PetterState.m_MinAimY <= OSC_UP_THRESHOLD &&
			PetterState.m_MaxAimY <= OSC_DOWN_LIMIT;

		if(CurA.m_Weapon != WEAPON_GUN || !Bobbing)
			continue;

		// find closest player
		const vec2 PosA = GameClient()->m_aClients[A].m_RenderPos;
		int Target = -1;
		float BestDistSq = 0.0f;
		for(int B = 0; B < MAX_CLIENTS; B++)
		{
			if(B == A)
				continue;
			if(!GameClient()->m_Snap.m_aCharacters[B].m_Active || !GameClient()->m_aClients[B].m_Active)
				continue;

			const vec2 PosB = GameClient()->m_aClients[B].m_RenderPos;
			const float Dx = PosB.x - PosA.x;
			const float Dy = PosB.y - PosA.y;
			if(absolute(Dx) > PET_RANGE_X || absolute(Dy) > PET_RANGE_Y)
				continue;
			if((Dx >= 0.0f) != (AimDir.x >= 0.0f))
				continue;

			const float DistSq = Dx * Dx + Dy * Dy;
			if(Target == -1 || DistSq < BestDistSq)
			{
				Target = B;
				BestDistSq = DistSq;
			}
		}

		if(Target != -1)
		{
			CPetState &TargetState = m_aStates[Target];
			TargetState.m_PetUntil = Now + PET_LINGER;
			TargetState.m_PetFromRight = PosA.x > GameClient()->m_aClients[Target].m_RenderPos.x;
		}
	}
}

void CPetting::RenderHands()
{
	if(!m_PetTexture.IsValid() || m_NumFrames <= 0)
		return;

	const float Now = LocalTime();
	const int Frame = (int)(Now / FRAME_TIME) % m_NumFrames;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CPetState &State = m_aStates[i];
		if(State.m_PetUntil <= Now)
			continue;
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active || !GameClient()->m_aClients[i].m_Active)
			continue;

		const vec2 Pos = GameClient()->m_aClients[i].m_RenderPos;
		const float Alpha = std::clamp((State.m_PetUntil - Now) / PET_FADE, 0.0f, 1.0f);

		float U0 = (float)Frame / m_NumFrames;
		float U1 = (float)(Frame + 1) / m_NumFrames;

		if(State.m_PetFromRight)
			std::swap(U0, U1);

		Graphics()->TextureSet(m_PetTexture);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetRotation(0.0f);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		Graphics()->QuadsSetSubset(U0, 0.0f, U1, 1.0f);
		const float Height = HAND_SIZE * 106.0f / 107.0f;
		IGraphics::CQuadItem QuadItem(Pos.x, Pos.y + HAND_Y_OFFSET, HAND_SIZE, Height);
		Graphics()->QuadsDraw(&QuadItem, 1);
		Graphics()->QuadsEnd();
	}
}

void CPetting::OnRender()
{
	if(!g_Config.m_ClMClientPet)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	UpdatePetting();
	RenderHands();
}
