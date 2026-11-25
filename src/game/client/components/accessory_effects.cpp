#include "accessory_effects.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>

namespace
{
	const char *EFFECT_KEY_COMMAND = "+accessory_effect";

	constexpr float HELD_TIME = 0.15f;
	constexpr float MAX_EMIT_LAG = 0.1f;
	constexpr float MAX_FRAME_TIME = 0.1f;
	constexpr size_t MAX_PARTICLES = 4096;

	constexpr float BEAM_LENGTH = 700.0f;
	constexpr float PREVIEW_BEAM_LENGTH = 70.0f;
	constexpr float PREVIEW_REACH = 0.35f;
	constexpr float PREVIEW_SIZE = 0.6f;
	constexpr float PREVIEW_PAUSE = 0.25f;

	constexpr int RAINBOW_LANES = 6;
	constexpr float MONEY_HEIGHT = 120.0f;
	constexpr float MONEY_WIDTH = 100.0f;
	constexpr float CLOUD_HEIGHT = 72.0f;
	constexpr float LIGHTNING_DELAY = 1.2f;
	constexpr float LIGHTNING_CHANCE = 0.015f;
	constexpr float BOLT_LENGTH = 220.0f;
	constexpr int BOLT_PARTS = 8;
	constexpr float SLAM_DEPTH = 96.0f;
	const ColorRGBA COLOR_WHITE = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	const ColorRGBA COLOR_BLACK = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);

	constexpr float BURST_TIME = 0.25f;
	constexpr float MIN_PRESS_TIME = 0.1f;

	const float s_aEmitIntervals[CAccessories::NUM_INTERACTIVE] = {
		1.0f,
		1.0f / 110.0f,
		1.0f / 80.0f,
		1.0f / 16.0f,
		1.0f / 45.0f,
		0.4f,
		1.0f / 40.0f,
		1.0f / 8.0f,
		1.0f / 22.0f,
		1.0f / 50.0f,
		1.0f / 50.0f,
		0.5f,
		0.35f,
		1.0f / 60.0f,
	};

	struct SColorStop
	{
		float m_Age;
		ColorRGBA m_Color;
	};

	const SColorStop s_aFireStops[] = {
		{0.0f, ColorRGBA(1.0f, 0.96f, 0.65f, 0.9f)},
		{0.2f, ColorRGBA(1.0f, 0.78f, 0.2f, 0.9f)},
		{0.5f, ColorRGBA(1.0f, 0.42f, 0.06f, 0.8f)},
		{0.75f, ColorRGBA(0.75f, 0.14f, 0.04f, 0.55f)},
		{1.0f, ColorRGBA(0.18f, 0.14f, 0.13f, 0.0f)},
	};

	ColorRGBA MixColor(const ColorRGBA &From, const ColorRGBA &To, float Amount)
	{
		return ColorRGBA(mix(From.r, To.r, Amount), mix(From.g, To.g, Amount), mix(From.b, To.b, Amount), mix(From.a, To.a, Amount));
	}

	ColorRGBA Hue(float Value)
	{
		return color_cast<ColorRGBA>(ColorHSLA(Value - std::floor(Value), 1.0f, 0.6f));
	}

	ColorRGBA FireColor(float Age)
	{
		for(size_t i = 1; i < std::size(s_aFireStops); i++)
		{
			const SColorStop &From = s_aFireStops[i - 1];
			const SColorStop &To = s_aFireStops[i];
			if(Age <= To.m_Age)
				return MixColor(From.m_Color, To.m_Color, (Age - From.m_Age) / (To.m_Age - From.m_Age));
		}
		return s_aFireStops[std::size(s_aFireStops) - 1].m_Color;
	}

	ColorRGBA CustomFireColor(float Age, const ColorRGBA &Custom)
	{
		const ColorRGBA Hot = MixColor(Custom, COLOR_WHITE, 0.65f);
		const ColorRGBA Cold = MixColor(Custom, COLOR_BLACK, 0.7f);
		ColorRGBA Color = Age < 0.4f ? MixColor(Hot, Custom, Age / 0.4f) : MixColor(Custom, Cold, (Age - 0.4f) / 0.6f);
		Color.a = FireColor(Age).a;
		return Color;
	}

	CAccessories::SSlotSettings EffectSettings(const CAccessories::SSettings &Settings, int Item)
	{
		CAccessories::SSlotSettings Slot = Settings.m_aSlots[CAccessories::SLOT_INTERACTIVE];
		if(Slot.m_Item != Item)
			Slot = {Item, 100, 0, 0, 65408};
		return Slot;
	}

	bool EffectColor(const CAccessories::SSettings &Settings, int Item, ColorRGBA *pColor)
	{
		*pColor = COLOR_WHITE;
		const CAccessories::SSlotSettings &Slot = Settings.m_aSlots[CAccessories::SLOT_INTERACTIVE];
		if(Slot.m_Item != Item || !Settings.m_UseCustomColor || !(Settings.m_CustomColorSlots & (1 << CAccessories::SLOT_INTERACTIVE)))
			return false;
		*pColor = color_cast<ColorRGBA>(ColorHSLA(Slot.m_Color).UnclampLighting(ColorHSLA::DARKEST_LGT));
		return true;
	}
}

