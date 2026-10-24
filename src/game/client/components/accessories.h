#ifndef GAME_CLIENT_COMPONENTS_ACCESSORIES_H
#define GAME_CLIENT_COMPONENTS_ACCESSORIES_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

class CAnimState;
class CTeeRenderInfo;

class CAccessories : public CComponent
{
public:
	// one accessory can be equipped per slot
	enum
	{
		SLOT_HEAD = 0,
		SLOT_BODY,
		SLOT_FEET,
		// added after the others so the order in the whisper stays compatible
		SLOT_FACE,
		NUM_SLOTS,
	};

	// item 0 of every slot is "none"
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
		HEAD_CHICK,
		HEAD_FLOWER,
		HEAD_SNOWFLAKE,
		HEAD_PIZZA,
		HEAD_TOP_HAT,
		HEAD_PIRATE_HAT,
		HEAD_GOLDEN_HALO,
		HEAD_HEART,
		HEAD_GHOST,
		HEAD_BOW,
		HEAD_FIRE,
		HEAD_CHERRY_BLOSSOM,
		NUM_HEADS,
	};

	enum
	{
		BODY_NONE = 0,
		BODY_ANGEL_WINGS,
		BODY_DEVIL_WINGS,
		BODY_BAT_WINGS,
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
		NUM_FACES,
	};

	enum
	{
		MAX_ITEMS = 32,
	};

	struct SSlotSettings
	{
		int m_Item;
		int m_Size;
		// horizontal offset, or the spread for paired items like wings
		int m_X;
		int m_Y;
		unsigned m_Color;

		bool operator==(const SSlotSettings &Other) const = default;
	};

	struct SSettings
	{
		SSlotSettings m_aSlots[NUM_SLOTS];
		bool m_UseCustomColor;

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
	};

	// Dummy = false is the main player, true is the dummy
	static SConfig Config(bool Dummy);
	static SSettings Settings(bool Dummy);
	// compact text form for sending them to other M-Client players, without spaces or semicolons
	static void Serialize(const SSettings &Settings, char *pBuf, int BufSize);
	static bool Deserialize(const char *pText, SSettings *pSettings);
	static bool AnyEquipped(const SSettings &Settings);

	// every item remembers its own size and offsets, items that were never adjusted use the defaults
	// returns false and the defaults if the item was never adjusted
	static bool LoadAdjustment(bool Dummy, int Slot, int Item, int *pSize, int *pX, int *pY);
	static void SaveAdjustment(bool Dummy, int Slot, int Item, int Size, int X, int Y);
	// puts on another item of the slot, keeping the adjustment of the old one and restoring the one of the new
	static void Equip(bool Dummy, int Slot, int Item);
	static void ItemKey(int Slot, int Item, char *pBuf, int BufSize);

	static int NumItems(int Slot);
	static const char *ItemName(int Slot, int Item);
	static const char *SlotName(int Slot);
	// paired items (wings) have a left and a right image and use a spread instead of a horizontal offset
	static bool IsPaired(int Slot, int Item);

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;

	// back layer (body) is rendered before the tee, front layer (head, face, feet) after it
	void RenderBack(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings);
	// Direction is where the tee looks, face accessories follow the eyes
	void RenderFront(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, float Alpha, const SSettings &Settings);

private:
	// [slot][item][side], side 1 is only used by paired items
	IGraphics::CTextureHandle m_aaaTextures[NUM_SLOTS][MAX_ITEMS][2];
	// grayscale versions that get tinted with the custom color
	IGraphics::CTextureHandle m_aaaGrayTextures[NUM_SLOTS][MAX_ITEMS][2];

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
	void RenderCanvas(IGraphics::CTextureHandle Texture, vec2 Center, float Size, float Angle, ColorRGBA Color);
};

#endif
