#include "accessories.h"

#include <base/str.h>

#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/animstate.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>

namespace
{
	// the accessory images are square canvases with the tee drawn in the middle,
	// the body of that reference tee is this fraction of the canvas wide
	constexpr float REF_BODY_FRACTION = 0.6f;
	constexpr float REF_BODY_HALF = REF_BODY_FRACTION / 2.0f;

	struct SItemInfo
	{
		// [side], paired items have a left and a right image
		const char *m_apFiles[2];
		// 0 for canvas images, otherwise the image is placed freely: its width in body widths
		float m_Size;
		// and its center relative to the anchor of the slot (top of the head, eyes, body center), in body widths
		vec2 m_Pos;
	};

	const SItemInfo s_aHeadItems[CAccessories::NUM_HEADS] = {
		{{nullptr, nullptr}, 0.0f, vec2(0.0f, 0.0f)},
		// tilted to the right, the bottom of the brim rests on the head
		{{"accessories/christmas_hat.png", nullptr}, 1.5f, vec2(0.14f, 0.17f)},
		{{"accessories/crown.png", nullptr}, 0.738f, vec2(0.002f, -0.158f)},
		{{"accessories/witch_hat.png", nullptr}, 1.299f, vec2(-0.003f, -0.343f)},
		{{"accessories/sprout.png", nullptr}, 0.446f, vec2(0.001f, -0.12f)},
		{{"accessories/party_hat.png", nullptr}, 0.763f, vec2(0.084f, -0.259f)},
		{{"accessories/antlers.png", nullptr}, 1.352f, vec2(0.0f, -0.123f)},
		{{"accessories/frog.png", nullptr}, 0.591f, vec2(-0.002f, -0.159f)},
		{{"accessories/chick.png", nullptr}, 0.542f, vec2(0.002f, -0.175f)},
		{{"accessories/flower.png", nullptr}, 0.424f, vec2(0.28f, -0.01f)},
		{{"accessories/snowflake.png", nullptr}, 0.427f, vec2(-0.28f, -0.024f)},
		{{"accessories/pizza.png", nullptr}, 0.532f, vec2(-0.001f, -0.134f)},
		{{"accessories/top_hat.png", nullptr}, 0.907f, vec2(-0.003f, -0.2f)},
		{{"accessories/pirate_hat.png", nullptr}, 1.27f, vec2(0.0f, -0.21f)},
		{{"accessories/golden_halo.png", nullptr}, 0.699f, vec2(-0.002f, -0.239f)},
		{{"accessories/heart.png", nullptr}, 0.427f, vec2(0.302f, 0.051f)},
		{{"accessories/ghost.png", nullptr}, 0.479f, vec2(0.0f, -0.161f)},
		{{"accessories/bow.png", nullptr}, 0.536f, vec2(0.246f, -0.068f)},
		{{"accessories/fire.png", nullptr}, 0.712f, vec2(0.005f, -0.251f)},
		{{"accessories/cherry_blossom.png", nullptr}, 0.458f, vec2(-0.278f, -0.045f)},
	};
	const SItemInfo s_aBodyItems[CAccessories::NUM_BODIES] = {
		{{nullptr, nullptr}, 0.0f, vec2(0.0f, 0.0f)},
		{{"accessories/wing_left.png", "accessories/wing_right.png"}, 0.0f, vec2(0.0f, 0.0f)},
		{{"accessories/devilwing_left.png", "accessories/devilwing_right.png"}, 0.0f, vec2(0.0f, 0.0f)},
		{{"accessories/bat_wings.png", nullptr}, 1.997f, vec2(0.004f, -0.104f)},
	};
	const SItemInfo s_aFaceItems[CAccessories::NUM_FACES] = {
		{{nullptr, nullptr}, 0.0f, vec2(0.0f, 0.0f)},
		{{"accessories/glasses.png", nullptr}, 1.05f, vec2(0.0f, 0.0f)},
		{{"accessories/sunglasses.png", nullptr}, 0.994f, vec2(0.004f, 0.002f)},
		{{"accessories/round_glasses.png", nullptr}, 0.994f, vec2(0.0f, 0.0f)},
	};