const CAccessoryEffects::SKindInfo CAccessoryEffects::ms_aKinds[NUM_KINDS] = {
	{2.2f, -160.0f, 0.15f, 0.35f, 1.3f},
	{2.6f, -30.0f, 0.1f, 0.4f, 1.6f},
	{1.6f, 110.0f, 0.2f, 1.0f, 0.6f},
	{1.7f, -70.0f, 0.0f, 0.7f, 1.1f},
	{1.0f, 500.0f, 0.2f, 1.0f, 0.3f},
	{0.2f, 620.0f, 0.12f, 0.8f, 1.1f},
	{0.1f, 120.0f, 0.0f, 1.0f, 1.0f},
	{3.0f, 60.0f, 0.2f, 1.0f, 0.1f},
	{2.4f, 200.0f, 0.4f, 1.0f, 0.2f},
	{2.0f, 400.0f, 0.5f, 1.0f, 0.2f},
	{1.3f, -50.0f, 0.1f, 0.7f, 1.5f},
	{1.8f, 260.0f, 0.0f, 1.0f, 1.0f},
	{0.3f, 720.0f, 0.1f, 1.0f, 0.8f},
	{0.0f, 0.0f, 0.0f, 0.8f, 1.1f},
	{0.0f, 800.0f, 0.0f, 1.0f, 1.0f},
	{0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
	{2.8f, -40.0f, 0.2f, 0.5f, 1.8f},
	{0.4f, 900.0f, 0.35f, 1.0f, 0.8f},
	{0.8f, -45.0f, 0.1f, 0.6f, 1.7f},
	{3.2f, -90.0f, 0.1f, 0.35f, 1.8f},
};

const char *CAccessoryEffects::Command()
{
	return EFFECT_KEY_COMMAND;
}

void CAccessoryEffects::ConKeyAccessoryEffect(IConsole::IResult *pResult, void *pUserData)
{
	CAccessoryEffects *pSelf = static_cast<CAccessoryEffects *>(pUserData);
	const bool Held = pResult->GetInteger(0) != 0;
	if(Held && !pSelf->m_Held)
		pSelf->Press();
	pSelf->m_Held = Held;
}

void CAccessoryEffects::OnConsoleInit()
{
	Console()->Register(EFFECT_KEY_COMMAND, "", CFGFLAG_CLIENT, ConKeyAccessoryEffect, this, "M-Client: use your interactive accessory, hold to keep using it");
}

void CAccessoryEffects::OnReset()
{
	for(CBurst &Burst : m_aBursts)
		Burst = CBurst{};
	m_World.m_vParticles.clear();
	m_World.m_vBeams.clear();
	m_Held = false;
	m_HeldId = -1;
}

void CAccessoryEffects::OnRelease()
{
	m_Held = false;
}

bool CAccessoryEffects::CanUse() const
{
	return Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active;
}

int CAccessoryEffects::EquippedItem() const
{
	const int Item = *CAccessories::Config(g_Config.m_ClDummy != 0).m_aSlots[CAccessories::SLOT_INTERACTIVE].m_pItem;
	return Item > 0 && Item < CAccessories::NUM_INTERACTIVE ? Item : 0;
}

void CAccessoryEffects::Press()
{
	const int Item = EquippedItem();
	if(Item == 0 || !CanUse())
		return;

	Hold(GameClient()->m_aLocalIds[g_Config.m_ClDummy], Item, MIN_PRESS_TIME);
	GameClient()->m_MClientDetect.QueueEffect(Item);
}

void CAccessoryEffects::Start(int ClientId, int Item)
{
	if(Item > 0 && Item < CAccessories::NUM_INTERACTIVE)
		Hold(ClientId, Item, BURST_TIME);
}

void CAccessoryEffects::Hold(int ClientId, int Item, float Time)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Item <= 0 || Item >= CAccessories::NUM_INTERACTIVE)
		return;

	CBurst &Burst = m_aBursts[ClientId];
	const float EndTime = LocalTime() + Time;
	if(Burst.m_Item != Item || !IsActive(ClientId))
		Burst.m_StartTime = LocalTime();
	if(Burst.m_Item != Item)
	{
		Burst.m_Item = Item;
		Burst.m_NextEmitTime = LocalTime();
		Burst.m_EndTime = EndTime;
		return;
	}
	Burst.m_EndTime = std::max(Burst.m_EndTime, EndTime);
}

void CAccessoryEffects::Stop(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aBursts[ClientId].m_EndTime = std::min(m_aBursts[ClientId].m_EndTime, LocalTime());
}

bool CAccessoryEffects::IsActive(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aBursts[ClientId].m_Item != 0 && LocalTime() < m_aBursts[ClientId].m_EndTime;
}

void CAccessoryEffects::OnUpdate()
{
	if(LocalTime() < m_LastUpdateTime)
		OnReset();
	m_LastUpdateTime = LocalTime();

	if(GameClient()->m_Chat.IsActive() || GameClient()->m_GameConsole.IsActive() || GameClient()->m_Menus.IsActive())
		m_Held = false;

	const int Item = m_Held && CanUse() ? EquippedItem() : 0;
	GameClient()->m_MClientDetect.SetEffectHold(Item);

	const int HeldId = Item != 0 ? GameClient()->m_aLocalIds[g_Config.m_ClDummy] : -1;
	if(m_HeldId != HeldId)
		Stop(m_HeldId);
	m_HeldId = HeldId;
	Hold(HeldId, Item, HELD_TIME);
}

bool CAccessoryEffects::Spawn(std::vector<CParticle> &vParticles, const CParticle &Particle) const
{
	if(vParticles.size() >= MAX_PARTICLES)
		return false;
	vParticles.push_back(Particle);
	return true;
}

CAccessoryEffects::CParticle CAccessoryEffects::NewParticle(EKind Kind, const SSource &Source, vec2 Pos, vec2 Vel, float LifeSpan, float Size, ColorRGBA Color) const
{
	CParticle Particle;
	Particle.m_Kind = Kind;
	Particle.m_Pos = Pos;
	Particle.m_Vel = Vel * Source.m_SpeedScale;
	Particle.m_Life = 0.0f;
	Particle.m_LifeSpan = LifeSpan;
	Particle.m_Size = Size * Source.m_SizeScale;
	Particle.m_Rot = random_angle();
	Particle.m_RotSpeed = random_float(-4.0f, 4.0f);
	Particle.m_Alpha = Source.m_Alpha;
	Particle.m_SpeedScale = Source.m_SpeedScale;
	Particle.m_SizeScale = Source.m_SizeScale;
	Particle.m_Color = Color;
	Particle.m_UseCustomColor = Source.m_UseCustomColor;
	Particle.m_Landed = false;
	return Particle;
}

void CAccessoryEffects::SpawnStream(CSystem &System, CParticle Particle, const SSource &Source, float Late) const
{
	const vec2 Ahead = Particle.m_Pos + Particle.m_Vel * Late;
	if(!Source.m_Collides || !Collision()->IntersectLine(Particle.m_Pos, Ahead, nullptr, nullptr))
		Particle.m_Pos = Ahead;
	Particle.m_Life = Late;
	Spawn(System.m_vParticles, Particle);
}

