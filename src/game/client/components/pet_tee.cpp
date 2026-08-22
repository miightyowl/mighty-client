#include "pet_tee.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>

namespace
{
	const float OTHERS_SIZE = 0.6f;
	const float OTHERS_ALPHA = 0.9f;
}

void CPetTee::PickWanderOffset(CPet &Pet, float Time, float Scale)
{
	const float BaseRadius = 55.0f + Scale * 34.0f;

	if(random_float() < 0.4f)
	{
		Pet.m_NextWanderTime = Time + random_float(1.8f, 4.0f);
		return;
	}

	const float ClearX = 34.0f + Scale * 18.0f;
	const float ClearTop = 58.0f + Scale * 30.0f;
	vec2 NewSpot = Pet.m_WanderOffset;
	for(int Try = 0; Try < 8; Try++)
	{
		const float Radius = BaseRadius * random_float(0.8f, 1.25f);
		NewSpot = random_direction() * Radius;
		NewSpot.y -= 12.0f;
		const bool OverFace = absolute(NewSpot.x) < ClearX && NewSpot.y > -ClearTop;
		if(!OverFace)
			break;
	}
	Pet.m_WanderOffset = mix(Pet.m_WanderOffset, NewSpot, random_float(0.35f, 1.0f));
	Pet.m_NextWanderTime = Time + random_float(1.0f, 2.4f);
}

void CPetTee::PickGazeTarget(CPet &Pet, float Time)
{
	Pet.m_GazeTarget = random_direction();
	Pet.m_NextGazeTime = Time + random_float(0.4f, 1.6f);
}

void CPetTee::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const char *pSkinName = nullptr;
		float Scale = OTHERS_SIZE;
		float Alpha = OTHERS_ALPHA;

		if(ClientId == LocalId)
		{
			if(g_Config.m_ClMClientPetTee)
			{
				pSkinName = g_Config.m_ClMClientPetTeeSkin;
				Scale = (float)g_Config.m_ClMClientPetTeeSize / 100.0f;
				Alpha = (float)g_Config.m_ClMClientPetTeeAlpha / 100.0f;
			}
		}
		else if(g_Config.m_ClMClientPetTeeOthers)
		{
			pSkinName = GameClient()->m_MClientDetect.PetSkin(ClientId);
		}

		if(pSkinName == nullptr)
		{
			m_aPets[ClientId].m_Alpha = 0.0f;
			continue;
		}

		RenderPet(ClientId, g_Config.m_ClMClientForceSkin ? "maodie" : pSkinName, Scale, Alpha);
	}
}

void CPetTee::RenderPet(int ClientId, const char *pSkinName, float Scale, float Alpha)
{
	CPet &Pet = m_aPets[ClientId];
	const auto &Player = GameClient()->m_aClients[ClientId];
	const float Delta = Client()->RenderFrameTime();
	const float Time = Client()->LocalTime();

	const bool Following = Player.m_Active && Player.m_Team != TEAM_SPECTATORS &&
			       (ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy] || GameClient()->m_Snap.m_aCharacters[ClientId].m_Active);

	if(Following)
	{
		if(Pet.m_Alpha == 0.0f)
		{
			Pet.m_WanderOffset = vec2(random_float(-40.0f, 40.0f), -(60.0f + Scale * 34.0f));
			PickWanderOffset(Pet, Time, Scale);
			PickGazeTarget(Pet, Time);
			Pet.m_Position = Player.m_RenderPos + Pet.m_WanderOffset;
			Pet.m_Velocity = vec2(0.0f, 0.0f);
			Pet.m_Dir = random_direction();
		}
		if(Time >= Pet.m_NextWanderTime)
			PickWanderOffset(Pet, Time, Scale);

		Pet.m_Target = Player.m_RenderPos + Pet.m_WanderOffset;
		Pet.m_Target.y += std::sin(Time * 2.3f) * 4.0f;

		if(Pet.m_Alpha < 1.0f)
		{
			Pet.m_Alpha += Delta;
			if(Pet.m_Alpha >= 1.0f)
				Pet.m_Alpha = 1.0f;
		}

		Pet.m_Position += Pet.m_Velocity * Delta;

		const vec2 ToTarget = Pet.m_Target - Pet.m_Position;
		const float PlayerDist = distance(Pet.m_Position, Player.m_RenderPos);

		float k = 14.0f;
		if(PlayerDist > 220.0f)
			k = mix(14.0f, 260.0f, std::clamp((PlayerDist - 220.0f) / 240.0f, 0.0f, 1.0f));
		const float c = 2.0f * std::sqrt(k);
		Pet.m_Velocity += (ToTarget * k - Pet.m_Velocity * c) * Delta;

		if(PlayerDist > 900.0f)
		{
			Pet.m_Position = Pet.m_Target;
			Pet.m_Velocity = vec2(0.0f, 0.0f);
		}
	}
	else
	{
		if(Pet.m_Alpha > 0.0f)
		{
			Pet.m_Alpha -= Delta;
			if(Pet.m_Alpha <= 0.0f)
				Pet.m_Alpha = 0.0f;
		}
	}

	if(Pet.m_Alpha <= 0.0f)
		return;

	if(Time >= Pet.m_NextGazeTime)
		PickGazeTarget(Pet, Time);
	Pet.m_Dir = mix(Pet.m_Dir, Pet.m_GazeTarget, std::clamp(Delta * 2.5f, 0.0f, 1.0f));
	if(length(Pet.m_Dir) > 0.001f)
		Pet.m_Dir = normalize(Pet.m_Dir);

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.Apply(GameClient()->m_Skins.Find(pSkinName));
	TeeRenderInfo.m_Size = 64.0f * Scale;
	TeeRenderInfo.m_GotAirJump = Pet.m_Velocity.y > -10.0f;
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, Pet.m_Dir, Pet.m_Position, Pet.m_Alpha * Alpha);
}

void CPetTee::OnMapLoad()
{
	for(CPet &Pet : m_aPets)
		Pet.m_Alpha = 0.0f;
}
