#include "pet_tee.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>

void CPetTee::PickWanderOffset(float Time, float Scale)
{
	const float BaseRadius = 55.0f + Scale * 34.0f;

	if(random_float() < 0.4f)
	{
		m_NextWanderTime = Time + random_float(1.8f, 4.0f);
		return;
	}

	const float ClearX = 34.0f + Scale * 18.0f;
	const float ClearTop = 58.0f + Scale * 30.0f;
	vec2 NewSpot = m_WanderOffset;
	for(int Try = 0; Try < 8; Try++)
	{
		const float Radius = BaseRadius * random_float(0.8f, 1.25f);
		NewSpot = random_direction() * Radius;
		NewSpot.y -= 12.0f;
		const bool OverFace = absolute(NewSpot.x) < ClearX && NewSpot.y > -ClearTop;
		if(!OverFace)
			break;
	}
	m_WanderOffset = mix(m_WanderOffset, NewSpot, random_float(0.35f, 1.0f));
	m_NextWanderTime = Time + random_float(1.0f, 2.4f);
}

void CPetTee::PickGazeTarget(float Time)
{
	m_GazeTarget = random_direction();
	m_NextGazeTime = Time + random_float(0.4f, 1.6f);
}

void CPetTee::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_ClMClientPetTee <= 0)
		return;

	const int PlayerId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(PlayerId < 0)
		return;
	const auto &Player = GameClient()->m_aClients[PlayerId];
	const float Delta = Client()->RenderFrameTime();
	const float Time = Client()->LocalTime();

	const float Scale = (float)g_Config.m_ClMClientPetTeeSize / 100.0f;

	if(Player.m_Active && Player.m_Team != TEAM_SPECTATORS)
	{
		if(m_Alpha == 0.0f)
		{
			m_WanderOffset = vec2(random_float(-40.0f, 40.0f), -(60.0f + Scale * 34.0f));
			PickWanderOffset(Time, Scale);
			PickGazeTarget(Time);
			m_Position = Player.m_RenderPos + m_WanderOffset;
			m_Velocity = vec2(0.0f, 0.0f);
			m_Dir = random_direction();
		}
		if(Time >= m_NextWanderTime)
			PickWanderOffset(Time, Scale);

		m_Target = Player.m_RenderPos + m_WanderOffset;
		m_Target.y += std::sin(Time * 2.3f) * 4.0f;

		if(m_Alpha < 1.0f)
		{
			m_Alpha += Delta;
			if(m_Alpha >= 1.0f)
				m_Alpha = 1.0f;
		}

		m_Position += m_Velocity * Delta;

		const vec2 ToTarget = m_Target - m_Position;
		const float PlayerDist = distance(m_Position, Player.m_RenderPos);

		float k = 14.0f;
		if(PlayerDist > 220.0f)
			k = mix(14.0f, 260.0f, std::clamp((PlayerDist - 220.0f) / 240.0f, 0.0f, 1.0f));
		const float c = 2.0f * std::sqrt(k);
		m_Velocity += (ToTarget * k - m_Velocity * c) * Delta;

		if(PlayerDist > 900.0f)
		{
			m_Position = m_Target;
			m_Velocity = vec2(0.0f, 0.0f);
		}
	}
	else
	{
		if(m_Alpha > 0.0f)
		{
			m_Alpha -= Delta;
			if(m_Alpha <= 0.0f)
				m_Alpha = 0.0f;
		}
	}

	if(m_Alpha <= 0.0f)
		return;

	if(Time >= m_NextGazeTime)
		PickGazeTarget(Time);
	m_Dir = mix(m_Dir, m_GazeTarget, std::clamp(Delta * 2.5f, 0.0f, 1.0f));
	if(length(m_Dir) > 0.001f)
		m_Dir = normalize(m_Dir);

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClMClientPetTeeSkin));
	TeeRenderInfo.m_Size = 64.0f * Scale;
	TeeRenderInfo.m_GotAirJump = m_Velocity.y > -10.0f;
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, m_Dir, m_Position, m_Alpha * (float)g_Config.m_ClMClientPetTeeAlpha / 100.0f);
}

void CPetTee::OnMapLoad()
{
	m_Alpha = 0.0f;
}