void CAccessoryEffects::EmitOne(CSystem &System, int Item, const SSource &Source, float Late) const
{
	const float Aim = angle(Source.m_Direction);
	const vec2 Mouth = Source.m_Anchors.m_Mouth;
	const ColorRGBA &Custom = Source.m_CustomColor;

	switch(Item)
	{
	case CAccessories::INTERACTIVE_FIRE:
	{
		CParticle Flame = NewParticle(KIND_FLAME, Source, Mouth, direction(Aim + random_float(-0.15f, 0.15f)) * random_float(430.0f, 620.0f), random_float(0.35f, 0.6f), random_float(24.0f, 40.0f), Custom);
		Flame.m_Vel += Source.m_Vel;
		SpawnStream(System, Flame, Source, Late);
		break;
	}
	case CAccessories::INTERACTIVE_ICE:
	{
		const ColorRGBA MistColor = Source.m_UseCustomColor ? Custom : MixColor(ColorRGBA(0.85f, 0.95f, 1.0f, 1.0f), ColorRGBA(0.5f, 0.75f, 1.0f, 1.0f), random_float());
		CParticle Mist = NewParticle(KIND_MIST, Source, Mouth, direction(Aim + random_float(-0.28f, 0.28f)) * random_float(260.0f, 440.0f), random_float(0.6f, 1.0f), random_float(20.0f, 32.0f), MistColor);
		Mist.m_Vel += Source.m_Vel;
		SpawnStream(System, Mist, Source, Late);
		if(random_float() < 0.35f)
		{
			const ColorRGBA FlakeColor = Source.m_UseCustomColor ? MixColor(Custom, COLOR_WHITE, 0.5f) : COLOR_WHITE;
			CParticle Flake = NewParticle(KIND_FLAKE, Source, Mouth, direction(Aim + random_float(-0.35f, 0.35f)) * random_float(280.0f, 520.0f), random_float(0.7f, 1.3f), random_float(10.0f, 18.0f), FlakeColor);
			Flake.m_Vel += Source.m_Vel;
			SpawnStream(System, Flake, Source, Late);
		}
		break;
	}
	case CAccessories::INTERACTIVE_BUBBLES:
	{
		const ColorRGBA BubbleColor = Source.m_UseCustomColor ? Custom : MixColor(ColorRGBA(0.55f, 0.85f, 1.0f, 1.0f), ColorRGBA(0.85f, 0.7f, 1.0f, 1.0f), random_float());
		CParticle Bubble = NewParticle(KIND_BUBBLE, Source, Mouth, direction(Aim + random_float(-0.35f, 0.35f)) * random_float(170.0f, 330.0f), random_float(1.1f, 2.2f), random_float(12.0f, 26.0f), BubbleColor);
		Bubble.m_Vel += Source.m_Vel * 0.5f;
		SpawnStream(System, Bubble, Source, Late);
		break;
	}
	case CAccessories::INTERACTIVE_RAINBOW:
	{
		const vec2 Side = vec2(-Source.m_Direction.y, Source.m_Direction.x) * (Source.m_Direction.x < 0.0f ? -1.0f : 1.0f);
		const vec2 LaneVel = direction(Aim + random_float(-0.04f, 0.04f)) * random_float(400.0f, 480.0f) - vec2(0.0f, 130.0f);
		for(int Lane = 0; Lane < RAINBOW_LANES; Lane++)
		{
			const float Offset = (float)Lane - (RAINBOW_LANES - 1) / 2.0f;
			const float Share = (float)Lane / (float)RAINBOW_LANES;
			const ColorRGBA LaneColor = Source.m_UseCustomColor ? MixColor(MixColor(Custom, COLOR_WHITE, 0.4f), MixColor(Custom, COLOR_BLACK, 0.3f), Share) : Hue(Share * 0.95f);
			CParticle Puke = NewParticle(KIND_PUKE, Source, Mouth + Side * (Offset * 1.6f * Source.m_SizeScale), LaneVel + Side * (Offset * 14.0f), random_float(0.8f, 1.1f), random_float(10.0f, 14.0f), LaneColor);
			Puke.m_Vel += Source.m_Vel;
			SpawnStream(System, Puke, Source, Late);
		}
		break;
	}
	case CAccessories::INTERACTIVE_FIREWORK:
	{
		const ColorRGBA RocketColor = Source.m_UseCustomColor ? Custom : Hue(random_float());
		CParticle Rocket = NewParticle(KIND_ROCKET, Source, Mouth, direction(Aim + random_float(-0.05f, 0.05f)) * random_float(620.0f, 720.0f), random_float(0.5f, 0.65f), 12.0f, RocketColor);
		Rocket.m_Vel += Source.m_Vel * 0.5f;
		SpawnStream(System, Rocket, Source, Late);
		break;
	}
	case CAccessories::INTERACTIVE_LASER:
	{
		if(System.m_vBeams.empty())
			break;
		const CBeam &Beam = System.m_vBeams[System.m_vBeams.size() - 1 - rand() % 2];
		const vec2 EmberVel = direction(Aim + pi + random_float(-1.2f, 1.2f)) * random_float(80.0f, 320.0f);
		Spawn(System.m_vParticles, NewParticle(KIND_EMBER, Source, Beam.m_To - Source.m_Direction * (2.0f * Source.m_SizeScale), EmberVel, random_float(0.2f, 0.45f), random_float(5.0f, 10.0f), MixColor(Beam.m_Color, COLOR_WHITE, 0.4f)));
		break;
	}
	case CAccessories::INTERACTIVE_KISSES:
	{
		CParticle Kiss = NewParticle(KIND_KISS, Source, Mouth, direction(Aim + random_float(-0.22f, 0.22f)) * random_float(210.0f, 320.0f), random_float(1.1f, 1.7f), random_float(16.0f, 24.0f), Source.m_UseCustomColor ? Custom : COLOR_WHITE);
		Kiss.m_Vel += Source.m_Vel * 0.5f;
		Kiss.m_Rot = random_float(-0.3f, 0.3f);
		Kiss.m_RotSpeed = random_float(-0.4f, 0.4f);
		SpawnStream(System, Kiss, Source, Late);
		break;
	}
	case CAccessories::INTERACTIVE_MONEY:
	{
		const vec2 Head = (Source.m_Anchors.m_aEyes[0] + Source.m_Anchors.m_aEyes[1]) / 2.0f;
		vec2 Start = Head + vec2(random_float(-MONEY_WIDTH, MONEY_WIDTH), -MONEY_HEIGHT - random_float(0.0f, 30.0f)) * Source.m_SizeScale;
		if(Source.m_Collides)
			Collision()->IntersectLine(Head, Start, nullptr, &Start);
		const ColorRGBA BillColor = Source.m_UseCustomColor ? Custom : MixColor(ColorRGBA(0.36f, 0.66f, 0.36f, 1.0f), ColorRGBA(0.5f, 0.76f, 0.42f, 1.0f), random_float());
		CParticle Bill = NewParticle(KIND_BILL, Source, Start, vec2(random_float(-70.0f, 70.0f), random_float(10.0f, 70.0f)), random_float(2.0f, 3.0f), random_float(20.0f, 26.0f), BillColor);
		Bill.m_RotSpeed = random_float(-2.0f, 2.0f);
		Spawn(System.m_vParticles, Bill);
		break;
	}
	case CAccessories::INTERACTIVE_RAIN_CLOUD:
	{
		const vec2 Head = (Source.m_Anchors.m_aEyes[0] + Source.m_Anchors.m_aEyes[1]) / 2.0f;
		const vec2 Cloud = Reach(Source, Head, Head - vec2(0.0f, CLOUD_HEIGHT * Source.m_SizeScale));
		const ColorRGBA GrayColor = MixColor(ColorRGBA(0.38f, 0.4f, 0.45f, 1.0f), ColorRGBA(0.62f, 0.64f, 0.68f, 1.0f), random_float());
		const vec2 PuffPos = Reach(Source, Cloud, Cloud + vec2(random_float(-34.0f, 34.0f), random_float(-9.0f, 7.0f)) * Source.m_SizeScale);
		CParticle Puff = NewParticle(KIND_CLOUD, Source, PuffPos, vec2(0.0f, 0.0f), random_float(0.3f, 0.45f), random_float(26.0f, 38.0f), Source.m_UseCustomColor ? MixColor(GrayColor, Custom, 0.5f) : GrayColor);
		Puff.m_Vel = Source.m_Vel;
		Spawn(System.m_vParticles, Puff);

		for(int i = 0; i < 3; i++)
		{
			const vec2 DropPos = Reach(Source, Cloud, Cloud + vec2(random_float(-32.0f, 32.0f), 6.0f) * Source.m_SizeScale);
			CParticle Drop = NewParticle(KIND_RAIN, Source, DropPos, vec2(0.0f, random_float(140.0f, 220.0f)), 1.0f, random_float(10.0f, 15.0f), Source.m_UseCustomColor ? Custom : ColorRGBA(0.4f, 0.65f, 1.0f, 1.0f));
			Drop.m_Vel.x += Source.m_Vel.x * 0.5f;
			Spawn(System.m_vParticles, Drop);
		}

		if(Source.m_ActiveTime >= LIGHTNING_DELAY && random_float() < LIGHTNING_CHANCE)
		{
			const vec2 BoltPos = Reach(Source, Cloud, Cloud + vec2(random_float(-24.0f, 24.0f), 4.0f) * Source.m_SizeScale);
			const vec2 BoltEnd = Reach(Source, BoltPos, BoltPos + vec2(0.0f, BOLT_LENGTH * Source.m_SpeedScale));
			CParticle Bolt = NewParticle(KIND_BOLT, Source, BoltPos, vec2(0.0f, 0.0f), random_float(0.18f, 0.3f), 1.0f, Source.m_UseCustomColor ? MixColor(Custom, COLOR_WHITE, 0.3f) : ColorRGBA(1.0f, 0.95f, 0.45f, 1.0f));
			Bolt.m_Size = BoltEnd.y - BoltPos.y;
			Spawn(System.m_vParticles, Bolt);
		}
		break;
	}
	case CAccessories::INTERACTIVE_GROUND_SLAM:
	{
		const vec2 Head = (Source.m_Anchors.m_aEyes[0] + Source.m_Anchors.m_aEyes[1]) / 2.0f;
		vec2 Ground = Source.m_Anchors.m_Feet;
		bool OnGround = true;
		if(Source.m_Collides)
		{
			const vec2 Below = Head + vec2(0.0f, SLAM_DEPTH * Source.m_SizeScale);
			vec2 Hit;
			OnGround = Collision()->IntersectLine(Head, Below, nullptr, &Hit) != 0;
			Ground = OnGround ? Hit - vec2(0.0f, 2.0f) : Reach(Source, Head, Ground);
		}
		for(int i = 0; i < (OnGround ? 34 : 14); i++)
		{
			const float Side = i % 2 == 0 ? -1.0f : 1.0f;
			const ColorRGBA DustColor = Source.m_UseCustomColor ? MixColor(Custom, COLOR_WHITE, 0.2f) : MixColor(ColorRGBA(0.72f, 0.62f, 0.48f, 1.0f), ColorRGBA(0.85f, 0.79f, 0.68f, 1.0f), random_float());
			Spawn(System.m_vParticles, NewParticle(KIND_DUST, Source, Ground, vec2(Side * random_float(60.0f, 420.0f), random_float(-90.0f, -5.0f)), random_float(0.5f, 0.9f), random_float(18.0f, 30.0f), DustColor));
		}
		for(int i = 0; OnGround && i < 18; i++)
		{
			const ColorRGBA StoneColor = Source.m_UseCustomColor ? MixColor(Custom, COLOR_BLACK, 0.25f) : MixColor(ColorRGBA(0.38f, 0.32f, 0.27f, 1.0f), ColorRGBA(0.58f, 0.52f, 0.45f, 1.0f), random_float());
			CParticle Stone = NewParticle(KIND_STONE, Source, Ground, vec2(random_float(-260.0f, 260.0f), random_float(-440.0f, -160.0f)), random_float(0.8f, 1.3f), random_float(5.0f, 10.0f), StoneColor);
			Stone.m_RotSpeed = random_float(-9.0f, 9.0f);
			Spawn(System.m_vParticles, Stone);
		}
		break;
	}
	case CAccessories::INTERACTIVE_SLEEPY:
	{
		const vec2 Head = (Source.m_Anchors.m_aEyes[0] + Source.m_Anchors.m_aEyes[1]) / 2.0f;
		const float Side = Source.m_Direction.x < 0.0f ? -1.0f : 1.0f;
		const vec2 Start = Reach(Source, Head, Head + vec2(Side * 12.0f, -18.0f) * Source.m_SizeScale);
		CParticle Sleep = NewParticle(KIND_SLEEP, Source, Start, vec2(Side * random_float(20.0f, 45.0f), random_float(-65.0f, -45.0f)), random_float(1.5f, 2.0f), random_float(13.0f, 17.0f), Source.m_UseCustomColor ? Custom : ColorRGBA(0.8f, 0.9f, 1.0f, 1.0f));
		Sleep.m_Rot = random_float(-0.25f, 0.25f);
		Sleep.m_RotSpeed = random_float(-0.2f, 0.2f);
		Spawn(System.m_vParticles, Sleep);
		break;
	}
	case CAccessories::INTERACTIVE_STEAM:
	{
		const vec2 Head = (Source.m_Anchors.m_aEyes[0] + Source.m_Anchors.m_aEyes[1]) / 2.0f;
		for(int i = 0; i < 2; i++)
		{
			const float Side = i == 0 ? -1.0f : 1.0f;
			const ColorRGBA SteamColor = Source.m_UseCustomColor ? MixColor(Custom, COLOR_WHITE, random_float(0.0f, 0.4f)) : MixColor(ColorRGBA(0.82f, 0.84f, 0.87f, 1.0f), COLOR_WHITE, random_float());
			CParticle Steam = NewParticle(KIND_STEAM, Source, Reach(Source, Head, Source.m_Anchors.m_aSides[i]), vec2(Side * random_float(230.0f, 360.0f), random_float(-190.0f, -90.0f)), random_float(0.4f, 0.65f), random_float(16.0f, 24.0f), SteamColor);
			Steam.m_Vel += Source.m_Vel;
			SpawnStream(System, Steam, Source, Late);
		}
		break;
	}
	case CAccessories::INTERACTIVE_CRYING:
	{
		const ColorRGBA TearColor = Source.m_UseCustomColor ? Custom : MixColor(ColorRGBA(0.35f, 0.7f, 1.0f, 1.0f), ColorRGBA(0.6f, 0.87f, 1.0f, 1.0f), random_float());
		for(int Eye = 0; Eye < 2; Eye++)
		{
			const float Side = Eye == 0 ? -1.0f : 1.0f;
			CParticle Tear = NewParticle(KIND_TEAR, Source, Source.m_Anchors.m_aEyes[Eye], vec2(Side * random_float(70.0f, 150.0f), random_float(-230.0f, -150.0f)), random_float(0.9f, 1.2f), random_float(6.0f, 10.0f), TearColor);
			Tear.m_Vel += Source.m_Vel;
			SpawnStream(System, Tear, Source, Late);
		}
		break;
	}
	}
}

