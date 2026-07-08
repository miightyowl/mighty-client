#include "finish_fireworks.h"
#include <base/str.h>
#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/storage.h>
#include <generated/client_data.h>
#include <generated/protocol.h>
#include <game/client/components/particles.h>
#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>

static const float CELEBRATION_DURATION = 6.0f;

void CFinishFireworks::EnsureSoundsLoaded()
{
	if(m_LaunchSample != -2)
		return;
	m_LaunchSample = Storage()->FileExists("audio/firework_launch.wv", IStorage::TYPE_ALL) ? Sound()->LoadWV("audio/firework_launch.wv") : -1;
	m_ExplodeSample = Storage()->FileExists("audio/firework_explode.wv", IStorage::TYPE_ALL) ? Sound()->LoadWV("audio/firework_explode.wv") : -1;
}

void CFinishFireworks::Start()
{
	const float Now = Client()->LocalTime();
	if(Now < m_CelebrationEnd || !GameClient()->m_Snap.m_pLocalCharacter)
		return;
	m_CenterPos = GameClient()->m_LocalCharacterPos;
	m_CelebrationEnd = Now + CELEBRATION_DURATION;
	m_NextRocketTime = Now;
	EnsureSoundsLoaded();
}

void CFinishFireworks::OnMessage(int MsgType, void *pRawMsg)
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;

	if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		const CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		if(pMsg->m_ClientId != -1)
			return;
		char aName[MAX_NAME_LENGTH];
		if(CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName)) > 0 && str_comp(aName, GameClient()->m_aClients[LocalId].m_aName) == 0)
			Start();
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(pMsg->m_ClientId == LocalId)
			Start();
	}
}

void CFinishFireworks::OnRender()
{
	const float Now = Client()->LocalTime();

	if(Now < m_CelebrationEnd && Now >= m_NextRocketTime)
	{
		LaunchRocket(m_CenterPos, Now);
		m_NextRocketTime = Now + random_float(0.1f, 0.35f);
	}

	for(size_t i = 0; i < m_vRockets.size();)
	{
		const SRocket &Rocket = m_vRockets[i];
		const float Progress = (Now - Rocket.m_LaunchTime) / Rocket.m_FlightTime;
		if(Progress >= 1.0f)
		{
			SpawnExplosion(Rocket.m_TargetPos, Rocket.m_Color);
			m_vRockets[i] = m_vRockets.back();
			m_vRockets.pop_back();
			continue;
		}

		const float Eased = 1.0f - (1.0f - Progress) * (1.0f - Progress);
		const vec2 Pos = mix(Rocket.m_StartPos, Rocket.m_TargetPos, Eased);
		for(int Trail = 0; Trail < 2; Trail++)
		{
			CParticle Spark;
			Spark.SetDefault();
			Spark.m_Spr = SPRITE_PART_BALL;
			Spark.m_Pos = Pos + vec2(random_float(-4.0f, 4.0f), random_float(-4.0f, 4.0f));
			Spark.m_Vel = vec2(random_float(-30.0f, 30.0f), random_float(20.0f, 60.0f));
			Spark.m_LifeSpan = random_float(0.25f, 0.4f);
			Spark.m_StartSize = random_float(6.0f, 10.0f);
			Spark.m_EndSize = 0.0f;
			Spark.m_Gravity = 200.0f;
			Spark.m_Friction = 0.7f;
			Spark.m_Color = ColorRGBA(1.0f, 0.9f, 0.6f, 0.8f);
			Spark.m_UseAlphaFading = true;
			Spark.m_StartAlpha = 0.8f;
			Spark.m_EndAlpha = 0.0f;
			Spark.m_Collides = false;
			GameClient()->m_Particles.Add(CParticles::GROUP_GENERAL, &Spark);
		}
		i++;
	}
}

void CFinishFireworks::LaunchRocket(vec2 CenterPos, float Now)
{
	SRocket Rocket;
	Rocket.m_StartPos = CenterPos + vec2(random_float(-60.0f, 60.0f), 0.0f);
	Rocket.m_TargetPos = CenterPos + vec2(random_float(-450.0f, 450.0f), random_float(-550.0f, -250.0f));
	Rocket.m_LaunchTime = Now;
	Rocket.m_FlightTime = random_float(0.4f, 0.7f);
	Rocket.m_Color = color_cast<ColorRGBA>(ColorHSLA(random_float(0.0f, 1.0f), 1.0f, 0.65f));
	m_vRockets.push_back(Rocket);

	if(m_LaunchSample >= 0 && g_Config.m_SndEnable && g_Config.m_SndGame)
		Sound()->PlayAt(CSounds::CHN_WORLD, m_LaunchSample, ISound::FLAG_POS, random_float(0.5f, 0.7f), Rocket.m_StartPos);
}

void CFinishFireworks::SpawnExplosion(vec2 Pos, ColorRGBA Color)
{
	if(m_ExplodeSample >= 0 && g_Config.m_SndEnable && g_Config.m_SndGame)
		Sound()->PlayAt(CSounds::CHN_WORLD, m_ExplodeSample, ISound::FLAG_POS, random_float(0.8f, 1.0f), Pos);

	for(int i = 0; i < 48; i++)
	{
		CParticle Spark;
		Spark.SetDefault();
		Spark.m_Spr = SPRITE_PART_BALL;
		Spark.m_Pos = Pos;
		Spark.m_Vel = direction(random_angle()) * random_float(50.0f, 650.0f);
		Spark.m_LifeSpan = random_float(0.7f, 1.3f);
		Spark.m_StartSize = random_float(8.0f, 14.0f);
		Spark.m_EndSize = 0.0f;
		Spark.m_Gravity = 120.0f;
		Spark.m_Friction = 0.75f;
		Spark.m_Color = Color.WithAlpha(random_float(0.8f, 1.0f));
		Spark.m_UseAlphaFading = true;
		Spark.m_StartAlpha = 1.0f;
		Spark.m_EndAlpha = 0.0f;
		Spark.m_Collides = false;
		GameClient()->m_Particles.Add(CParticles::GROUP_GENERAL, &Spark);
	}

	for(int i = 0; i < 12; i++)
	{
		CParticle Flash;
		Flash.SetDefault();
		Flash.m_Spr = SPRITE_PART_BALL;
		Flash.m_Pos = Pos;
		Flash.m_Vel = direction(random_angle()) * random_float(30.0f, 200.0f);
		Flash.m_LifeSpan = random_float(0.3f, 0.5f);
		Flash.m_StartSize = random_float(10.0f, 16.0f);
		Flash.m_EndSize = 0.0f;
		Flash.m_Friction = 0.8f;
		Flash.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		Flash.m_UseAlphaFading = true;
		Flash.m_StartAlpha = 1.0f;
		Flash.m_EndAlpha = 0.0f;
		Flash.m_Collides = false;
		GameClient()->m_Particles.Add(CParticles::GROUP_GENERAL, &Flash);
	}
}

void CFinishFireworks::OnReset()
{
	m_vRockets.clear();
	m_CelebrationEnd = -1.0f;
	m_NextRocketTime = 0.0f;
}