	const SItemInfo *ItemInfo(int Slot, int Item)
	{
		if(Item <= 0 || Item >= CAccessories::NumItems(Slot))
			return nullptr;
		switch(Slot)
		{
		case CAccessories::SLOT_HEAD: return &s_aHeadItems[Item];
		case CAccessories::SLOT_BODY: return &s_aBodyItems[Item];
		case CAccessories::SLOT_FACE: return &s_aFaceItems[Item];
		default: return nullptr;
		}
	}
}

CAccessories::SConfig CAccessories::Config(bool Dummy)
{
	SConfig Config;
	if(Dummy)
	{
		Config.m_aSlots[SLOT_HEAD] = {&g_Config.m_ClMClientDummyAccessoryHead, &g_Config.m_ClMClientDummyAccessoryHeadSize, &g_Config.m_ClMClientDummyAccessoryHeadX, &g_Config.m_ClMClientDummyAccessoryHeadY, &g_Config.m_ClMClientDummyAccessoryHeadColor};
		Config.m_aSlots[SLOT_BODY] = {&g_Config.m_ClMClientDummyAccessoryBody, &g_Config.m_ClMClientDummyAccessoryBodySize, &g_Config.m_ClMClientDummyAccessoryBodySpread, &g_Config.m_ClMClientDummyAccessoryBodyY, &g_Config.m_ClMClientDummyAccessoryBodyColor};
		Config.m_aSlots[SLOT_FEET] = {&g_Config.m_ClMClientDummyAccessoryFeet, &g_Config.m_ClMClientDummyAccessoryFeetSize, &g_Config.m_ClMClientDummyAccessoryFeetX, &g_Config.m_ClMClientDummyAccessoryFeetY, &g_Config.m_ClMClientDummyAccessoryFeetColor};
		Config.m_aSlots[SLOT_FACE] = {&g_Config.m_ClMClientDummyAccessoryFace, &g_Config.m_ClMClientDummyAccessoryFaceSize, &g_Config.m_ClMClientDummyAccessoryFaceX, &g_Config.m_ClMClientDummyAccessoryFaceY, &g_Config.m_ClMClientDummyAccessoryFaceColor};
		Config.m_pUseCustomColor = &g_Config.m_ClMClientDummyAccessoryUseCustomColor;
	}
	else
	{
		Config.m_aSlots[SLOT_HEAD] = {&g_Config.m_ClMClientAccessoryHead, &g_Config.m_ClMClientAccessoryHeadSize, &g_Config.m_ClMClientAccessoryHeadX, &g_Config.m_ClMClientAccessoryHeadY, &g_Config.m_ClMClientAccessoryHeadColor};
		Config.m_aSlots[SLOT_BODY] = {&g_Config.m_ClMClientAccessoryBody, &g_Config.m_ClMClientAccessoryBodySize, &g_Config.m_ClMClientAccessoryBodySpread, &g_Config.m_ClMClientAccessoryBodyY, &g_Config.m_ClMClientAccessoryBodyColor};
		Config.m_aSlots[SLOT_FEET] = {&g_Config.m_ClMClientAccessoryFeet, &g_Config.m_ClMClientAccessoryFeetSize, &g_Config.m_ClMClientAccessoryFeetX, &g_Config.m_ClMClientAccessoryFeetY, &g_Config.m_ClMClientAccessoryFeetColor};
		Config.m_aSlots[SLOT_FACE] = {&g_Config.m_ClMClientAccessoryFace, &g_Config.m_ClMClientAccessoryFaceSize, &g_Config.m_ClMClientAccessoryFaceX, &g_Config.m_ClMClientAccessoryFaceY, &g_Config.m_ClMClientAccessoryFaceColor};
		Config.m_pUseCustomColor = &g_Config.m_ClMClientAccessoryUseCustomColor;
	}
	return Config;
}