vec2 CAccessoryEffects::Reach(const SSource &Source, vec2 From, vec2 To) const
{
	if(Source.m_Collides)
		Collision()->IntersectLine(From, To, nullptr, &To);
	return To;
}

void CAccessoryEffects::EmitBeams(CSystem &System, const SSource &Source) const
{
	ColorRGBA Color = Source.m_UseCustomColor ? Source.m_CustomColor : ColorRGBA(1.0f, 0.12f, 0.1f, 1.0f);
	Color.a = Source.m_Alpha;
	for(const vec2 &Eye : Source.m_Anchors.m_aEyes)
	{
		vec2 End = Eye + Source.m_Direction * Source.m_BeamLength;
		if(Source.m_Collides)
			Collision()->IntersectLine(Eye, End, nullptr, &End);
		System.m_vBeams.push_back({Eye, End, Source.m_SizeScale, Color});
	}
}

void CAccessoryEffects::Emit(int ClientId, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, vec2 Vel, float Alpha, const CAccessories::SSettings &Settings)
{
	if(!IsActive(ClientId) || GameClient()->IsWorldPaused() || GameClient()->IsDemoPlaybackPaused())
		return;

	CBurst &Burst = m_aBursts[ClientId];
	const CAccessories::SSlotSettings Slot = EffectSettings(Settings, Burst.m_Item);
	const float Scale = pInfo->m_Size / 64.0f * Slot.m_Size / 100.0f;

	SSource Source;
	Source.m_Anchors = CAccessories::EffectAnchors(GameClient()->RenderTools(), pAnim, pInfo, Pos, Direction, Slot);
	Source.m_Direction = Direction;
	Source.m_Vel = Vel;
	Source.m_SpeedScale = Scale;
	Source.m_SizeScale = Scale;
	Source.m_Alpha = Alpha;
	Source.m_BeamLength = BEAM_LENGTH * Scale;
	Source.m_ActiveTime = LocalTime() - Burst.m_StartTime;
	Source.m_UseCustomColor = EffectColor(Settings, Burst.m_Item, &Source.m_CustomColor);
	Source.m_Collides = true;

	if(Burst.m_Item == CAccessories::INTERACTIVE_LASER)
		EmitBeams(m_World, Source);

	const float Now = LocalTime();
	Burst.m_NextEmitTime = std::max(Burst.m_NextEmitTime, Now - MAX_EMIT_LAG);
	for(; Burst.m_NextEmitTime <= Now; Burst.m_NextEmitTime += s_aEmitIntervals[Burst.m_Item])
		EmitOne(m_World, Burst.m_Item, Source, Now - Burst.m_NextEmitTime);
}

