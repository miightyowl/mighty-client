#include "girlfriend_tee.h"

#include <base/math.h>
#include <base/mem.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/collision.h>
#include <game/gamecore.h>

#include <cmath>

namespace
{
	constexpr float FOLLOW_OFFSET = 52.0f;
	constexpr float DISTRACT_MIN = 70.0f;
	constexpr float DISTRACT_MAX = 160.0f;
	constexpr float CATCH_UP_DIST = 190.0f;
	constexpr float TELEPORT_DIST = 850.0f;
	constexpr float HAMMER_REACT_DIST = 140.0f;
	constexpr float HAMMER_REACT_WINDOW = 0.55f;
}

void CGirlfriendTee::ResetState()
{
	m_Alpha = 0.0f;
	m_Velocity = vec2(0.0f, 0.0f);
	m_Mood = EMood::FOLLOW;
	m_Initialized = false;
	m_IdleSeconds = 0.0f;
	m_TiredThreshold = random_float(15.0f, 20.0f);
	m_EyeEmote = EMOTE_NORMAL;
	m_EyeEmoteUntil = 0.0f;
	m_Emoticon = -1;
	m_EmoticonStart = -100.0f;
	m_LastSurpriseTime = -100.0f;
	m_LastEmoticonTime = -100.0f;
	m_LastHammerTime = -100.0f;
	m_LastAttackTick = -1;
	m_PrevPlayerSpeed = 0.0f;
	m_WasPlayerFrozen = false;
	mem_zero(m_aWasFrozen, sizeof(m_aWasFrozen));
}

void CGirlfriendTee::OnReset()
{
	ResetState();
}

void CGirlfriendTee::OnMapLoad()
{
	ResetState();
}

void CGirlfriendTee::SpawnBeside(vec2 PlayerPos, vec2 PlayerDir)
{
	const float Side = PlayerDir.x >= 0.0f ? -1.0f : 1.0f;
	m_SideSign = Side;
	m_Position = PlayerPos + vec2(Side * FOLLOW_OFFSET, 0.0f);
	m_Velocity = vec2(0.0f, 0.0f);
	m_Dir = vec2(-Side, 0.0f);
	m_Grounded = Collision()->IsOnGround(m_Position, CCharacterCore::PhysicalSize());
	m_Initialized = true;
	m_Mood = EMood::FOLLOW;
	m_NextDistractTime = Client()->LocalTime() + random_float(4.0f, 10.0f);
	m_NextSideFlipTime = Client()->LocalTime() + random_float(8.0f, 18.0f);
}

void CGirlfriendTee::ShowEmoticon(int Emoticon, float Time, float MinGap)
{
	if(Time - m_LastEmoticonTime < MinGap)
		return;
	m_Emoticon = Emoticon;
	m_EmoticonStart = Time;
	m_LastEmoticonTime = Time;

	int Eye = EMOTE_NORMAL;
	switch(Emoticon)
	{
	case EMOTICON_EXCLAMATION:
	case EMOTICON_QUESTION:
	case EMOTICON_WTF:
		Eye = EMOTE_SURPRISE;
		break;
	case EMOTICON_HEARTS:
	case EMOTICON_EYES:
	case EMOTICON_MUSIC:
		Eye = EMOTE_HAPPY;
		break;
	case EMOTICON_DEVILTEE:
	case EMOTICON_SPLATTEE:
	case EMOTICON_ZOMG:
		Eye = EMOTE_ANGRY;
		break;
	case EMOTICON_ZZZ:
	case EMOTICON_DOTDOT:
	case EMOTICON_DROP:
		Eye = EMOTE_BLINK;
		break;
	case EMOTICON_SORRY:
	case EMOTICON_OOP:
		Eye = EMOTE_PAIN;
		break;
	default:
		break;
	}
	SetEyeEmote(Eye, Time, 2.0f);
}

void CGirlfriendTee::SetEyeEmote(int Emote, float Time, float Duration)
{
	m_EyeEmote = Emote;
	m_EyeEmoteUntil = Time + Duration;
}