CAccessories::SSettings CAccessories::Settings(bool Dummy)
{
	const SConfig Config = CAccessories::Config(Dummy);
	SSettings Settings;
	for(int Slot = 0; Slot < NUM_SLOTS; Slot++)
	{
		const SSlotConfig &SlotConfig = Config.m_aSlots[Slot];
		Settings.m_aSlots[Slot] = {*SlotConfig.m_pItem, *SlotConfig.m_pSize, *SlotConfig.m_pX, *SlotConfig.m_pY, *SlotConfig.m_pColor};
	}
	Settings.m_UseCustomColor = *Config.m_pUseCustomColor != 0;
	return Settings;
}

bool CAccessories::AnyEquipped(const SSettings &Settings)
{
	for(const SSlotSettings &Slot : Settings.m_aSlots)
	{
		if(Slot.m_Item != 0)
			return true;
	}
	return false;
}

void CAccessories::Serialize(const SSettings &Settings, char *pBuf, int BufSize)
{
	// "<custom color>,<slot>,<slot>,<slot>" where a slot is "0" or "<item>.<size>.<x>.<y>.<color>"
	str_format(pBuf, BufSize, "%d", Settings.m_UseCustomColor ? 1 : 0);
	for(const SSlotSettings &Slot : Settings.m_aSlots)
	{
		char aSlot[64];
		if(Slot.m_Item == 0)
			str_copy(aSlot, ",0");
		else
			str_format(aSlot, sizeof(aSlot), ",%d.%d.%d.%d.%x", Slot.m_Item, Slot.m_Size, Slot.m_X, Slot.m_Y, Slot.m_Color & 0xffffffu);
		str_append(pBuf, aSlot, BufSize);
	}
}

bool CAccessories::Deserialize(const char *pText, SSettings *pSettings)
{
	SSettings Settings{};
	char aPart[64];
	const char *pRest = str_next_token(pText, ",", aPart, sizeof(aPart));
	int UseCustomColor;
	if(!pRest || !str_toint(aPart, &UseCustomColor))
		return false;
	Settings.m_UseCustomColor = UseCustomColor != 0;

	for(SSlotSettings &Slot : Settings.m_aSlots)
	{
		Slot = {0, 100, 0, 0, 65408};
		// clients from before a slot was added do not send it
		pRest = str_next_token(pRest, ",", aPart, sizeof(aPart));
		if(!pRest)
			break;

		char aValue[16];
		int aValues[4];
		int NumValues = 0;
		const char *pValues = aPart;
		while(NumValues < 4 && (pValues = str_next_token(pValues, ".", aValue, sizeof(aValue))) && str_toint(aValue, &aValues[NumValues]))
			NumValues++;
		if(NumValues == 1 && !pValues && aValues[0] == 0)
			continue; // "0" means nothing is equipped
		if(NumValues != 4 || !str_next_token(pValues, ".", aValue, sizeof(aValue)))
			return false;

		Slot.m_Item = std::clamp(aValues[0], 0, (int)MAX_ITEMS - 1);
		Slot.m_Size = std::clamp(aValues[1], 25, 300);
		Slot.m_X = std::clamp(aValues[2], -40, 40);
		Slot.m_Y = std::clamp(aValues[3], -40, 40);
		Slot.m_Color = (unsigned)str_toulong_base(aValue, 16) & 0xffffffu;
	}
	*pSettings = Settings;
	return true;
}

void CAccessories::ItemKey(int Slot, int Item, char *pBuf, int BufSize)
{
	// the image name, so remembered adjustments survive items being added or removed
	pBuf[0] = '\0';
	const SItemInfo *pInfo = ItemInfo(Slot, Item);
	if(!pInfo)
		return;
	const char *pName = str_startswith(pInfo->m_apFiles[0], "accessories/");
	str_copy(pBuf, pName ? pName : pInfo->m_apFiles[0], BufSize);
	const int Length = str_length(pBuf);
	if(Length > 4 && str_comp(pBuf + Length - 4, ".png") == 0)
		pBuf[Length - 4] = '\0';
}

