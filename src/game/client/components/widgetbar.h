#ifndef GAME_CLIENT_COMPONENTS_WIDGETBAR_H
#define GAME_CLIENT_COMPONENTS_WIDGETBAR_H

#include <base/types.h>
#include <base/vmath.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <vector>

class CWidgetBar : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

	void RenderBar(const CUIRect &Bar);

	int CurrentEdgeJumpDirection(vec2 *pPos = nullptr, bool *pDouble = nullptr) const;

private:
	struct SSegment
	{
		char m_aLabel[24] = "";
		char m_aValue[40] = "";
		bool m_Highlight = false;
	};

	void BuildSegments(std::vector<SSegment> &vLeft, std::vector<SSegment> &vCenter, std::vector<SSegment> &vRight);
	bool IsFreezeAt(int TileX, int TileY) const;
	int EdgeJumpDirection(vec2 Pos) const;
	bool EdgeJumpInfo(char *pBuf, int BufSize, bool *pOnSpot) const;
	float SegmentWidth(float FontSize, const SSegment &Segment) const;
	float DrawSegment(float x, float y, float FontSize, const SSegment &Segment);
};

#endif