void CAccessoryEffects::Pop(std::vector<CParticle> &vParticles, const CParticle &Bubble) const
{
	for(int i = 0; i < 5; i++)
	{
		CParticle Droplet = Bubble;
		Droplet.m_Kind = KIND_DROPLET;
		Droplet.m_Vel = random_direction() * (random_float(40.0f, 120.0f) * Bubble.m_SpeedScale);
		Droplet.m_Life = 0.0f;
		Droplet.m_LifeSpan = random_float(0.15f, 0.3f);
		Droplet.m_Size = random_float(3.0f, 5.0f) * Bubble.m_SizeScale;
		Spawn(vParticles, Droplet);
	}
}

void CAccessoryEffects::Splash(std::vector<CParticle> &vParticles, const CParticle &Tear) const
{
	for(int i = 0; i < 2; i++)
	{
		CParticle Droplet = Tear;
		Droplet.m_Kind = KIND_DROPLET;
		Droplet.m_Vel = vec2(random_float(-70.0f, 70.0f), random_float(-110.0f, -40.0f)) * Tear.m_SpeedScale;
		Droplet.m_Life = 0.0f;
		Droplet.m_LifeSpan = random_float(0.15f, 0.25f);
		Droplet.m_Size = random_float(2.5f, 4.0f) * Tear.m_SizeScale;
		Spawn(vParticles, Droplet);
	}
}

void CAccessoryEffects::Explode(std::vector<CParticle> &vParticles, const CParticle &Rocket) const
{
	const ColorRGBA Second = Rocket.m_UseCustomColor ? MixColor(Rocket.m_Color, COLOR_WHITE, 0.5f) : Hue(random_float());
	for(int i = 0; i < 64; i++)
	{
		CParticle Spark = Rocket;
		Spark.m_Kind = KIND_SPARK;
		Spark.m_Vel = random_direction() * (random_float(60.0f, 440.0f) * Rocket.m_SpeedScale);
		Spark.m_Life = 0.0f;
		Spark.m_LifeSpan = random_float(0.6f, 1.2f);
		Spark.m_Size = random_float(8.0f, 15.0f) * Rocket.m_SizeScale;
		Spark.m_Color = i % 3 == 0 ? Second : Rocket.m_Color;
		Spawn(vParticles, Spark);
	}
}