bool CAccessories::LoadAdjustment(bool Dummy, int Slot, int Item, int *pSize, int *pX, int *pY)
{
	*pSize = 100;
	*pX = 0;
	*pY = 0;

	char aKey[64];
	ItemKey(Slot, Item, aKey, sizeof(aKey));
	if(aKey[0] == '\0')
		return false;

	// entries are "<item>.<size>.<x>.<y>"
	const char *pList = Dummy ? g_Config.m_ClMClientDummyAccessoryAdjustments : g_Config.m_ClMClientAccessoryAdjustments;
	char aEntry[96];
	while((pList = str_next_token(pList, " ", aEntry, sizeof(aEntry))))
	{
		char aName[64];
		const char *pValues = str_next_token(aEntry, ".", aName, sizeof(aName));
		if(!pValues || str_comp(aName, aKey) != 0)
			continue;
		int aValues[3];
		int NumValues = 0;
		char aValue[16];
		while(NumValues < 3 && (pValues = str_next_token(pValues, ".", aValue, sizeof(aValue))) && str_toint(aValue, &aValues[NumValues]))
			NumValues++;
		if(NumValues != 3)
			continue;
		*pSize = std::clamp(aValues[0], 25, 300);
		*pX = std::clamp(aValues[1], -40, 40);
		*pY = std::clamp(aValues[2], -40, 40);
		return true;
	}
	return false;
}

void CAccessories::SaveAdjustment(bool Dummy, int Slot, int Item, int Size, int X, int Y)
{
	char aKey[64];
	ItemKey(Slot, Item, aKey, sizeof(aKey));
	if(aKey[0] == '\0')
		return;

	char *pList = Dummy ? g_Config.m_ClMClientDummyAccessoryAdjustments : g_Config.m_ClMClientAccessoryAdjustments;
	const int ListSize = Dummy ? sizeof(g_Config.m_ClMClientDummyAccessoryAdjustments) : sizeof(g_Config.m_ClMClientAccessoryAdjustments);

	// keep the other entries, then add this one unless it is the default
	char aPrefix[72];
	str_format(aPrefix, sizeof(aPrefix), "%s.", aKey);
	char aNewList[1024] = "";
	const char *pRest = pList;
	char aEntry[96];
	while((pRest = str_next_token(pRest, " ", aEntry, sizeof(aEntry))))
	{
		// entries starting with a digit are from when items were remembered by their number
		if(str_startswith(aEntry, aPrefix) || (aEntry[0] >= '0' && aEntry[0] <= '9'))
			continue;
		if(aNewList[0] != '\0')
			str_append(aNewList, " ");
		str_append(aNewList, aEntry);
	}
	if(Size != 100 || X != 0 || Y != 0)
	{
		str_format(aEntry, sizeof(aEntry), "%s%d.%d.%d", aPrefix, Size, X, Y);
		if(str_length(aNewList) + str_length(aEntry) + 2 <= ListSize)
		{
			if(aNewList[0] != '\0')
				str_append(aNewList, " ");
			str_append(aNewList, aEntry);
		}
	}
	str_copy(pList, aNewList, ListSize);
}

void CAccessories::Equip(bool Dummy, int Slot, int Item)
{
	const SSlotConfig &SlotConfig = Config(Dummy).m_aSlots[Slot];
	if(*SlotConfig.m_pItem == Item)
		return;
	SaveAdjustment(Dummy, Slot, *SlotConfig.m_pItem, *SlotConfig.m_pSize, *SlotConfig.m_pX, *SlotConfig.m_pY);
	*SlotConfig.m_pItem = Item;
	LoadAdjustment(Dummy, Slot, Item, SlotConfig.m_pSize, SlotConfig.m_pX, SlotConfig.m_pY);
}

