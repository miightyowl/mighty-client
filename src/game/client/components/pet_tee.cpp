#include "pet_tee.h"

#include <engine/client.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/collision.h>

namespace
{
	const float OTHERS_SIZE = 0.6f;
	const float OTHERS_ALPHA = 0.9f;
}

namespace
{
	float RestClearance(float Scale)
	{
		const float PlayerRadius = 32.0f;
		const float PetRadius = 32.0f * Scale;
		return PlayerRadius + PetRadius + 10.0f;
	}

	enum
	{
		SIDE_LEFT = -1,
		SIDE_EITHER = 0,
		SIDE_RIGHT = 1,
		SIDE_ANYWHERE = 2,
	};

	float SideMin(float Clearance)
	{
		return Clearance * 0.8f;
	}

	float TrailMin(float Clearance)
	{
		return SideMin(Clearance) + 100.0f;
	}

	bool OnSide(vec2 Offset, int Side, float Clearance)
	{
		if(Side == SIDE_ANYWHERE)
			return true;
		if(Side == SIDE_EITHER)
			return absolute(Offset.x) >= SideMin(Clearance);
		return Offset.x * Side >= TrailMin(Clearance);
	}

	vec2 KeepOffTee(vec2 Offset, float Clearance)
	{
		const float Dist = length(Offset);
		if(Dist >= Clearance)
			return Offset;
		const vec2 Dir = Dist > 0.001f ? Offset / Dist : vec2(0.0f, -1.0f);
		return Dir * Clearance;
	}
}

bool CPetTee::SpotFree(vec2 PlayerPos, vec2 Offset, float Scale) const
{
	const CCollision *pCollision = Collision();
	const vec2 Center = PlayerPos + Offset;
	const float Radius = 32.0f * Scale + 6.0f;
	const float Step = 12.0f;
	const int NumSteps = (int)(2.0f * Radius / Step);
	for(int StepY = 0; StepY <= NumSteps; StepY++)
	{
		for(int StepX = 0; StepX <= NumSteps; StepX++)
		{
			const vec2 Point = vec2(-Radius + StepX * Step, -Radius + StepY * Step);
			if(length_squared(Point) <= Radius * Radius && pCollision->CheckPoint(Center + Point))
				return false;
		}
	}
	for(int i = 0; i < 16; i++)
	{
		const float Angle = i * (2.0f * pi / 16.0f);
		if(pCollision->CheckPoint(Center + direction(Angle) * Radius))
			return false;
	}
	return pCollision->IntersectLine(PlayerPos, Center, nullptr, nullptr) == 0;
}

