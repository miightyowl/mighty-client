#include "maodie_walk.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <cmath>

namespace {
	constexpr float SIZE_FRACTION = 0.9f;
	constexpr float FLOOR_OFFSET_FRACTION = 0.25f;
	constexpr float CROSS_TIME = 7.0f;
	constexpr float ANIM_SPEED = 3.0f;
	constexpr float MIN_INTERVAL = 120.0f;
	constexpr float MAX_INTERVAL = 300.0f;
}

void CMaodieWalk::OnReset()
{
	m_Active = false;
	m_NextWalkTime = 0.0f;
}

void CMaodieWalk::ScheduleNext()
{
	m_NextWalkTime = LocalTime() + random_float(MIN_INTERVAL, MAX_INTERVAL);
}

void CMaodieWalk::OnRender()
{
	const bool Ingame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!Ingame || !g_Config.m_ClMClientMaodieWalk)
	{
		OnReset();
		return;
	}

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();

	const float Size = Screen.h * SIZE_FRACTION;
	const float StartX = Screen.x - Size;
	const float EndX = Screen.x + Screen.w + Size;

	if(!m_Active)
	{
		// don't walk in while the menu is open
		if(GameClient()->m_Menus.IsActive())
			return;
		if(m_NextWalkTime <= 0.0f)
		{
			ScheduleNext();
			return;
		}
		if(LocalTime() < m_NextWalkTime)
			return;

		m_Active = true;
		m_Dir = random_float() < 0.5f ? 1.0f : -1.0f;
		m_PosX = m_Dir > 0.0f ? StartX : EndX;
	}

	m_PosX += m_Dir * (EndX - StartX) / CROSS_TIME * Client()->RenderFrameTime();
	if((m_Dir > 0.0f && m_PosX > EndX) || (m_Dir < 0.0f && m_PosX < StartX))
	{
		m_Active = false;
		ScheduleNext();
		return;
	}

	const float AnimScale = Size / 64.0f;
	float WalkTime = std::fmod(m_PosX / AnimScale * ANIM_SPEED, 100.0f) / 100.0f;
	if(WalkTime < 0.0f)
		WalkTime += 1.0f;

	CAnimState State;
	State.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
	State.Add(&g_pData->m_aAnimations[ANIM_WALK], WalkTime, 1.0f);

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.Apply(GameClient()->m_Skins.Find("maodie"));
	TeeRenderInfo.m_Size = Size;
	TeeRenderInfo.m_GotAirJump = true;

	// the bottom of the screen is the floor he walks on
	const vec2 Pos = vec2(m_PosX, Screen.y + Screen.h - Size * FLOOR_OFFSET_FRACTION);
	RenderTools()->RenderTee(&State, &TeeRenderInfo, EMOTE_HAPPY, vec2(m_Dir, 0.0f), Pos);
}
