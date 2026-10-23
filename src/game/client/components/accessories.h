#ifndef GAME_CLIENT_COMPONENTS_ACCESSORIES_H
#define GAME_CLIENT_COMPONENTS_ACCESSORIES_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

class CAnimState;
class CTeeRenderInfo;

class CAccessories : public CComponent
{
public:
	enum
	{
		HEAD_NONE = 0,
		HEAD_HALO,
		HEAD_HORNS,
		NUM_HEADS,
	};

	enum
	{
		WINGS_NONE = 0,
		WINGS_ANGEL,
		WINGS_DEVIL,
		NUM_WINGS,
	};

	struct SSettings
	{
		int m_Head;
		int m_HeadSize;
		int m_HeadX;
		int m_HeadY;
		int m_Wings;
		int m_WingsSize;
		int m_WingsSpread;
		int m_WingsY;
	};

	// Dummy = false is the main player, true is the dummy
	static SSettings Settings(bool Dummy);

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;

	// back layer (wings) is rendered before the tee, front layer (head) after it
	void RenderBack(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings);
	void RenderFront(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos, float Alpha, const SSettings &Settings);

private:
	IGraphics::CTextureHandle m_aHeadTextures[NUM_HEADS];
	IGraphics::CTextureHandle m_aaWingTextures[NUM_WINGS][2];

	struct SFit
	{
		vec2 m_Center;
		float m_CanvasSize;
		float m_Scale;
		float m_Angle;
	};
	static SFit FitToTee(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, vec2 Pos);
	void RenderCanvas(IGraphics::CTextureHandle Texture, vec2 Center, float Size, float Angle, float Alpha);
};

#endif
