#include "finish_fans.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/race.h>
#include <game/client/render.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
	constexpr int TILE_SIZE = 32;
	constexpr int SEARCH_TILES = 8;
	constexpr int MAX_FANS_PER_FINISH = 3;
	constexpr int FAN_SPACING_TILES = 2;
	constexpr int JUMP_ROOM_TILES = 2;
	constexpr float JUMP_HEIGHT = 34.0f;
	constexpr float JUMP_PERIOD = 0.75f;
	constexpr float TEE_HALF = 14.0f;
	constexpr float CELEBRATION_DURATION = 5.0f;
	constexpr float FADE_DURATION = 0.6f;
	constexpr float EMOTICON_PERIOD = 1.6f;
	constexpr float NOTICE_DURATION = 1.2f;
	constexpr float NOTICE_DELAY = 0.35f;
	constexpr float LOOK_DISTANCE = 1200.0f;

	constexpr float CUTE_HUES[] = {0.94f, 0.06f, 0.14f, 0.40f, 0.56f, 0.75f};

	bool IsDangerous(int Index)
	{
		return Index == TILE_DEATH || Index == TILE_FREEZE || Index == TILE_DFREEZE || Index == TILE_LFREEZE;
	}
}

void CFinishFans::OnMapLoad()
{
	FindSpots();
	m_Gone = false;
}

void CFinishFans::OnReset()
{
	m_CelebrationStart = -1.0f;
	m_CelebrationClientId = -1;
	m_Gone = false;
}