void CPetTee::PickWanderOffset(CPet &Pet, float Time, float Scale, vec2 PlayerPos)
{
	const float BaseRadius = 55.0f + Scale * 34.0f;
	const float Clearance = RestClearance(Scale);
	const vec2 Current = KeepOffTee(Pet.m_WanderOffset, Clearance);
	const bool CurrentFree = SpotFree(PlayerPos, Current, Scale);
	const int WantedSide = Pet.m_TrailSide != 0 ? Pet.m_TrailSide : SIDE_EITHER;

	if(CurrentFree && OnSide(Current, WantedSide, Clearance) && random_float() < 0.4f)
	{
		Pet.m_NextWanderTime = Time + random_float(1.8f, 4.0f);
		return;
	}

	const int aSides[] = {WantedSide, SIDE_EITHER, SIDE_ANYWHERE};
	bool Found = false;
	vec2 NewSpot = Pet.m_WanderOffset;
	int UsedSide = WantedSide;
	for(int Pass = 0; Pass < (int)std::size(aSides) && !Found; Pass++)
	{
		const int Side = aSides[Pass];
		if(Pass > 0)
		{
			if(Side == aSides[Pass - 1])
				continue;
			if(CurrentFree && OnSide(Current, Side, Clearance))
				break;
		}
		UsedSide = Side;
		for(int Try = 0; Try < 32; Try++)
		{
			const float Radius = Try < 16 ? BaseRadius * random_float(0.8f, 1.25f) : random_float(Clearance, BaseRadius * 1.25f);
			vec2 Spot;
			if(Side == SIDE_ANYWHERE)
			{
				Spot = random_direction() * Radius;
				Spot.y -= 12.0f;
			}
			else
			{
				const int Dir = Side == SIDE_EITHER ? (random_float() < 0.5f ? -1 : 1) : Side;
				const float MinX = Side == SIDE_EITHER ? SideMin(Clearance) : TrailMin(Clearance);
				const float MaxX = Side == SIDE_EITHER ? Radius : Radius + 100.0f;
				Spot.x = random_float(MinX, std::max(MinX, MaxX)) * Dir;
				Spot.y = -random_float(0.0f, Radius * 0.6f) - 12.0f;
				Spot.y = std::max(Spot.y, -absolute(Spot.x));
			}
			Spot = KeepOffTee(Spot, Clearance);
			if(OnSide(Spot, Side, Clearance) && SpotFree(PlayerPos, Spot, Scale))
			{
				NewSpot = Spot;
				Found = true;
				break;
			}
		}
	}

	if(!Found)
	{
		Pet.m_NextWanderTime = Time + 0.5f;
		return;
	}

	const vec2 Mixed = KeepOffTee(mix(Pet.m_WanderOffset, NewSpot, random_float(0.35f, 1.0f)), Clearance);
	Pet.m_WanderOffset = SpotFree(PlayerPos, Mixed, Scale) && OnSide(Mixed, UsedSide, Clearance) ? Mixed : NewSpot;
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

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const char *pSkinName = nullptr;
		bool UseCustomColor = false;
		int ColorBody = 0;
		int ColorFeet = 0;
		float Scale = OTHERS_SIZE;
		float Alpha = OTHERS_ALPHA;

		const bool LocalPlayer = ClientId == GameClient()->m_aLocalIds[IClient::CONN_MAIN];
		const bool LocalDummy = ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY];
		if(LocalPlayer || LocalDummy)
		{
			const bool Dummy = LocalDummy;
			const bool Enabled = Dummy ? g_Config.m_ClMClientDummyPetTee != 0 : g_Config.m_ClMClientPetTee != 0;
			if(Enabled)
			{
				pSkinName = Dummy ? g_Config.m_ClMClientDummyPetTeeSkin : g_Config.m_ClMClientPetTeeSkin;
				UseCustomColor = Dummy ? g_Config.m_ClMClientDummyPetTeeUseCustomColor != 0 : g_Config.m_ClMClientPetTeeUseCustomColor != 0;
				ColorBody = Dummy ? g_Config.m_ClMClientDummyPetTeeColorBody : g_Config.m_ClMClientPetTeeColorBody;
				ColorFeet = Dummy ? g_Config.m_ClMClientDummyPetTeeColorFeet : g_Config.m_ClMClientPetTeeColorFeet;
				Scale = (float)(Dummy ? g_Config.m_ClMClientDummyPetTeeSize : g_Config.m_ClMClientPetTeeSize) / 100.0f;
				Alpha = (float)(Dummy ? g_Config.m_ClMClientDummyPetTeeAlpha : g_Config.m_ClMClientPetTeeAlpha) / 100.0f;
			}
		}
		else if(g_Config.m_ClMClientPetTeeOthers)
		{
			const CMClientDetect::SPetAppearance Appearance = GameClient()->m_MClientDetect.PetAppearance(ClientId);
			pSkinName = Appearance.m_pSkin;
			UseCustomColor = Appearance.m_UseCustomColor;
			ColorBody = Appearance.m_ColorBody;
			ColorFeet = Appearance.m_ColorFeet;
			Scale = (float)g_Config.m_ClMClientPetTeeSize / 100.0f;
			Alpha = (float)g_Config.m_ClMClientPetTeeAlpha / 100.0f;
		}

		if(pSkinName == nullptr)
		{
			m_aPets[ClientId].m_Alpha = 0.0f;
			continue;
		}

		RenderPet(ClientId, g_Config.m_ClMClientForceSkin ? "maodie" : pSkinName, UseCustomColor, ColorBody, ColorFeet, Scale, Alpha);
	}
}