void CAccessoryEffects::Update(CSystem &System, float TimePassed, bool Collides) const
{
	std::vector<CParticle> vSpawned;
	for(CParticle &Particle : System.m_vParticles)
	{
		const SKindInfo &Kind = ms_aKinds[Particle.m_Kind];
		Particle.m_Life += TimePassed;
		Particle.m_Vel *= std::exp(-Kind.m_Drag * TimePassed);
		Particle.m_Vel.y += Kind.m_Gravity * Particle.m_SpeedScale * TimePassed;
		if(Particle.m_Kind == KIND_BUBBLE)
			Particle.m_Vel.x += std::sin(Particle.m_Life * 6.0f + Particle.m_Rot) * 140.0f * Particle.m_SpeedScale * TimePassed;
		else if(Particle.m_Kind == KIND_KISS)
			Particle.m_Vel.x += std::sin(Particle.m_Life * 5.0f + Particle.m_LifeSpan * 20.0f) * 110.0f * Particle.m_SpeedScale * TimePassed;
		else if(Particle.m_Kind == KIND_SLEEP)
			Particle.m_Vel.x += std::sin(Particle.m_Life * 3.5f + Particle.m_LifeSpan * 20.0f) * 70.0f * Particle.m_SpeedScale * TimePassed;
		else if(Particle.m_Kind == KIND_BILL && !Particle.m_Landed)
			Particle.m_Vel.x += std::sin(Particle.m_Life * 4.0f + Particle.m_LifeSpan * 20.0f) * 260.0f * Particle.m_SpeedScale * TimePassed;
		Particle.m_Rot += Particle.m_RotSpeed * TimePassed;

		int Bounces = 0;
		vec2 Step = Particle.m_Vel * TimePassed;
		if(Collides)
		{
			Collision()->MovePoint(&Particle.m_Pos, &Step, Kind.m_Bounce, &Bounces);
			Particle.m_Vel = Step / TimePassed;
		}
		else
		{
			Particle.m_Pos += Step;
		}

		if(Particle.m_Kind == KIND_ROCKET && random_float() < TimePassed * 90.0f)
		{
			CParticle Trail = Particle;
			Trail.m_Kind = KIND_TRAIL;
			Trail.m_Vel = random_direction() * (random_float(10.0f, 60.0f) * Particle.m_SpeedScale);
			Trail.m_Life = 0.0f;
			Trail.m_LifeSpan = random_float(0.25f, 0.5f);
			Trail.m_Size = random_float(6.0f, 10.0f) * Particle.m_SizeScale;
			Trail.m_Color = MixColor(ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f), Particle.m_Color, 0.3f);
			Spawn(vSpawned, Trail);
		}
		if(Particle.m_Kind == KIND_PUKE && Bounces > 0)
			Particle.m_Vel *= std::exp(-7.0f * TimePassed);

		if(Particle.m_Kind == KIND_BILL && Bounces > 0 && Collision()->CheckPoint(Particle.m_Pos + vec2(0.0f, 4.0f)))
		{
			Particle.m_Landed = true;
			Particle.m_Vel = vec2(0.0f, 0.0f);
			Particle.m_Rot = 0.0f;
			Particle.m_RotSpeed = 0.0f;
		}

		const bool Hit = Bounces > 0 && (Particle.m_Kind == KIND_BUBBLE || Particle.m_Kind == KIND_ROCKET || Particle.m_Kind == KIND_TEAR || Particle.m_Kind == KIND_RAIN);
		if(Hit)
			Particle.m_Life = Particle.m_LifeSpan;
		if(Particle.m_Life >= Particle.m_LifeSpan)
		{
			if(Particle.m_Kind == KIND_BUBBLE)
				Pop(vSpawned, Particle);
			else if((Particle.m_Kind == KIND_TEAR || Particle.m_Kind == KIND_RAIN) && Hit)
				Splash(vSpawned, Particle);
			else if(Particle.m_Kind == KIND_ROCKET)
				Explode(vSpawned, Particle);
		}
	}
	std::erase_if(System.m_vParticles, [](const CParticle &Particle) { return Particle.m_Life >= Particle.m_LifeSpan; });
	for(const CParticle &Particle : vSpawned)
		Spawn(System.m_vParticles, Particle);
}

bool CAccessoryEffects::LayerQuad(const CParticle &Particle, ELayer Layer, vec2 *pPos, float *pSize, ColorRGBA *pColor)
{
	const float Age = std::clamp(Particle.m_Life / Particle.m_LifeSpan, 0.0f, 1.0f);
	const SKindInfo &Kind = ms_aKinds[Particle.m_Kind];
	*pPos = Particle.m_Pos;
	*pSize = Particle.m_Size * mix(Kind.m_StartSize, Kind.m_EndSize, Age);
	*pColor = Particle.m_Color;

	float Opacity = 1.0f;
	switch(Particle.m_Kind)
	{
	case KIND_FLAME:
		*pSize = Particle.m_Size * mix(Kind.m_StartSize, Kind.m_EndSize, std::sin(std::min(Age * 1.25f, 1.0f) * pi / 2.0f));
		*pColor = Particle.m_UseCustomColor ? CustomFireColor(Age, Particle.m_Color) : FireColor(Age);
		Opacity = pColor->a;
		if(Layer == LAYER_SMOKE)
			break;
		if(Layer != LAYER_GLOW || Age >= 0.6f)
			return false;
		*pSize *= 0.5f;
		Opacity *= 0.35f * (1.0f - Age / 0.6f);
		break;
	case KIND_MIST:
		if(Layer != LAYER_SMOKE)
			return false;
		Opacity = 0.55f * (1.0f - Age);
		break;
	case KIND_FLAKE:
		if(Layer != LAYER_FLAKE)
			return false;
		Opacity = 1.0f - Age * Age;
		break;
	case KIND_BUBBLE:
		if(Layer == LAYER_BALL)
		{
			Opacity = 0.5f;
			break;
		}
		if(Layer != LAYER_SHINE)
			return false;
		*pPos += vec2(-0.16f, -0.18f) * *pSize;
		*pSize *= 0.3f;
		*pColor = COLOR_WHITE;
		Opacity = 0.85f;
		break;
	case KIND_DROPLET:
		if(Layer != LAYER_BALL)
			return false;
		Opacity = 0.7f * (1.0f - Age);
		break;
	case KIND_PUKE:
		if(Layer != LAYER_BALL)
			return false;
		Opacity = Age < 0.7f ? 1.0f : (1.0f - Age) / 0.3f;
		break;
	case KIND_CLOUD:
		if(Layer != LAYER_SMOKE)
			return false;
		Opacity = 0.8f * std::min(Age * 4.0f, 1.0f) * std::min((1.0f - Age) * 2.0f, 1.0f);
		break;
	case KIND_DUST:
	case KIND_STEAM:
		if(Layer != LAYER_SMOKE)
			return false;
		Opacity = 0.65f * (1.0f - Age);
		break;
	case KIND_KISS:
		if(Layer != (Particle.m_UseCustomColor ? LAYER_CUSTOM_HEART : LAYER_HEART))
			return false;
		Opacity = Age < 0.6f ? 1.0f : (1.0f - Age) / 0.4f;
		break;
	case KIND_TEAR:
		if(Layer == LAYER_BALL)
		{
			Opacity = 0.9f;
			break;
		}
		if(Layer != LAYER_SHINE)
			return false;
		*pPos += vec2(-0.15f, -0.17f) * *pSize;
		*pSize *= 0.35f;
		*pColor = COLOR_WHITE;
		Opacity = 0.8f;
		break;
	case KIND_ROCKET:
		if(Layer != LAYER_GLOW)
			return false;
		break;
	case KIND_TRAIL:
	case KIND_EMBER:
		if(Layer != LAYER_GLOW)
			return false;
		Opacity = 1.0f - Age;
		break;
	case KIND_SPARK:
		if(Layer == LAYER_SPARKLE)
		{
			*pColor = MixColor(*pColor, COLOR_WHITE, 0.5f);
			*pSize *= 1.6f;
			Opacity = 0.6f * (1.0f - Age);
			break;
		}
		if(Layer != LAYER_GLOW)
			return false;
		Opacity = 1.0f - Age * Age;
		break;
	default:
		return false;
	}
	pColor->a = Opacity * Particle.m_Alpha;
	return true;
}