void CGirlfriendTee::UpdateReactions(float Time, int LocalId, vec2 PlayerPos, vec2 PlayerVel, bool PlayerFrozen)
{
	const auto &Local = GameClient()->m_aClients[LocalId];
	const float PlayerSpeed = length(PlayerVel);

	// hammer swing edge
	const int AttackTick = Local.m_RenderCur.m_AttackTick;
	if(AttackTick != m_LastAttackTick && AttackTick > 0)
	{
		const int Weapon = Local.m_Predicted.m_ActiveWeapon;
		if(Weapon == WEAPON_HAMMER)
			m_LastHammerTime = Time;
	}
	m_LastAttackTick = AttackTick;

	const bool RecentHammer = (Time - m_LastHammerTime) <= HAMMER_REACT_WINDOW;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == LocalId)
			continue;
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			m_aWasFrozen[ClientId] = false;
			continue;
		}

		const auto &Other = GameClient()->m_aClients[ClientId];
		const bool OtherFrozen = Other.m_FreezeEnd != 0 || Other.m_DeepFrozen || Other.m_LiveFrozen;
		const float Dist = distance(PlayerPos, Other.m_RenderPos);
		if(Dist <= HAMMER_REACT_DIST && RecentHammer)
		{
			if(m_aWasFrozen[ClientId] && !OtherFrozen)
				ShowEmoticon(EMOTICON_HEARTS, Time, 2.5f); // unfroze someone
			else if(!m_aWasFrozen[ClientId] && OtherFrozen)
				ShowEmoticon(EMOTICON_DEVILTEE, Time, 2.5f); // freezeblocked someone
		}
		m_aWasFrozen[ClientId] = OtherFrozen;
	}

	if(PlayerFrozen && !m_WasPlayerFrozen)
		ShowEmoticon(EMOTICON_SORRY, Time, 2.0f);
	else if(!PlayerFrozen && m_WasPlayerFrozen)
		ShowEmoticon(EMOTICON_EYES, Time, 2.0f);
	m_WasPlayerFrozen = PlayerFrozen;

	// idle / tired
	const bool PlayerMoving = PlayerSpeed > 0.35f || absolute(Local.m_Predicted.m_Direction) != 0;
	if(PlayerMoving)
		m_IdleSeconds = 0.0f;
	else
		m_IdleSeconds += Client()->RenderFrameTime();

	if(m_IdleSeconds >= m_TiredThreshold)
	{
		if(m_Mood != EMood::TIRED)
		{
			m_Mood = EMood::TIRED;
			ShowEmoticon(EMOTICON_ZZZ, Time, 4.0f);
			SetEyeEmote(EMOTE_BLINK, Time, 3.0f);
		}
		else if(Time - m_LastEmoticonTime > 5.0f)
			ShowEmoticon(EMOTICON_ZZZ, Time, 4.0f);
	}
	else if(m_Mood == EMood::TIRED)
	{
		m_Mood = EMood::FOLLOW;
		m_TiredThreshold = random_float(15.0f, 20.0f);
	}

	// surprise when suddenly sprinting after being slow
	if(!PlayerFrozen && m_PrevPlayerSpeed < 2.5f && PlayerSpeed > 7.5f &&
		Time - m_LastSurpriseTime > 8.0f && m_IdleSeconds < 2.0f && random_float() < 0.45f)
	{
		ShowEmoticon(EMOTICON_EXCLAMATION, Time, 3.0f);
		m_LastSurpriseTime = Time;
	}
	m_PrevPlayerSpeed = PlayerSpeed;
}

