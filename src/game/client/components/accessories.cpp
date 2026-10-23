#include "accessories.h"

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/animstate.h>
#include <game/client/render.h>

namespace
{
	// the accessory images are square canvases with the tee drawn in the middle,
	// the body of that reference tee is this fraction of the canvas wide
	constexpr float REF_BODY_FRACTION = 0.6f;
	constexpr float REF_BODY_HALF = REF_BODY_FRACTION / 2.0f;

	const char *const s_apHeadFiles[CAccessories::NUM_HEADS] = {nullptr, "accessories/angel.png", "accessories/demon_horns.png"};
	const char *const s_aapWingFiles[CAccessories::NUM_WINGS][2] = {
		{nullptr, nullptr},
		{"accessories/wing_left.png", "accessories/wing_right.png"},
		{"accessories/devilwing_left.png", "accessories/devilwing_right.png"},
	};
}

CAccessories::SSettings CAccessories::Settings(bool Dummy)
{
	SSettings Settings;
	if(Dummy)
	{
		Settings.m_Head = g_Config.m_ClMClientDummyAccessoryHead;
		Settings.m_HeadSize = g_Config.m_ClMClientDummyAccessoryHeadSize;
		Settings.m_HeadX = g_Config.m_ClMClientDummyAccessoryHeadX;
		Settings.m_HeadY = g_Config.m_ClMClientDummyAccessoryHeadY;
		Settings.m_Wings = g_Config.m_ClMClientDummyAccessoryWings;
		Settings.m_WingsSize = g_Config.m_ClMClientDummyAccessoryWingsSize;
		Settings.m_WingsSpread = g_Config.m_ClMClientDummyAccessoryWingsSpread;
		Settings.m_WingsY = g_Config.m_ClMClientDummyAccessoryWingsY;
	}
	else
	{
		Settings.m_Head = g_Config.m_ClMClientAccessoryHead;
		Settings.m_HeadSize = g_Config.m_ClMClientAccessoryHeadSize;
		Settings.m_HeadX = g_Config.m_ClMClientAccessoryHeadX;
		Settings.m_HeadY = g_Config.m_ClMClientAccessoryHeadY;
		Settings.m_Wings = g_Config.m_ClMClientAccessoryWings;
		Settings.m_WingsSize = g_Config.m_ClMClientAccessoryWingsSize;
		Settings.m_WingsSpread = g_Config.m_ClMClientAccessoryWingsSpread;
		Settings.m_WingsY = g_Config.m_ClMClientAccessoryWingsY;
	}
	return Settings;
}

void CAccessories::OnInit()
{
	for(int i = 0; i < NUM_HEADS; i++)
	{
		if(s_apHeadFiles[i] && Storage()->FileExists(s_apHeadFiles[i], IStorage::TYPE_ALL))
			m_aHeadTextures[i] = Graphics()->LoadTexture(s_apHeadFiles[i], IStorage::TYPE_ALL);
	}
	for(int i = 0; i < NUM_WINGS; i++)
	{
		for(int Side = 0; Side < 2; Side++)
		{
			if(s_aapWingFiles[i][Side] && Storage()->FileExists(s_aapWingFiles[i][Side], IStorage::TYPE_ALL))
				m_aaWingTextures[i][Side] = Graphics()->LoadTexture(s_aapWingFiles[i][Side], IStorage::TYPE_ALL);
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
	Fit.m_CanvasSize = Width / REF_BODY_FRACTION;
	Fit.m_Scale = BaseSize / 64.0f;
	Fit.m_Angle = pAnim->GetBody()->m_Angle * pi * 2.0f;
	return Fit;
}

void CAccessories::RenderCanvas(IGraphics::CTextureHandle Texture, vec2 Center, float Size, float Angle, float Alpha)
{
	if(!Texture.IsValid())
		return;
	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->QuadsSetRotation(Angle);
	IGraphics::CQuadItem QuadItem(Center.x, Center.y, Size, Size);
	Graphics()->QuadsDraw(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CAccessories::RenderBack(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings)
{
	if(Settings.m_Wings <= WINGS_NONE || Settings.m_Wings >= NUM_WINGS)
		return;

	const SFit Fit = FitToTee(pAnim, pInfo, Pos);
	const float Size = Fit.m_CanvasSize * Settings.m_WingsSize / 100.0f;
	const float AngleDeg = Fit.m_Angle * 180.0f / pi;
	for(int Side = 0; Side < 2; Side++)
	{
		const float Sign = Side == 0 ? -1.0f : 1.0f;
		// scale around the point where the wing touches the body, so bigger wings grow outwards
		const float AttachX = Sign * (Fit.m_CanvasSize * REF_BODY_HALF + Settings.m_WingsSpread * Fit.m_Scale);
		const vec2 Offset = vec2(AttachX - Sign * Size * REF_BODY_HALF, -Settings.m_WingsY * Fit.m_Scale);
		RenderCanvas(m_aaWingTextures[Settings.m_Wings][Side], Fit.m_Center + rotate(Offset, AngleDeg), Size, Fit.m_Angle, Alpha);
	}
}

void CAccessories::RenderFront(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings)
{
	if(Settings.m_Head <= HEAD_NONE || Settings.m_Head >= NUM_HEADS)
		return;

	const SFit Fit = FitToTee(pAnim, pInfo, Pos);
	const float Size = Fit.m_CanvasSize * Settings.m_HeadSize / 100.0f;
	// scale around the top of the head, so bigger accessories stay on the head
	const float TopY = -Fit.m_CanvasSize * REF_BODY_HALF;
	const vec2 Offset = vec2(Settings.m_HeadX * Fit.m_Scale, TopY + Size * REF_BODY_HALF - Settings.m_HeadY * Fit.m_Scale);
	RenderCanvas(m_aHeadTextures[Settings.m_Head], Fit.m_Center + rotate(Offset, Fit.m_Angle * 180.0f / pi), Size, Fit.m_Angle, Alpha);
}