void CAccessoryEffects::RenderBeams(const CSystem &System)
{
	if(System.m_vBeams.empty())
		return;

	struct SBeamPass
	{
		float m_Width;
		float m_White;
		float m_Alpha;
	};
	const SBeamPass aPasses[] = {{11.0f, 0.0f, 0.25f}, {6.0f, 0.0f, 0.55f}, {2.2f, 0.8f, 0.95f}};
	const float Flicker = 1.0f + 0.12f * std::sin(LocalTime() * 45.0f);

	Graphics()->BlendAdditive();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const CBeam &Beam : System.m_vBeams)
	{
		const vec2 Delta = Beam.m_To - Beam.m_From;
		if(length(Delta) < 0.1f)
			continue;
		const vec2 Dir = normalize(Delta);
		for(const SBeamPass &Pass : aPasses)
		{
			const vec2 Side = vec2(-Dir.y, Dir.x) * (Pass.m_Width * Beam.m_Width * Flicker / 2.0f);
			Graphics()->SetColor(MixColor(Beam.m_Color, COLOR_WHITE, Pass.m_White).WithAlpha(Pass.m_Alpha * Beam.m_Color.a));
			const IGraphics::CFreeformItem Item(Beam.m_From - Side, Beam.m_From + Side, Beam.m_To - Side, Beam.m_To + Side);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();

	Graphics()->TextureSet(GameClient()->m_ParticlesSkin.m_aSpriteParticles[SPRITE_PART_BALL - SPRITE_PART_SLICE]);
	Graphics()->QuadsBegin();
	for(const CBeam &Beam : System.m_vBeams)
	{
		const float Size = 14.0f * Beam.m_Width * Flicker;
		Graphics()->SetColor(Beam.m_Color.WithAlpha(0.5f * Beam.m_Color.a));
		IGraphics::CQuadItem aItems[] = {
			IGraphics::CQuadItem(Beam.m_From.x, Beam.m_From.y, Size * 0.8f, Size * 0.8f),
			IGraphics::CQuadItem(Beam.m_To.x, Beam.m_To.y, Size * 1.4f, Size * 1.4f)};
		Graphics()->QuadsDraw(aItems, std::size(aItems));
	}
	Graphics()->QuadsEnd();
	Graphics()->BlendNormal();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAccessoryEffects::RenderBills(const CSystem &System)
{
	struct SBillPart
	{
		vec2 m_Size;
		float m_White;
		float m_Black;
	};
	const SBillPart aParts[] = {{vec2(1.0f, 0.52f), 0.0f, 0.5f}, {vec2(0.88f, 0.38f), 0.35f, 0.0f}, {vec2(0.26f, 0.26f), 0.0f, 0.3f}};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const CParticle &Bill : System.m_vParticles)
	{
		if(Bill.m_Kind != KIND_BILL)
			continue;

		const float Age = std::clamp(Bill.m_Life / Bill.m_LifeSpan, 0.0f, 1.0f);
		const float Alpha = Bill.m_Alpha * (Age < 0.75f ? 1.0f : (1.0f - Age) / 0.25f);
		const float Turn = Bill.m_Landed ? 0.4f : std::max(0.15f, absolute(std::cos(Bill.m_Life * 5.0f + Bill.m_LifeSpan * 20.0f)));
		const vec2 Right = direction(Bill.m_Rot);
		const vec2 Down = vec2(-Right.y, Right.x);
		for(const SBillPart &Part : aParts)
		{
			const vec2 HalfRight = Right * (Bill.m_Size * Part.m_Size.x / 2.0f);
			const vec2 HalfDown = Down * (Bill.m_Size * Part.m_Size.y * Turn / 2.0f);
			Graphics()->SetColor(MixColor(MixColor(Bill.m_Color, COLOR_WHITE, Part.m_White), COLOR_BLACK, Part.m_Black).WithAlpha(Alpha));
			const IGraphics::CFreeformItem Item(Bill.m_Pos - HalfRight - HalfDown, Bill.m_Pos + HalfRight - HalfDown, Bill.m_Pos - HalfRight + HalfDown, Bill.m_Pos + HalfRight + HalfDown);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAccessoryEffects::RenderShapes(const CSystem &System)
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(const CParticle &Particle : System.m_vParticles)
	{
		const float Age = std::clamp(Particle.m_Life / Particle.m_LifeSpan, 0.0f, 1.0f);
		const vec2 Pos = Particle.m_Pos;
		const auto &&DrawQuad = [&](vec2 TopLeft, vec2 TopRight, vec2 BottomLeft, vec2 BottomRight) {
			const IGraphics::CFreeformItem Item(TopLeft, TopRight, BottomLeft, BottomRight);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		};
		const auto &&DrawLine = [&](vec2 From, vec2 To, float Width) {
			if(distance(From, To) < 0.01f)
				return;
			const vec2 Dir = normalize(To - From);
			const vec2 Side = vec2(-Dir.y, Dir.x) * (Width / 2.0f);
			DrawQuad(From - Side, From + Side, To - Side, To + Side);
		};

		switch(Particle.m_Kind)
		{
		case KIND_RAIN:
			Graphics()->SetColor(Particle.m_Color.WithAlpha(0.8f * Particle.m_Alpha));
			if(length(Particle.m_Vel) > 0.01f)
				DrawLine(Pos - normalize(Particle.m_Vel) * Particle.m_Size, Pos, 2.2f * Particle.m_SizeScale);
			break;
		case KIND_STONE:
		{
			const vec2 Right = direction(Particle.m_Rot) * (Particle.m_Size / 2.0f);
			const vec2 Down = vec2(-Right.y, Right.x) * 0.8f;
			const float Alpha = Particle.m_Alpha * (Age < 0.7f ? 1.0f : (1.0f - Age) / 0.3f);
			Graphics()->SetColor(MixColor(Particle.m_Color, COLOR_BLACK, 0.5f).WithAlpha(Alpha));
			DrawQuad(Pos - Right - Down, Pos + Right - Down, Pos - Right + Down, Pos + Right + Down);
			Graphics()->SetColor(Particle.m_Color.WithAlpha(Alpha));
			DrawQuad(Pos - (Right + Down) * 0.7f, Pos + (Right - Down) * 0.7f, Pos - (Right - Down) * 0.7f, Pos + (Right + Down) * 0.7f);
			break;
		}
		case KIND_BOLT:
		{
			const auto &&Point = [&](int Part) {
				const float Noise = std::sin((float)Part * 2.4f + Particle.m_LifeSpan * 1000.0f);
				const float Offset = Part == 0 ? 0.0f : Noise * 10.0f * Particle.m_SizeScale;
				return Pos + vec2(Offset, Particle.m_Size * (float)Part / (float)BOLT_PARTS);
			};
			const float aWidths[] = {7.0f, 2.5f};
			for(int Pass = 0; Pass < 2; Pass++)
			{
				Graphics()->SetColor((Pass == 0 ? Particle.m_Color : MixColor(Particle.m_Color, COLOR_WHITE, 0.8f)).WithAlpha((Pass == 0 ? 0.45f : 1.0f) * (1.0f - Age) * Particle.m_Alpha));
				for(int Part = 0; Part < BOLT_PARTS; Part++)
					DrawLine(Point(Part), Point(Part + 1), aWidths[Pass] * Particle.m_SizeScale);
			}
			break;
		}
		case KIND_SLEEP:
		{
			const float Size = Particle.m_Size * mix(ms_aKinds[KIND_SLEEP].m_StartSize, ms_aKinds[KIND_SLEEP].m_EndSize, Age);
			const float Alpha = Particle.m_Alpha * std::min(Age * 8.0f, 1.0f) * (Age < 0.6f ? 1.0f : (1.0f - Age) / 0.4f);
			const vec2 Right = direction(Particle.m_Rot) * Size;
			const vec2 Down = vec2(-Right.y, Right.x);
			const auto &&Corner = [&](float x, float y) { return Pos + Right * x + Down * y; };
			for(int Pass = 0; Pass < 2; Pass++)
			{
				const float Grow = Pass == 0 ? 0.09f : 0.0f;
				Graphics()->SetColor((Pass == 0 ? MixColor(Particle.m_Color, COLOR_BLACK, 0.75f) : Particle.m_Color).WithAlpha(Alpha));
				DrawQuad(Corner(-0.45f - Grow, -0.5f - Grow), Corner(0.45f + Grow, -0.5f - Grow), Corner(-0.45f - Grow, -0.3f + Grow), Corner(0.45f + Grow, -0.3f + Grow));
				DrawQuad(Corner(0.15f - Grow, -0.3f), Corner(0.45f + Grow, -0.3f), Corner(-0.45f - Grow, 0.3f), Corner(-0.15f + Grow, 0.3f));
				DrawQuad(Corner(-0.45f - Grow, 0.3f - Grow), Corner(0.45f + Grow, 0.3f - Grow), Corner(-0.45f - Grow, 0.5f + Grow), Corner(0.45f + Grow, 0.5f + Grow));
			}
			break;
		}
		default:
			break;
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAccessoryEffects::Render(CSystem &System)
{
	RenderBills(System);
	RenderShapes(System);
	for(int Layer = 0; Layer < NUM_LAYERS && !System.m_vParticles.empty(); Layer++)
	{
		const bool Additive = Layer == LAYER_GLOW || Layer == LAYER_SPARKLE;
		IGraphics::CTextureHandle Texture;
		if(Layer == LAYER_FLAKE || Layer == LAYER_SPARKLE)
			Texture = GameClient()->m_ExtrasSkin.m_aSpriteParticles[(Layer == LAYER_FLAKE ? SPRITE_PART_SNOWFLAKE : SPRITE_PART_SPARKLE) - SPRITE_PART_SNOWFLAKE];
		else if(Layer == LAYER_HEART || Layer == LAYER_CUSTOM_HEART)
			Texture = GameClient()->m_Accessories.HeartTexture(Layer == LAYER_CUSTOM_HEART);
		else
			Texture = GameClient()->m_ParticlesSkin.m_aSpriteParticles[(Layer == LAYER_SMOKE ? SPRITE_PART_SMOKE : SPRITE_PART_BALL) - SPRITE_PART_SLICE];

		if(Additive)
			Graphics()->BlendAdditive();
		Graphics()->TextureSet(Texture);
		Graphics()->QuadsBegin();
		for(const CParticle &Particle : System.m_vParticles)
		{
			vec2 Pos;
			float Size;
			ColorRGBA Color;
			if(!LayerQuad(Particle, (ELayer)Layer, &Pos, &Size, &Color))
				continue;
			Graphics()->SetColor(Color);
			Graphics()->QuadsSetRotation(Particle.m_Rot);
			IGraphics::CQuadItem QuadItem(Pos.x, Pos.y, Size, Size);
			Graphics()->QuadsDraw(&QuadItem, 1);
		}
		Graphics()->QuadsEnd();
		Graphics()->QuadsSetRotation(0.0f);
		if(Additive)
			Graphics()->BlendNormal();
	}
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	RenderBeams(System);
	System.m_vBeams.clear();
}

void CAccessoryEffects::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	const float Now = LocalTime();
	const float TimePassed = std::clamp(Now - m_World.m_LastTime, 0.0f, MAX_FRAME_TIME) * GameClient()->GetAnimationPlaybackSpeed();
	m_World.m_LastTime = Now;
	if(TimePassed > 0.0f)
		Update(m_World, TimePassed, true);
	Render(m_World);
}

void CAccessoryEffects::RenderPreview(int PreviewId, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, const CAccessories::SSettings &Settings)
{
	const CAccessories::SSlotSettings &Slot = Settings.m_aSlots[CAccessories::SLOT_INTERACTIVE];
	if(PreviewId < 0 || PreviewId >= NUM_PREVIEWS || Slot.m_Item <= 0 || Slot.m_Item >= CAccessories::NUM_INTERACTIVE)
		return;

	CSystem &System = m_aPreviews[PreviewId];
	const float Now = LocalTime();
	if(Now - System.m_LastTime > PREVIEW_PAUSE)
	{
		System.m_vParticles.clear();
		System.m_NextEmitTime = Now;
	}
	const float TimePassed = std::clamp(Now - System.m_LastTime, 0.0f, MAX_FRAME_TIME);
	System.m_LastTime = Now;

	const float Scale = pInfo->m_Size / 64.0f * Slot.m_Size / 100.0f;
	SSource Source;
	Source.m_Anchors = CAccessories::EffectAnchors(GameClient()->RenderTools(), pAnim, pInfo, Pos, Direction, Slot);
	Source.m_Direction = Direction;
	Source.m_Vel = vec2(0.0f, 0.0f);
	Source.m_SpeedScale = Scale * PREVIEW_REACH;
	Source.m_SizeScale = Scale * PREVIEW_SIZE;
	Source.m_Alpha = 1.0f;
	Source.m_BeamLength = PREVIEW_BEAM_LENGTH * Scale;
	Source.m_ActiveTime = LIGHTNING_DELAY;
	Source.m_UseCustomColor = EffectColor(Settings, Slot.m_Item, &Source.m_CustomColor);
	Source.m_Collides = false;

	if(TimePassed > 0.0f)
		Update(System, TimePassed, false);
	if(Slot.m_Item == CAccessories::INTERACTIVE_LASER)
		EmitBeams(System, Source);
	const float Interval = s_aEmitIntervals[Slot.m_Item] * (Slot.m_Item == CAccessories::INTERACTIVE_FIREWORK || Slot.m_Item == CAccessories::INTERACTIVE_GROUND_SLAM ? 3.0f : 1.0f);
	System.m_NextEmitTime = std::clamp(System.m_NextEmitTime, Now - MAX_EMIT_LAG, Now + Interval);
	for(; System.m_NextEmitTime <= Now; System.m_NextEmitTime += Interval)
		EmitOne(System, Slot.m_Item, Source, Now - System.m_NextEmitTime);
	Render(System);
}