void CFinishFans::FindSpots()
{
	m_vFans.clear();
	const CCollision *pCollision = Collision();
	const int Width = pCollision->GetWidth();
	const int Height = pCollision->GetHeight();
	if(Width < 3 || Height < 4)
		return;

	const auto &&TileIs = [&](int x, int y, int Tile) {
		const int Index = y * Width + x;
		return pCollision->GetTileIndex(Index) == Tile || pCollision->GetFrontTileIndex(Index) == Tile;
	};
	const auto &&Inside = [&](int x, int y) {
		return x >= 0 && y >= 0 && x < Width && y < Height;
	};
	const auto &&Solid = [&](int x, int y) {
		return Inside(x, y) && pCollision->IsSolid(x * TILE_SIZE + TILE_SIZE / 2, y * TILE_SIZE + TILE_SIZE / 2);
	};
	const auto &&Free = [&](int x, int y) {
		if(!Inside(x, y) || Solid(x, y))
			return false;
		const int Index = y * Width + x;
		return !IsDangerous(pCollision->GetTileIndex(Index)) && !IsDangerous(pCollision->GetFrontTileIndex(Index));
	};

	std::vector<ivec2> vStarts;
	std::vector<char> vFinish(Width * Height, 0);
	for(int y = 0; y < Height; y++)
	{
		for(int x = 0; x < Width; x++)
		{
			if(TileIs(x, y, TILE_FINISH))
				vFinish[y * Width + x] = 1;
			else if(TileIs(x, y, TILE_START))
				vStarts.emplace_back(x, y);
		}
	}

	std::vector<char> vVisited(Width * Height, 0);
	for(int StartIndex = 0; StartIndex < Width * Height; StartIndex++)
	{
		if(!vFinish[StartIndex] || vVisited[StartIndex])
			continue;

		std::vector<ivec2> vLine;
		std::vector<int> vStack = {StartIndex};
		vVisited[StartIndex] = 1;
		ivec2 Min(Width, Height), Max(-1, -1);
		while(!vStack.empty())
		{
			const int Index = vStack.back();
			vStack.pop_back();
			const ivec2 Tile(Index % Width, Index / Width);
			vLine.push_back(Tile);
			Min = ivec2(std::min(Min.x, Tile.x), std::min(Min.y, Tile.y));
			Max = ivec2(std::max(Max.x, Tile.x), std::max(Max.y, Tile.y));
			for(int dy = -1; dy <= 1; dy++)
			{
				for(int dx = -1; dx <= 1; dx++)
				{
					const int NextX = Tile.x + dx;
					const int NextY = Tile.y + dy;
					if(!Inside(NextX, NextY))
						continue;
					const int Next = NextY * Width + NextX;
					if(vFinish[Next] && !vVisited[Next])
					{
						vVisited[Next] = 1;
						vStack.push_back(Next);
					}
				}
			}
		}

		const vec2 LineCenter = (vec2(Min.x + Max.x + 1, Min.y + Max.y + 1) / 2.0f) * (float)TILE_SIZE;
		const ivec2 *pStart = nullptr;
		float StartDistance = 0.0f;
		for(const ivec2 &Start : vStarts)
		{
			const float Distance = distance(vec2(Start.x, Start.y) * (float)TILE_SIZE, LineCenter);
			if(!pStart || Distance < StartDistance)
			{
				pStart = &Start;
				StartDistance = Distance;
			}
		}

		struct SCandidate
		{
			ivec2 m_Tile;
			float m_Distance;
			vec2 m_FinishTilePos;
		};
		std::vector<SCandidate> vCandidates;
		for(int y = std::max(Min.y - SEARCH_TILES, 0); y <= std::min(Max.y + SEARCH_TILES, Height - 2); y++)
		{
			for(int x = std::max(Min.x - SEARCH_TILES, 1); x <= std::min(Max.x + SEARCH_TILES, Width - 2); x++)
			{
				if(!Solid(x - 1, y + 1) || !Solid(x, y + 1) || !Solid(x + 1, y + 1))
					continue;
				if(!Free(x, y) || !Free(x - 1, y) || !Free(x + 1, y))
					continue;
				bool JumpRoom = true;
				for(int Up = 1; Up <= JUMP_ROOM_TILES && JumpRoom; Up++)
					JumpRoom = Free(x, y - Up);
				if(!JumpRoom)
					continue;

				const vec2 SpotPos = vec2(x + 0.5f, y + 0.5f) * (float)TILE_SIZE;
				if(pStart && !vFinish[y * Width + x] && distance(vec2(pStart->x, pStart->y) * (float)TILE_SIZE, SpotPos) < StartDistance)
					continue;

				float BestDistance = -1.0f;
				vec2 BestFinish;
				for(const ivec2 &Tile : vLine)
				{
					const vec2 TilePos = vec2(Tile.x + 0.5f, Tile.y + 0.5f) * (float)TILE_SIZE;
					const float Distance = distance(TilePos, SpotPos);
					if(BestDistance < 0.0f || Distance < BestDistance)
					{
						BestDistance = Distance;
						BestFinish = TilePos;
					}
				}
				if(BestDistance > SEARCH_TILES * TILE_SIZE || pCollision->IntersectLine(BestFinish, SpotPos, nullptr, nullptr))
					continue;
				vCandidates.push_back({ivec2(x, y), BestDistance, BestFinish});
			}
		}

		std::sort(vCandidates.begin(), vCandidates.end(), [](const SCandidate &Left, const SCandidate &Right) {
			return Left.m_Distance < Right.m_Distance;
		});
		int NumFans = 0;
		const size_t FirstFan = m_vFans.size();
		for(const SCandidate &Candidate : vCandidates)
		{
			if(NumFans >= MAX_FANS_PER_FINISH)
				break;
			bool TooClose = false;
			for(size_t i = FirstFan; i < m_vFans.size() && !TooClose; i++)
			{
				const vec2 Other = m_vFans[i].m_Pos / (float)TILE_SIZE;
				TooClose = absolute(Other.x - (Candidate.m_Tile.x + 0.5f)) < FAN_SPACING_TILES + 0.5f && absolute(Other.y - (Candidate.m_Tile.y + 1.0f)) < 2.0f;
			}
			if(TooClose)
				continue;

			SFan Fan;
			Fan.m_Pos = vec2((Candidate.m_Tile.x + 0.5f) * TILE_SIZE, (Candidate.m_Tile.y + 1) * TILE_SIZE - TEE_HALF);
			Fan.m_FinishPos = LineCenter;
			Fan.m_Dir = normalize(Candidate.m_FinishTilePos - Fan.m_Pos);
			Fan.m_Phase = (float)((Candidate.m_Tile.x * 7 + Candidate.m_Tile.y * 13) % 100) / 100.0f;
			const float Hue = CUTE_HUES[(Candidate.m_Tile.x * 3 + Candidate.m_Tile.y + (int)m_vFans.size()) % std::size(CUTE_HUES)];
			Fan.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.85f, 0.82f, 1.0f));
			Fan.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(Hue, 0.6f, 0.68f, 1.0f));
			Fan.m_Weapon = (Candidate.m_Tile.x + Candidate.m_Tile.y + (int)m_vFans.size()) % 2 == 0 ? WEAPON_HAMMER : WEAPON_GUN;
			m_vFans.push_back(Fan);
			NumFans++;
		}
	}
}