int CAccessories::NumItems(int Slot)
{
	switch(Slot)
	{
	case SLOT_HEAD: return NUM_HEADS;
	case SLOT_BODY: return NUM_BODIES;
	case SLOT_FEET: return NUM_FEET;
	case SLOT_FACE: return NUM_FACES;
	default: return 0;
	}
}

const char *CAccessories::ItemName(int Slot, int Item)
{
	if(Item == 0)
		return Localize("None");
	if(Slot == SLOT_HEAD)
	{
		switch(Item)
		{
		case HEAD_CHRISTMAS_HAT: return Localize("Christmas hat");
		case HEAD_CROWN: return Localize("Crown");
		case HEAD_WITCH_HAT: return Localize("Witch hat");
		case HEAD_SPROUT: return Localize("Sprout");
		case HEAD_PARTY_HAT: return Localize("Party hat");
		case HEAD_ANTLERS: return Localize("Antlers");
		case HEAD_FROG: return Localize("Frog");
		case HEAD_CHICK: return Localize("Chick");
		case HEAD_FLOWER: return Localize("Flower");
		case HEAD_SNOWFLAKE: return Localize("Snowflake");
		case HEAD_PIZZA: return Localize("Pizza");
		case HEAD_TOP_HAT: return Localize("Top hat");
		case HEAD_PIRATE_HAT: return Localize("Pirate hat");
		case HEAD_GOLDEN_HALO: return Localize("Golden halo");
		case HEAD_HEART: return Localize("Heart pin");
		case HEAD_GHOST: return Localize("Ghost");
		case HEAD_BOW: return Localize("Bow");
		case HEAD_FIRE: return Localize("Fire");
		case HEAD_CHERRY_BLOSSOM: return Localize("Cherry blossom");
		}
	}
	else if(Slot == SLOT_BODY)
	{
		switch(Item)
		{
		case BODY_ANGEL_WINGS: return Localize("Angel wings");
		case BODY_DEVIL_WINGS: return Localize("Devil wings");
		case BODY_BAT_WINGS: return Localize("Bat wings");
		}
	}
	else if(Slot == SLOT_FACE)
	{
		switch(Item)
		{
		case FACE_GLASSES: return Localize("Glasses");
		case FACE_SUNGLASSES: return Localize("Sunglasses");
		case FACE_ROUND_GLASSES: return Localize("Round glasses");
		}
	}
	return "";
}

const char *CAccessories::SlotName(int Slot)
{
	switch(Slot)
	{
	case SLOT_HEAD: return Localize("Head");
	case SLOT_BODY: return Localize("Body");
	case SLOT_FEET: return Localize("Feet");
	case SLOT_FACE: return Localize("Face");
	default: return "";
	}
}

bool CAccessories::IsPaired(int Slot, int Item)
{
	const SItemInfo *pInfo = ItemInfo(Slot, Item);
	return pInfo && pInfo->m_apFiles[1] != nullptr;
}

void CAccessories::OnInit()
{
	for(int Slot = 0; Slot < NUM_SLOTS; Slot++)
	{
		for(int Item = 0; Item < NumItems(Slot); Item++)
		{
			for(int Side = 0; Side < 2; Side++)
			{
				const SItemInfo *pInfo = ItemInfo(Slot, Item);
				const char *pFile = pInfo ? pInfo->m_apFiles[Side] : nullptr;
				if(!pFile)
					continue;
				CImageInfo Image;
				if(!Graphics()->LoadPng(Image, pFile, IStorage::TYPE_ALL))
					continue;

				// grayscale copy made the same way as for skins: the most common shade becomes the
				// picked color and the rest gets darker or lighter around it
				CImageInfo Gray = Image.DeepCopy();
				ConvertToGrayscale(Gray);
				if(Gray.m_Format == CImageInfo::FORMAT_RGBA)
				{
					const size_t NumPixels = Gray.m_Width * Gray.m_Height;
					int aFreq[256] = {0};
					for(size_t i = 0; i < NumPixels; i++)
					{
						if(Gray.m_pData[i * 4 + 3] > 128)
							aFreq[Gray.m_pData[i * 4]]++;
					}
					uint8_t OrgWeight = 1;
					for(int i = 1; i < 256; i++)
					{
						if(aFreq[OrgWeight] < aFreq[i])
							OrgWeight = i;
					}
					const uint8_t NewWeight = 192;
					for(size_t i = 0; i < NumPixels; i++)
					{
						uint8_t Value = Gray.m_pData[i * 4];
						if(Value <= OrgWeight)
							Value = (uint8_t)((Value / (float)OrgWeight) * NewWeight);
						else
							Value = (uint8_t)(((Value - OrgWeight) / (float)(255 - OrgWeight)) * (255 - NewWeight) + NewWeight);
						Gray.m_pData[i * 4] = Value;
						Gray.m_pData[i * 4 + 1] = Value;
						Gray.m_pData[i * 4 + 2] = Value;
					}
				}

				m_aaaTextures[Slot][Item][Side] = Graphics()->LoadTextureRawMove(Image, 0, pFile);
				m_aaaGrayTextures[Slot][Item][Side] = Graphics()->LoadTextureRawMove(Gray, 0, pFile);
			}
		}
	}
}