void CGirlfriendTee::UpdateAI(float Time, float Delta, vec2 PlayerPos, vec2 PlayerVel, vec2 PlayerDir, bool PlayerFrozen)
{
	const CTuningParams &Tuning = GameClient()->m_aTuning[g_Config.m_ClDummy];
	const float TickSpeed = (float)Client()->GameTickSpeed();
	const float TickFrac = Delta * TickSpeed;
	const float DistToPlayer = distance(m_Position, PlayerPos);

	if(DistToPlayer > TELEPORT_DIST)
		SpawnBeside(PlayerPos, PlayerDir);

	if(Time >= m_NextSideFlipTime && m_Mood == EMood::FOLLOW)
	{
		m_SideSign = -m_SideSign;
		m_NextSideFlipTime = Time + random_float(10.0f, 22.0f);
	}

	// schedule distractions
	if(m_Mood == EMood::FOLLOW && !PlayerFrozen && Time >= m_NextDistractTime && DistToPlayer < 120.0f)
	{
		if(random_float() < 0.55f)
		{
			const float Angle = random_float(-pi, pi);
			const float Radius = random_float(DISTRACT_MIN, DISTRACT_MAX);
			m_DistractTarget = PlayerPos + vec2(std::cos(Angle), std::sin(Angle) * 0.35f) * Radius;
			m_DistractTarget.y = PlayerPos.y;
			m_Mood = EMood::DISTRACTED;
			m_DistractUntil = Time + random_float(1.6f, 3.8f);
			if(random_float() < 0.25f)
				ShowEmoticon(EMOTICON_DOTDOT, Time, 3.0f);
		}
		m_NextDistractTime = Time + random_float(5.0f, 14.0f);
	}

	if(m_Mood == EMood::DISTRACTED && (Time >= m_DistractUntil || DistToPlayer > CATCH_UP_DIST))
		m_Mood = DistToPlayer > CATCH_UP_DIST ? EMood::CATCH_UP : EMood::FOLLOW;

	if(m_Mood != EMood::TIRED && DistToPlayer > CATCH_UP_DIST)
		m_Mood = EMood::CATCH_UP;
	else if(m_Mood == EMood::CATCH_UP && DistToPlayer < FOLLOW_OFFSET + 20.0f)
		m_Mood = EMood::FOLLOW;

	vec2 Target = PlayerPos + vec2(m_SideSign * FOLLOW_OFFSET, 0.0f);
	if(m_Mood == EMood::DISTRACTED)
		Target = m_DistractTarget;
	else if(m_Mood == EMood::CATCH_UP)
		Target = PlayerPos + vec2(m_SideSign * (FOLLOW_OFFSET * 0.7f), 0.0f);
	else if(m_Mood == EMood::TIRED)
		Target = m_Position; // stay put

	// match player height loosely when following closely
	if(m_Mood == EMood::FOLLOW || m_Mood == EMood::CATCH_UP)
		Target.y = PlayerPos.y;

	m_Grounded = Collision()->IsOnGround(m_Position, CCharacterCore::PhysicalSize());

	float MaxSpeed = m_Grounded ? Tuning.m_GroundControlSpeed : Tuning.m_AirControlSpeed;
	float Accel = m_Grounded ? Tuning.m_GroundControlAccel : Tuning.m_AirControlAccel;
	float Friction = m_Grounded ? Tuning.m_GroundFriction : Tuning.m_AirFriction;
	if(m_Mood == EMood::CATCH_UP)
	{
		MaxSpeed *= 1.45f;
		Accel *= 1.35f;
	}
	else if(m_Mood == EMood::TIRED)
	{
		MaxSpeed *= 0.15f;
		Accel *= 0.2f;
	}
	else if(m_Mood == EMood::DISTRACTED)
	{
		MaxSpeed *= 0.75f;
	}

	// hurry if the player is sprinting away
	const float PlayerSpeed = length(PlayerVel);
	if(m_Mood == EMood::FOLLOW && PlayerSpeed > 6.0f && DistToPlayer > FOLLOW_OFFSET + 10.0f)
	{
		MaxSpeed = std::max(MaxSpeed, PlayerSpeed * 1.05f);
		Accel *= 1.2f;
	}

	const float Dx = Target.x - m_Position.x;
	int Direction = 0;
	if(absolute(Dx) > 4.0f)
		Direction = Dx > 0.0f ? 1 : -1;

	m_Velocity.y += Tuning.m_Gravity * TickFrac;

	if(Direction)
	{
		m_Velocity.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Velocity.x, Direction * Accel * TickFrac);
		m_Dir = mix(m_Dir, vec2((float)Direction, 0.0f), std::clamp(Delta * 8.0f, 0.0f, 1.0f));
	}
	else
	{
		m_Velocity.x *= std::pow(Friction, TickFrac);
		// look at the player when idle
		const vec2 ToPlayer = PlayerPos - m_Position;
		if(length(ToPlayer) > 1.0f)
		{
			const vec2 Look = normalize(vec2(ToPlayer.x, 0.0f));
			if(length(Look) > 0.01f)
				m_Dir = mix(m_Dir, Look, std::clamp(Delta * 3.0f, 0.0f, 1.0f));
		}
	}

	// small hop to catch up over gaps
	if(m_Mood == EMood::CATCH_UP && m_Grounded && DistToPlayer > 120.0f && absolute(Dx) > 40.0f && random_float() < Delta * 1.5f)
		m_Velocity.y = -Tuning.m_GroundJumpImpulse;

	bool GroundedAfter = false;
	Collision()->MoveBox(&m_Position, &m_Velocity, CCharacterCore::PhysicalSizeVec2(),
		vec2(Tuning.m_GroundElasticityX, Tuning.m_GroundElasticityY), &GroundedAfter);
	m_Grounded = GroundedAfter || Collision()->IsOnGround(m_Position, CCharacterCore::PhysicalSize());

	if(length(m_Dir) > 0.001f)
		m_Dir = normalize(m_Dir);
	else
		m_Dir = vec2(1.0f, 0.0f);
}