void CFinishFans::StartCelebration(int ClientId)
{
	if(m_Gone)
		return;
	m_CelebrationStart = LocalTime();
	m_CelebrationClientId = ClientId;
	m_FinishedRaceTick = GameClient()->LastRaceTick();
}

void CFinishFans::OnMessage(int MsgType, void *pRawMsg)
{
	if(!g_Config.m_ClMClientFinishFans)
		return;
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
			StartCelebration(LocalId);
	}
	else if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		if(pMsg->m_ClientId == LocalId)
			StartCelebration(LocalId);
	}
}

void CFinishFans::RenderWeapon(const SFan &Fan, const CAnimState &State, vec2 Pos, float Time, bool Celebrating, float Alpha)
{
	const auto &WeaponData = g_pData->m_Weapons.m_aId[Fan.m_Weapon];
	float ScaleX, ScaleY;
	Graphics()->GetSpriteScale(WeaponData.m_pSpriteBody, ScaleX, ScaleY);
	const bool Left = Fan.m_Dir.x < 0.0f;

	vec2 WeaponPos;
	float Rotation;
	if(Fan.m_Weapon == WEAPON_HAMMER)
	{
		WeaponPos = Pos + vec2(State.GetAttach()->m_X, State.GetAttach()->m_Y);
		WeaponPos.y += WeaponData.m_Offsety;
		if(Left)
			WeaponPos.x -= WeaponData.m_Offsetx;
		const float Swing = Celebrating ? 0.3f * std::sin((Time + Fan.m_Phase) * 9.0f) : 0.0f;
		Rotation = Left ? 100.0f - Swing : 500.0f + Swing;
	}
	else
	{
		WeaponPos = Pos + Fan.m_Dir * WeaponData.m_Offsetx;
		WeaponPos.y += WeaponData.m_Offsety;
		if(g_Config.m_ClOldGunPosition)
			WeaponPos.y -= 8.0f;
		Rotation = State.GetAttach()->m_Angle * pi * 2.0f + angle(Fan.m_Dir);
	}

	Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeapons[Fan.m_Weapon]);
	Graphics()->QuadsBegin();
	if(Left)
		Graphics()->QuadsSetSubset(0.0f, 1.0f, 1.0f, 0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->QuadsSetRotation(Rotation);
	IGraphics::CQuadItem QuadItem(WeaponPos.x, WeaponPos.y, WeaponData.m_VisualSize * ScaleX, WeaponData.m_VisualSize * ScaleY);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CFinishFans::PickIdleGaze(SFan &Fan, float Time)
{
	Fan.m_IdleAtFinish = random_float() < 0.2f;
	if(!Fan.m_IdleAtFinish)
	{
		const bool Left = random_float() < 0.5f;
		const float Elevation = random_float(-0.6f, 0.3f);
		Fan.m_IdleDir = vec2(Left ? -std::cos(Elevation) : std::cos(Elevation), std::sin(Elevation));
	}
	Fan.m_NextGlance = Time + random_float(1.5f, 5.0f);
}

void CFinishFans::RenderFan(SFan &Fan, float Time, float FrameTime, float Alpha)
{
	const float SinceCelebration = Time - m_CelebrationStart;
	const bool Celebrating = m_CelebrationStart >= 0.0f && SinceCelebration < CELEBRATION_DURATION;

	float JumpOffset = 0.0f;
	bool InAir = false;
	if(Celebrating)
	{
		const float JumpTime = std::fmod(SinceCelebration / JUMP_PERIOD + Fan.m_Phase, 1.0f);
		if(JumpTime < 0.6f)
		{
			const float t = JumpTime / 0.6f;
			JumpOffset = 4.0f * JUMP_HEIGHT * t * (1.0f - t);
			InAir = true;
		}
	}
	const vec2 Pos = Fan.m_Pos - vec2(0.0f, JumpOffset);

	const auto &&CanSee = [&](vec2 PlayerPos) {
		return !Collision()->IntersectLine(Fan.m_Pos, PlayerPos, nullptr, nullptr);
	};
	std::optional<vec2> Target;
	if(Celebrating && m_CelebrationClientId >= 0 && GameClient()->m_Snap.m_aCharacters[m_CelebrationClientId].m_Active &&
		CanSee(GameClient()->m_aClients[m_CelebrationClientId].m_RenderPos))
		Target = GameClient()->m_aClients[m_CelebrationClientId].m_RenderPos;
	else if(GameClient()->m_Snap.m_pLocalCharacter && distance(GameClient()->m_LocalCharacterPos, Fan.m_Pos) < LOOK_DISTANCE &&
		CanSee(GameClient()->m_LocalCharacterPos))
	{
		Target = GameClient()->m_LocalCharacterPos;
		if(!Fan.m_Noticed)
		{
			Fan.m_Noticed = true;
			Fan.m_NoticedAt = Time + Fan.m_Phase * NOTICE_DELAY;
		}
	}

	vec2 WantedDir = Fan.m_Dir;
	float TurnSpeed = 6.0f;
	if(Target.has_value())
	{
		if(distance(*Target, Pos) > 1.0f)
			WantedDir = normalize(*Target - Pos);
	}
	else
	{
		if(Time >= Fan.m_NextGlance)
			PickIdleGaze(Fan, Time);
		if(Fan.m_IdleAtFinish && distance(Fan.m_FinishPos, Pos) > 1.0f)
			WantedDir = normalize(Fan.m_FinishPos - Pos);
		else
			WantedDir = Fan.m_IdleDir;
		TurnSpeed = 4.0f;
	}
	Fan.m_Dir = normalize(mix(Fan.m_Dir, WantedDir, 1.0f - std::exp(-FrameTime * TurnSpeed)));

	CAnimState State;
	State.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
	State.Add(&g_pData->m_aAnimations[InAir ? ANIM_INAIR : ANIM_IDLE], 0.0f, 1.0f);

	const float SinceNoticed = Fan.m_NoticedAt >= 0.0f ? Time - Fan.m_NoticedAt : -1.0f;
	const bool Surprised = !Celebrating && SinceNoticed >= 0.0f && SinceNoticed < NOTICE_DURATION;

	int Emote = EMOTE_NORMAL;
	if(Celebrating)
		Emote = EMOTE_HAPPY;
	else if(Surprised)
		Emote = EMOTE_SURPRISE;
	else if(std::fmod(Time + Fan.m_Phase * 5.0f, 5.0f) < 0.15f)
		Emote = EMOTE_BLINK;

	CTeeRenderInfo TeeInfo;
	TeeInfo.Apply(GameClient()->m_Skins.Find("twinbop"));
	TeeInfo.m_CustomColoredSkin = true;
	TeeInfo.m_ColorBody = Fan.m_ColorBody;
	TeeInfo.m_ColorFeet = Fan.m_ColorFeet;
	TeeInfo.m_Size = 64.0f;
	TeeInfo.m_GotAirJump = true;
	RenderWeapon(Fan, State, Pos, Time, Celebrating, Alpha);
	RenderTools()->RenderTee(&State, &TeeInfo, Emote, Fan.m_Dir, Pos, Alpha);

	if(Surprised)
	{
		RenderEmoticon(EMOTICON_EXCLAMATION, Pos, SinceNoticed, NOTICE_DURATION, Alpha);
		return;
	}

	if(!Celebrating)
		return;
	const float EmoticonTime = std::fmod(SinceCelebration + Fan.m_Phase * EMOTICON_PERIOD, EMOTICON_PERIOD);
	RenderEmoticon(EMOTICON_HEARTS, Pos, EmoticonTime, EMOTICON_PERIOD * 0.85f, Alpha);
}

void CFinishFans::RenderEmoticon(int Emoticon, vec2 Pos, float EmoticonTime, float Length, float Alpha)
{
	if(EmoticonTime < 0.0f || EmoticonTime > Length)
		return;
	const float Grow = std::min(EmoticonTime / 0.1f, 1.0f);
	const float FadeOut = std::min((Length - EmoticonTime) / 0.2f, 1.0f);
	const float Wiggle = std::min(EmoticonTime / 0.2f, 1.0f);
	Graphics()->TextureSet(GameClient()->m_EmoticonsSkin.m_aSpriteEmoticons[Emoticon]);
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(pi / 6.0f * std::sin(2.0f * pi * Wiggle));
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, FadeOut * Alpha);
	IGraphics::CQuadItem QuadItem(Pos.x, Pos.y - 23.0f - 32.0f * Grow, 64.0f, 64.0f * Grow);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CFinishFans::OnRender()
{
	if(!g_Config.m_ClMClientFinishFans || m_vFans.empty())
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const CScreenRect OldScreen = Graphics()->GetScreen();
	Graphics()->MapScreenToInterface(GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y, GameClient()->m_Camera.m_Zoom);
	const CScreenRect Screen = Graphics()->GetScreen();

	const float Time = LocalTime();
	const float FrameTime = Client()->RenderFrameTime();

	if(m_Gone)
	{
		const int RaceTick = GameClient()->LastRaceTick();
		if(RaceTick > 0 && RaceTick != m_FinishedRaceTick)
			m_Gone = false;
		else
			return;
	}
	const int RunTick = GameClient()->LastRaceTick();
	if(RunTick != m_NoticeRaceTick)
	{
		m_NoticeRaceTick = RunTick;
		for(SFan &Fan : m_vFans)
		{
			Fan.m_Noticed = false;
			Fan.m_NoticedAt = -1.0f;
		}
	}

	float Alpha = 1.0f;
	if(m_CelebrationStart >= 0.0f)
	{
		const float SinceCelebration = Time - m_CelebrationStart;
		if(SinceCelebration >= CELEBRATION_DURATION)
		{
			m_CelebrationStart = -1.0f;
			m_Gone = true;
			return;
		}
		Alpha = std::clamp((CELEBRATION_DURATION - SinceCelebration) / FADE_DURATION, 0.0f, 1.0f);
	}

	constexpr float MARGIN = 100.0f;
	for(SFan &Fan : m_vFans)
	{
		if(Fan.m_Pos.x + MARGIN < Screen.m_TopLeft.x || Fan.m_Pos.x - MARGIN > Screen.m_BottomRight.x ||
			Fan.m_Pos.y + MARGIN < Screen.m_TopLeft.y || Fan.m_Pos.y - MARGIN > Screen.m_BottomRight.y)
			continue;
		RenderFan(Fan, Time, FrameTime, Alpha);
	}

	Graphics()->MapScreen(OldScreen);
}