void CPetTee::RenderPet(int ClientId, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, float Scale, float Alpha)
{
	CPet &Pet = m_aPets[ClientId];
	const auto &Player = GameClient()->m_aClients[ClientId];
	const float Delta = Client()->RenderFrameTime();
	const float Time = Client()->LocalTime();

	const bool Local = ClientId == GameClient()->m_aLocalIds[IClient::CONN_MAIN] || ClientId == GameClient()->m_aLocalIds[IClient::CONN_DUMMY];
	const bool Following = Player.m_Active && Player.m_Team != TEAM_SPECTATORS &&
			       (Local || GameClient()->m_Snap.m_aCharacters[ClientId].m_Active);

	if(Following)
	{
		if(Pet.m_Alpha == 0.0f)
		{
			Pet.m_WanderOffset = vec2((random_float() < 0.5f ? -1.0f : 1.0f) * (RestClearance(Scale) + 10.0f), -20.0f);
			PickWanderOffset(Pet, Time, Scale, Player.m_RenderPos);
			PickGazeTarget(Pet, Time);
			Pet.m_Position = Player.m_RenderPos + Pet.m_WanderOffset;
			Pet.m_Velocity = vec2(0.0f, 0.0f);
			Pet.m_Dir = random_direction();
			Pet.m_LastPlayerPos = Player.m_RenderPos;
			Pet.m_PlayerVel = vec2(0.0f, 0.0f);
			Pet.m_PlayerVelRaw = vec2(0.0f, 0.0f);
			Pet.m_TrailSide = 0;
		}

		if(Delta > 0.0f)
		{
			const vec2 Moved = Player.m_RenderPos - Pet.m_LastPlayerPos;
			const vec2 Vel = length(Moved) > 300.0f ? vec2(0.0f, 0.0f) : Moved / Delta;
			Pet.m_PlayerVel = mix(Pet.m_PlayerVel, Vel, std::clamp(Delta * 6.0f, 0.0f, 1.0f));
			Pet.m_PlayerVelRaw = Vel;
		}
		Pet.m_LastPlayerPos = Player.m_RenderPos;

		const int OldTrailSide = Pet.m_TrailSide;
		if(Pet.m_PlayerVel.x > 220.0f)
			Pet.m_TrailSide = -1;
		else if(Pet.m_PlayerVel.x < -220.0f)
			Pet.m_TrailSide = 1;
		else if(absolute(Pet.m_PlayerVel.x) < 60.0f)
			Pet.m_TrailSide = 0;
		if(Pet.m_TrailSide != OldTrailSide)
			Pet.m_NextBlockedCheck = Time;

		if(Time >= Pet.m_NextWanderTime)
			PickWanderOffset(Pet, Time, Scale, Player.m_RenderPos);
		Pet.m_WanderOffset = KeepOffTee(Pet.m_WanderOffset, RestClearance(Scale));

		if(Time >= Pet.m_NextBlockedCheck)
		{
			Pet.m_NextBlockedCheck = Time + 0.15f;
			if(!SpotFree(Player.m_RenderPos, Pet.m_WanderOffset, Scale) || !OnSide(Pet.m_WanderOffset, Pet.m_TrailSide != 0 ? Pet.m_TrailSide : SIDE_EITHER, RestClearance(Scale)))
				PickWanderOffset(Pet, Time, Scale, Player.m_RenderPos);
		}

		vec2 Lag = -Pet.m_PlayerVel * 0.08f;
		if(length(Lag) > 80.0f)
			Lag = normalize(Lag) * 80.0f;

		Pet.m_Target = Player.m_RenderPos + Pet.m_WanderOffset + Lag;
		Pet.m_Target.y += std::sin(Time * 2.3f) * 4.0f;

		if(Pet.m_Alpha < 1.0f)
		{
			Pet.m_Alpha += Delta;
			if(Pet.m_Alpha >= 1.0f)
				Pet.m_Alpha = 1.0f;
		}

		const float Stiffness = 20.0f;
		const float Damping = 2.0f * std::sqrt(Stiffness);
		const vec2 TargetVel = Pet.m_PlayerVel;

		const float Clearance = RestClearance(Scale);
		const bool UseCushion = absolute(Pet.m_WanderOffset.x) >= SideMin(Clearance);
		const float SpotSide = Pet.m_WanderOffset.x >= 0.0f ? 1.0f : -1.0f;
		const float HardMin = Clearance * 0.75f;
		const float CushionStart = HardMin + 30.0f;

		float Remaining = Delta;
		while(Remaining > 0.0f)
		{
			const float Step = std::min(Remaining, 1.0f / 120.0f);
			Pet.m_Velocity += ((Pet.m_Target - Pet.m_Position) * Stiffness + (TargetVel - Pet.m_Velocity) * Damping) * Step;

			const float RelX = (Pet.m_Position.x - Player.m_RenderPos.x) * SpotSide;
			const bool Beside = UseCushion && RelX > 0.0f && absolute(Pet.m_Position.y - Player.m_RenderPos.y) < Clearance * 1.5f;
			if(Beside && RelX < CushionStart)
			{
				const float Inward = -(Pet.m_Velocity.x - Pet.m_PlayerVelRaw.x) * SpotSide;
				if(Inward > 0.0f)
				{
					const float Depth = std::clamp((CushionStart - RelX) / (CushionStart - HardMin), 0.0f, 1.0f);
					Pet.m_Velocity.x += SpotSide * Inward * (1.0f - std::exp(-Step * 30.0f * Depth));
				}
			}

			Pet.m_Position += Pet.m_Velocity * Step;

			if(Beside && RelX >= HardMin && (Pet.m_Position.x - Player.m_RenderPos.x) * SpotSide < HardMin)
			{
				Pet.m_Position.x = Player.m_RenderPos.x + SpotSide * HardMin;
				const float Inward = -(Pet.m_Velocity.x - Pet.m_PlayerVelRaw.x) * SpotSide;
				if(Inward > 0.0f)
					Pet.m_Velocity.x += SpotSide * Inward;
			}
			Remaining -= Step;
		}

		const float PlayerDist = distance(Pet.m_Position, Player.m_RenderPos);
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
	TeeRenderInfo.ApplyColors(UseCustomColor, ColorBody, ColorFeet);
	TeeRenderInfo.m_Size = 64.0f * Scale;
	TeeRenderInfo.m_GotAirJump = Pet.m_Velocity.y > -10.0f;
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, EMOTE_NORMAL, Pet.m_Dir, Pet.m_Position, Pet.m_Alpha * Alpha);
}

void CPetTee::OnMapLoad()
{
	for(CPet &Pet : m_aPets)
		Pet.m_Alpha = 0.0f;
}