CAccessories::SFit CAccessories::FitToTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos)
{
	float AnimScale, BaseSize;
	CRenderTools::GetRenderTeeAnimScaleAndBaseSize(pInfo, AnimScale, BaseSize);
	// same as CRenderTools::GetRenderTeeBodyScale, which is private
	const float BodyScale = (g_Config.m_ClFatSkins ? BaseSize * 1.3f : BaseSize) / 64.0f;
	const vec2 BodyPos = Pos + vec2(pAnim->GetBody()->m_X, pAnim->GetBody()->m_Y) * AnimScale;

	// width of the default skin's body, used for 0.7 tees and skins without metrics
	float Width = 0.66f * 64.0f * BodyScale;
	float Left = BodyPos.x - Width / 2.0f;
	float Bottom = BodyPos.y + Width / 2.0f;
	if(!pInfo->m_aSixup[g_Config.m_ClDummy].PartTexture(protocol7::SKINPART_BODY).IsValid())
	{
		vec2 BodyOffset;
		float BodyWidth, BodyHeight;
		CRenderTools::GetRenderTeeBodySize(pAnim, pInfo, BodyOffset, BodyWidth, BodyHeight);
		if(BodyWidth > 1.0f && BodyHeight > 1.0f)
		{
			Width = BodyWidth;
			Left = BodyPos.x - 32.0f * BodyScale + BodyOffset.x;
			Bottom = BodyPos.y - 32.0f * BodyScale + BodyOffset.y + BodyHeight;
		}
	}

	// fit by width and anchor at the bottom, hats and ears on skins only make them taller
	SFit Fit;
	Fit.m_Center = vec2(Left + Width / 2.0f, Bottom - Width / 2.0f);
	Fit.m_BodyPos = BodyPos;
	Fit.m_CanvasSize = Width / REF_BODY_FRACTION;
	Fit.m_Scale = BaseSize / 64.0f;
	Fit.m_Angle = pAnim->GetBody()->m_Angle * pi * 2.0f;
	return Fit;
}

