#ifndef GAME_CLIENT_COMPONENTS_ACCESSORY_EFFECTS_H
#define GAME_CLIENT_COMPONENTS_ACCESSORY_EFFECTS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/components/accessories.h>

#include <vector>

class CAnimState;
class CTeeRenderInfo;

class CAccessoryEffects : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnReset() override;
	void OnRelease() override;
	void OnUpdate() override;
	void OnRender() override;

	static const char *Command();

	void Start(int ClientId, int Item);
	void Hold(int ClientId, int Item, float Time);
	void Stop(int ClientId);
	bool IsActive(int ClientId) const;

	void Emit(int ClientId, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, vec2 Vel, float Alpha, const CAccessories::SSettings &Settings);

	void RenderPreview(int PreviewId, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, const CAccessories::SSettings &Settings);
	enum
	{
		NUM_PREVIEWS = CAccessories::NUM_INTERACTIVE + 1,
	};

private:
	struct CBurst
	{
		int m_Item = 0;
		float m_StartTime = 0.0f;
		float m_EndTime = 0.0f;
		float m_NextEmitTime = 0.0f;
	};
	CBurst m_aBursts[MAX_CLIENTS];

	enum EKind
	{
		KIND_FLAME,
		KIND_MIST,
		KIND_FLAKE,
		KIND_BUBBLE,
		KIND_DROPLET,
		KIND_PUKE,
		KIND_ROCKET,
		KIND_TRAIL,
		KIND_SPARK,
		KIND_EMBER,
		KIND_KISS,
		KIND_BILL,
		KIND_TEAR,
		KIND_CLOUD,
		KIND_RAIN,
		KIND_BOLT,
		KIND_DUST,
		KIND_STONE,
		KIND_SLEEP,
		KIND_STEAM,
		NUM_KINDS,
	};

	struct SKindInfo
	{
		float m_Drag;
		float m_Gravity;
		float m_Bounce;
		float m_StartSize;
		float m_EndSize;
	};
	static const SKindInfo ms_aKinds[NUM_KINDS];

	enum ELayer
	{
		LAYER_SMOKE,
		LAYER_BALL,
		LAYER_FLAKE,
		LAYER_SHINE,
		LAYER_HEART,
		LAYER_CUSTOM_HEART,
		LAYER_GLOW,
		LAYER_SPARKLE,
		NUM_LAYERS,
	};

	struct CParticle
	{
		EKind m_Kind;
		vec2 m_Pos;
		vec2 m_Vel;
		float m_Life;
		float m_LifeSpan;
		float m_Size;
		float m_Rot;
		float m_RotSpeed;
		float m_Alpha;
		float m_SpeedScale;
		float m_SizeScale;
		ColorRGBA m_Color;
		bool m_UseCustomColor;
		bool m_Landed;
	};

	struct CBeam
	{
		vec2 m_From;
		vec2 m_To;
		float m_Width;
		ColorRGBA m_Color;
	};

	struct CSystem
	{
		std::vector<CParticle> m_vParticles;
		std::vector<CBeam> m_vBeams;
		float m_LastTime = 0.0f;
		float m_NextEmitTime = 0.0f;
	};
	CSystem m_World;
	CSystem m_aPreviews[NUM_PREVIEWS];

	struct SSource
	{
		CAccessories::SEffectAnchors m_Anchors;
		vec2 m_Direction;
		vec2 m_Vel;
		float m_SpeedScale;
		float m_SizeScale;
		float m_Alpha;
		float m_BeamLength;
		float m_ActiveTime;
		bool m_UseCustomColor;
		ColorRGBA m_CustomColor;
		bool m_Collides;
	};

	bool m_Held = false;
	int m_HeldId = -1;
	float m_LastUpdateTime = 0.0f;

	bool CanUse() const;
	int EquippedItem() const;
	void Press();

	bool Spawn(std::vector<CParticle> &vParticles, const CParticle &Particle) const;
	CParticle NewParticle(EKind Kind, const SSource &Source, vec2 Pos, vec2 Vel, float LifeSpan, float Size, ColorRGBA Color) const;
	void SpawnStream(CSystem &System, CParticle Particle, const SSource &Source, float Late) const;
	void EmitOne(CSystem &System, int Item, const SSource &Source, float Late) const;
	vec2 Reach(const SSource &Source, vec2 From, vec2 To) const;
	void EmitBeams(CSystem &System, const SSource &Source) const;
	void Pop(std::vector<CParticle> &vParticles, const CParticle &Bubble) const;
	void Splash(std::vector<CParticle> &vParticles, const CParticle &Tear) const;
	void Explode(std::vector<CParticle> &vParticles, const CParticle &Rocket) const;
	void Update(CSystem &System, float TimePassed, bool Collides) const;
	void Render(CSystem &System);
	void RenderBeams(const CSystem &System);
	void RenderBills(const CSystem &System);
	void RenderShapes(const CSystem &System);
	static bool LayerQuad(const CParticle &Particle, ELayer Layer, vec2 *pPos, float *pSize, ColorRGBA *pColor);

	static void ConKeyAccessoryEffect(IConsole::IResult *pResult, void *pUserData);
};

#endif
