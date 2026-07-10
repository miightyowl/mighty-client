#ifndef GAME_CLIENT_COMPONENTS_WIDGETBAR_H
#define GAME_CLIENT_COMPONENTS_WIDGETBAR_H

#include <base/types.h>
#include <game/client/component.h>
#include <game/client/ui_rect.h>
#include <vector>

class CWidgetBar : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;

	void RenderBar(const CUIRect &Bar);

private:
	struct SSegment
	{
		char m_aLabel[24] = "";
		char m_aValue[40] = "";
	};

	void BuildSegments(std::vector<SSegment> &vLeft, std::vector<SSegment> &vCenter, std::vector<SSegment> &vRight);
	float SegmentWidth(float FontSize, const SSegment &Segment) const;
	float DrawSegment(float x, float y, float FontSize, const SSegment &Segment);
};

#endif