void CGirlfriendTee::RenderName(float Alpha) const
{
	if(!g_Config.m_ClNamePlates && !g_Config.m_ClNamePlatesOwn)
		return;

	const char *pName = g_Config.m_ClMClientGirlfriendName;
	if(pName[0] == '\0')
		pName = "Girlfriend";

	const float FontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
	const float Width = TextRender()->TextWidth(FontSize, pName);
	const float Offset = (float)g_Config.m_ClNamePlatesOffset;
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);
	TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha);
	TextRender()->Text(m_Position.x - Width / 2.0f, m_Position.y - Offset - FontSize, FontSize, pName, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

void CGirlfriendTee::RenderEmoticon(float Alpha) const
{
	if(m_Emoticon < 0 || m_Emoticon >= NUM_EMOTICONS)
		return;
	if(!g_Config.m_ClShowEmotes)
		return;

	const float SinceStart = Client()->LocalTime() - m_EmoticonStart;
	const float Duration = 2.0f;
	const float FromEnd = Duration - SinceStart;
	if(SinceStart < 0.0f || FromEnd <= 0.0f)
		return;

	float a = 1.0f;
	if(FromEnd < 0.2f)
		a = FromEnd / 0.2f;
	float h = 1.0f;
	if(SinceStart < 0.1f)
		h = SinceStart / 0.1f;

	float Wiggle = 0.0f;
	if(SinceStart < 0.2f)
		Wiggle = SinceStart / 0.2f;
	const float WiggleAngle = std::sin(5.0f * Wiggle);

	Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[m_Emoticon]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(pi / 6.0f * WiggleAngle);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, a * Alpha);
	IGraphics::CQuadItem QuadItem(m_Position.x, m_Position.y - 23.0f - 32.0f * h, 64.0f, 64.0f * h);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CGirlfriendTee::RenderTee(float Alpha) const
{
	const char *pSkinName = g_Config.m_ClMClientForceSkin ? "maodie" : g_Config.m_ClMClientGirlfriendSkin;
	if(pSkinName[0] == '\0')
		pSkinName = "default";

	CTeeRenderInfo TeeRenderInfo;
	TeeRenderInfo.Apply(GameClient()->m_Skins.Find(pSkinName));
	TeeRenderInfo.m_Size = 64.0f;
	TeeRenderInfo.m_GotAirJump = m_Grounded || m_Velocity.y > -1.5f;
	TeeRenderInfo.m_FeetFlipped = false;

	const bool Stationary = absolute(m_Velocity.x) < 0.15f;
	const bool InAir = !m_Grounded;
	const bool Running = absolute(m_Velocity.x) >= 8.0f;

	float WalkTime = std::fmod(m_Position.x, 100.0f) / 100.0f;
	float RunTime = std::fmod(m_Position.x, 200.0f) / 200.0f;
	if(WalkTime < 0.0f)
		WalkTime += 1.0f;
	if(RunTime < 0.0f)
		RunTime += 1.0f;

	CAnimState State;
	State.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
	if(InAir)
		State.Add(&g_pData->m_aAnimations[ANIM_INAIR], 0.0f, 1.0f);
	else if(Stationary)
	{
		if(m_Mood == EMood::TIRED)
		{
			State.Add(m_Dir.x < 0.0f ? &g_pData->m_aAnimations[ANIM_SIT_LEFT] : &g_pData->m_aAnimations[ANIM_SIT_RIGHT], 0.0f, 1.0f);
			TeeRenderInfo.m_FeetFlipped = true;
		}
		else
			State.Add(&g_pData->m_aAnimations[ANIM_IDLE], 0.0f, 1.0f);
	}
	else if(Running)
		State.Add(m_Velocity.x < 0.0f ? &g_pData->m_aAnimations[ANIM_RUN_LEFT] : &g_pData->m_aAnimations[ANIM_RUN_RIGHT], RunTime, 1.0f);
	else
		State.Add(&g_pData->m_aAnimations[ANIM_WALK], WalkTime, 1.0f);

	int Eye = m_EyeEmote;
	if(Client()->LocalTime() > m_EyeEmoteUntil)
		Eye = (m_Mood == EMood::TIRED) ? EMOTE_BLINK : EMOTE_NORMAL;

	RenderTools()->RenderTee(&State, &TeeRenderInfo, Eye, m_Dir, m_Position, Alpha);
}

void CGirlfriendTee::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		ResetState();
		return;
	}
	if(!g_Config.m_ClMClientGirlfriend)
	{
		ResetState();
		return;
	}

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
	{
		if(m_Alpha > 0.0f)
		{
			m_Alpha = std::max(0.0f, m_Alpha - Client()->RenderFrameTime());
			if(m_Alpha > 0.0f)
			{
				RenderTee(m_Alpha);
				RenderEmoticon(m_Alpha);
				RenderName(m_Alpha);
			}
		}
		return;
	}

	const auto &Player = GameClient()->m_aClients[LocalId];
	const vec2 PlayerPos = Player.m_RenderPos;
	const vec2 PlayerVel = Player.m_Predicted.m_Vel;
	vec2 PlayerDir = direction(Player.m_Angle);
	if(absolute(PlayerDir.x) < 0.01f)
	{
		if(Player.m_Predicted.m_Direction != 0)
			PlayerDir = vec2((float)Player.m_Predicted.m_Direction, 0.0f);
		else
			PlayerDir = vec2(1.0f, 0.0f);
	}
	else
		PlayerDir = normalize(vec2(PlayerDir.x, 0.0f));

	const bool PlayerFrozen = Player.m_Predicted.m_FreezeEnd != 0 || Player.m_Predicted.m_DeepFrozen || Player.m_Predicted.m_LiveFrozen;
	const float Delta = Client()->RenderFrameTime();
	const float Time = Client()->LocalTime();

	if(!m_Initialized || m_Alpha <= 0.0f)
		SpawnBeside(PlayerPos, PlayerDir);

	if(m_Alpha < 1.0f)
		m_Alpha = std::min(1.0f, m_Alpha + Delta);

	UpdateReactions(Time, LocalId, PlayerPos, PlayerVel, PlayerFrozen);
	UpdateAI(Time, Delta, PlayerPos, PlayerVel, PlayerDir, PlayerFrozen);

	RenderTee(m_Alpha);
	RenderEmoticon(m_Alpha);
	RenderName(m_Alpha);
}
