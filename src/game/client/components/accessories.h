#ifndef GAME_CLIENT_COMPONENTS_ACCESSORIES_H
#define GAME_CLIENT_COMPONENTS_ACCESSORIES_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

class CAnimState;
class CRenderTools;
class CTeeRenderInfo;

class CAccessories : public CComponent
{
public:
	enum
	{
		SLOT_HEAD = 0,
		SLOT_BODY,
		SLOT_FEET,
		SLOT_FACE,
		SLOT_PARTICLES,
		SLOT_INTERACTIVE,
		NUM_SLOTS,
	};

	enum
	{
		HEAD_NONE = 0,
		HEAD_CHRISTMAS_HAT,
		HEAD_CROWN,
		HEAD_WITCH_HAT,
		HEAD_SPROUT,
		HEAD_PARTY_HAT,
		HEAD_ANTLERS,
		HEAD_FROG,
		HEAD_FLOWER,
		HEAD_SNOWFLAKE,
		HEAD_TOP_HAT,
		HEAD_PIRATE_HAT,
		HEAD_GOLDEN_HALO,
		HEAD_HEART,
		HEAD_GHOST,
		HEAD_BOW,
		HEAD_CHERRY_BLOSSOM,
		HEAD_MOHAWK,
		HEAD_TRAFFIC_CONE,
		HEAD_RUBBER_DUCK,
		HEAD_PROPELLER_CAP,
		HEAD_PLUNGER,
		HEAD_GOLDEN_CHRISTMAS_HAT,
		HEAD_HOLLY_HAT,
		HEAD_CHRISTMAS_HAT_LEFT,
		HEAD_CHRISTMAS_HAT_LONG,
		HEAD_CHRISTMAS_HAT_BLACK,
		NUM_HEADS,
	};

	enum
	{
		BODY_NONE = 0,
		BODY_ANGEL_WINGS,
		BODY_DEVIL_WINGS,
		BODY_BAT_WINGS,
		BODY_GOLDEN_CHAIN,
		BODY_BOW_TIE,
		NUM_BODIES,
	};

	enum
	{
		FEET_NONE = 0,
		NUM_FEET,
	};

	enum
	{
		FACE_NONE = 0,
		FACE_GLASSES,
		FACE_SUNGLASSES,
		FACE_ROUND_GLASSES,
		FACE_DISGUISE,
		FACE_MOUSTACHE,
		FACE_CLOWN_NOSE,
		FACE_DOLLAR_GLASSES,
		NUM_FACES,
	};

	enum
	{
		PARTICLES_NONE = 0,
		PARTICLES_SPARKLES,
		PARTICLES_GOLDEN_SPARKLES,
		PARTICLES_RAINBOW_SPARKLES,
		PARTICLES_ORBIT,
		PARTICLES_HEARTS,
		PARTICLES_SNOW,
		PARTICLES_EMBERS,
		PARTICLES_BUBBLES,
		PARTICLES_SMOKE,
		PARTICLES_PETALS,
		NUM_PARTICLES,
	};

	enum
	{
		INTERACTIVE_NONE = 0,
		INTERACTIVE_FIRE,
		INTERACTIVE_ICE,
		INTERACTIVE_BUBBLES,
		INTERACTIVE_RAINBOW,
		INTERACTIVE_FIREWORK,
		INTERACTIVE_LASER,
		INTERACTIVE_KISSES,
		INTERACTIVE_MONEY,
		INTERACTIVE_CRYING,
		INTERACTIVE_RAIN_CLOUD,
		INTERACTIVE_GROUND_SLAM,
		INTERACTIVE_SLEEPY,
		INTERACTIVE_STEAM,
		NUM_INTERACTIVE,
	};

	enum
	{
		MAX_ITEMS = 32,
	};

	enum
	{
		LIST_FAVORITE = 0,
	};

	enum
	{
		CUSTOM_COLOR_ALL_SLOTS = (1 << SLOT_HEAD) | (1 << SLOT_BODY) | (1 << SLOT_FEET) | (1 << SLOT_FACE),
	};

	static constexpr int MIN_SIZE = 25;

	struct SSlotSettings
	{
		int m_Item;
		int m_Size;
		int m_X;
		int m_Y;
		unsigned m_Color;

		bool operator==(const SSlotSettings &Other) const = default;
	};

	struct SSettings
	{
		SSlotSettings m_aSlots[NUM_SLOTS];
		bool m_UseCustomColor;
		int m_CustomColorSlots;

		bool operator==(const SSettings &Other) const = default;
	};

	struct SSlotConfig
	{
		int *m_pItem;
		int *m_pSize;
		int *m_pX;
		int *m_pY;
		unsigned *m_pColor;
	};

	struct SConfig
	{
		SSlotConfig m_aSlots[NUM_SLOTS];
		int *m_pUseCustomColor;
		int *m_pCustomColorSlots;
	};

	static SConfig Config(bool Dummy);
	static SSettings Settings(bool Dummy);
	static void Serialize(const SSettings &Settings, char *pBuf, int BufSize);
	static bool Deserialize(const char *pText, SSettings *pSettings);
	static bool AnyEquipped(const SSettings &Settings);
	static void ApplySettings(bool Dummy, const SSettings &Settings);

	static bool LoadAdjustment(bool Dummy, int Slot, int Item, int *pSize, int *pX, int *pY);
	static void SaveAdjustment(bool Dummy, int Slot, int Item, int Size, int X, int Y);
	static void Equip(bool Dummy, int Slot, int Item);
	static void ItemKey(int Slot, int Item, char *pBuf, int BufSize);

	static bool IsInList(int List, int Slot, int Item);
	static void SetInList(int List, int Slot, int Item, bool Listed);

	static int NumItems(int Slot);
	static int MaxSize(int Slot, int Item);
	static const char *ItemName(int Slot, int Item);
	static const char *SlotName(int Slot);
	static bool IsPaired(int Slot, int Item);

	IGraphics::CTextureHandle HeartTexture() const { return m_aaaTextures[SLOT_HEAD][HEAD_HEART][0]; }

	struct SEffectAnchors
	{
		vec2 m_Mouth;
		vec2 m_aEyes[2];
		vec2 m_aSides[2];
		vec2 m_Feet;
	};
	static SEffectAnchors EffectAnchors(const CRenderTools *pRenderTools, const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, const SSlotSettings &Slot);

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;

	void RenderBack(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings);
	void RenderFront(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, float Alpha, const SSettings &Settings);

private:
	IGraphics::CTextureHandle m_aaaTextures[NUM_SLOTS][MAX_ITEMS][2];
	IGraphics::CTextureHandle m_aaaGrayTextures[NUM_SLOTS][MAX_ITEMS][2];
	IGraphics::CTextureHandle m_aaKeepWhiteTextures[NUM_SLOTS][MAX_ITEMS];

	struct SFit
	{
		vec2 m_Center;
		vec2 m_BodyPos;
		float m_CanvasSize;
		float m_Scale;
		float m_Angle;
	};
	static SFit FitToTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos);
	void RenderSlot(int Slot, const SFit &Fit, vec2 Direction, float Alpha, const SSettings &Settings);
	void RenderParticles(const SFit &Fit, float Alpha, const SSettings &Settings);
	void RenderCanvas(IGraphics::CTextureHandle Texture, vec2 Center, float Size, float Angle, ColorRGBA Color, float HeightScale = 1.0f);
};

#endif