void CAccessories::RenderCanvas(IGraphics::CTextureHandle Texture, vec2 Center, float Size, float Angle, ColorRGBA Color)
{
	if(!Texture.IsValid())
		return;
	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	Graphics()->QuadsSetRotation(Angle);
	IGraphics::CQuadItem QuadItem(Center.x, Center.y, Size, Size);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAccessories::RenderSlot(int Slot, const SFit &Fit, vec2 Direction, float Alpha, const SSettings &Settings)
{
	const SSlotSettings &SlotSettings = Settings.m_aSlots[Slot];
	const SItemInfo *pItem = ItemInfo(Slot, SlotSettings.m_Item);
	if(!pItem)
		return;

	const auto &aTextures = Settings.m_UseCustomColor ? m_aaaGrayTextures[Slot][SlotSettings.m_Item] : m_aaaTextures[Slot][SlotSettings.m_Item];
	ColorRGBA Color = Settings.m_UseCustomColor ? color_cast<ColorRGBA>(ColorHSLA(SlotSettings.m_Color).UnclampLighting(ColorHSLA::DARKEST_LGT)) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	Color.a = Alpha;

	const float AngleDeg = Fit.m_Angle * 180.0f / pi;
	const float Scale = SlotSettings.m_Size / 100.0f;
	const vec2 SliderOffset = vec2(SlotSettings.m_X * Fit.m_Scale, -SlotSettings.m_Y * Fit.m_Scale);
	if(pItem->m_Size > 0.0f)
	{
		// freely placed image, scaled around the anchor of the slot
		const float BodyWidth = Fit.m_CanvasSize * REF_BODY_FRACTION;
		vec2 Anchor;
		if(Slot == SLOT_FACE)
		{
			// same offset as the eyes in CRenderTools::RenderTee
			const float BaseSize = Fit.m_Scale * 64.0f;
			Anchor = Fit.m_BodyPos + vec2(Direction.x * 0.125f, -0.05f + Direction.y * 0.10f) * BaseSize;
		}
		else
		{
			// top of the head, body center or bottom of the body
			const float AnchorY = Slot == SLOT_HEAD ? -BodyWidth / 2.0f : (Slot == SLOT_BODY ? 0.0f : BodyWidth / 2.0f);
			Anchor = Fit.m_Center + rotate(vec2(0.0f, AnchorY), AngleDeg);
		}
		const vec2 Offset = pItem->m_Pos * BodyWidth * Scale + SliderOffset;
		RenderCanvas(aTextures[0], Anchor + rotate(Offset, AngleDeg), pItem->m_Size * BodyWidth * Scale, Fit.m_Angle, Color);
		return;
	}

	const float Size = Fit.m_CanvasSize * SlotSettings.m_Size / 100.0f;
	if(IsPaired(Slot, SlotSettings.m_Item))
	{
		for(int Side = 0; Side < 2; Side++)
		{
			const float Sign = Side == 0 ? -1.0f : 1.0f;
			// scale around the point where the item touches the body, so bigger items grow outwards
			const float AttachX = Sign * (Fit.m_CanvasSize * REF_BODY_HALF + SlotSettings.m_X * Fit.m_Scale);
			const vec2 Offset = vec2(AttachX - Sign * Size * REF_BODY_HALF, -SlotSettings.m_Y * Fit.m_Scale);
			RenderCanvas(aTextures[Side], Fit.m_Center + rotate(Offset, AngleDeg), Size, Fit.m_Angle, Color);
		}
		return;
	}

	// scale around the top of the head or the bottom of the body, so bigger items stay in place
	const float AnchorSign = Slot == SLOT_HEAD ? -1.0f : 1.0f;
	const float AnchorY = AnchorSign * (Fit.m_CanvasSize - Size) * REF_BODY_HALF;
	const vec2 Offset = vec2(SlotSettings.m_X * Fit.m_Scale, AnchorY - SlotSettings.m_Y * Fit.m_Scale);
	RenderCanvas(aTextures[0], Fit.m_Center + rotate(Offset, AngleDeg), Size, Fit.m_Angle, Color);
}

void CAccessories::RenderBack(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings)
{
	const SFit Fit = FitToTee(pAnim, pInfo, Pos);
	RenderSlot(SLOT_BODY, Fit, vec2(1.0f, 0.0f), Alpha, Settings);
}

void CAccessories::RenderFront(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, vec2 Direction, float Alpha, const SSettings &Settings)
{
	const SFit Fit = FitToTee(pAnim, pInfo, Pos);
	RenderSlot(SLOT_FEET, Fit, Direction, Alpha, Settings);
	RenderSlot(SLOT_FACE, Fit, Direction, Alpha, Settings);
	RenderSlot(SLOT_HEAD, Fit, Direction, Alpha, Settings);
}
