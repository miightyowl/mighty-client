#include "widgetbar.h"

#include <base/color.h>
#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud.h>
#include <game/client/gameclient.h>

#include <ctime>

void CWidgetBar::BuildSegments(std::vector<SSegment> &vLeft, std::vector<SSegment> &vCenter, std::vector<SSegment> &vRight)
{
	const bool InGame = (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK);
	const int Conn = g_Config.m_ClDummy;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const bool HasChar = InGame && LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active;

	const auto &&Add = [&](int Mode, const char *pLabel, const char *pValue) {
		std::vector<SSegment> *pTarget = Mode == 1 ? &vLeft : (Mode == 2 ? &vCenter : (Mode == 3 ? &vRight : nullptr));
		if(pTarget == nullptr)
			return;
		SSegment Segment;
		str_copy(Segment.m_aLabel, pLabel);
		str_copy(Segment.m_aValue, pValue);
		pTarget->push_back(Segment);
	};

	char aValue[40];

	// clock
	{
		char aClock[16] = "--:--";
		const std::time_t Now = std::time(nullptr);
		std::tm *pLocal = std::localtime(&Now);
		if(pLocal != nullptr)
			std::strftime(aClock, sizeof(aClock), "%H:%M", pLocal);
		Add(g_Config.m_ClMClientInfoBarClock, "", aClock);
	}

	// fps
	{
		const int Fps = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aValue, sizeof(aValue), "%d", Fps);
		Add(g_Config.m_ClMClientInfoBarFps, "FPS", aValue);
	}

	// ping
	{
		int Ping = 0;
		if(InGame && LocalId >= 0 && GameClient()->m_Snap.m_apPlayerInfos[LocalId] != nullptr)
			Ping = std::clamp(GameClient()->m_Snap.m_apPlayerInfos[LocalId]->m_Latency, 0, 999);
		str_format(aValue, sizeof(aValue), "%d", Ping);
		Add(g_Config.m_ClMClientInfoBarPing, "Ping", aValue);
	}

	// prediction
	{
		str_format(aValue, sizeof(aValue), "%d", InGame ? Client()->GetPredictionTime() : 0);
		Add(g_Config.m_ClMClientInfoBarPred, "Pred", aValue);
	}

	// position / speed / angle
	CHud::CMovementInformation Info;
	Info.m_Pos = vec2(0.0f, 0.0f);
	Info.m_Speed = vec2(0.0f, 0.0f);
	if(HasChar)
		Info = GameClient()->m_Hud.GetMovementInformation(LocalId, Conn);

	{
		str_format(aValue, sizeof(aValue), "%.2f, %.2f", Info.m_Pos.x, Info.m_Pos.y);
		Add(g_Config.m_ClMClientInfoBarPos, "Pos", aValue);
	}
	{
		str_format(aValue, sizeof(aValue), "%.2f", Info.m_Angle);
		Add(g_Config.m_ClMClientInfoBarAngle, "Angle", aValue);
	}
	{
		str_format(aValue, sizeof(aValue), "%.2f", length(Info.m_Speed));
		Add(g_Config.m_ClMClientInfoBarSpeed, "Speed", aValue);
	}
}

float CWidgetBar::SegmentWidth(float FontSize, const SSegment &Segment) const
{
	float Width = 0.0f;
	if(Segment.m_aLabel[0] != '\0')
		Width += TextRender()->TextWidth(FontSize, Segment.m_aLabel) + FontSize * 0.35f;
	Width += TextRender()->TextWidth(FontSize, Segment.m_aValue);
	return Width;
}

float CWidgetBar::DrawSegment(float x, float y, float FontSize, const SSegment &Segment)
{
	const ColorRGBA LabelColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMClientInfoBarLabelColor, false));
	const ColorRGBA ValueColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMClientInfoBarValueColor, false));
	if(Segment.m_aLabel[0] != '\0')
	{
		TextRender()->TextColor(LabelColor);
		TextRender()->Text(x, y, FontSize, Segment.m_aLabel, -1.0f);
		x += TextRender()->TextWidth(FontSize, Segment.m_aLabel) + FontSize * 0.35f;
	}
	TextRender()->TextColor(ValueColor);
	TextRender()->Text(x, y, FontSize, Segment.m_aValue, -1.0f);
	x += TextRender()->TextWidth(FontSize, Segment.m_aValue);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	return x;
}

void CWidgetBar::RenderBar(const CUIRect &Bar)
{
	CUIRect Background = Bar;
	const ColorRGBA BgColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMClientInfoBarBgColor, false));
	Background.Draw(BgColor.WithAlpha(g_Config.m_ClMClientInfoBarAlpha / 100.0f), IGraphics::CORNER_T, 6.0f);

	const float FontSize = std::clamp(Bar.h * 0.55f, 6.0f, 12.0f);
	float y = Bar.y + (Bar.h - FontSize) / 2.0f;
	const float Pad = 10.0f;
	const float Gap = 16.0f;

	std::vector<SSegment> vLeft, vCenter, vRight;
	BuildSegments(vLeft, vCenter, vRight);

	float x = Bar.x + Pad;
	for(const SSegment &Segment : vLeft)
	{
		x = DrawSegment(x, y, FontSize, Segment) + Gap;
	}

	float XRight = Bar.x + Bar.w - Pad;
	for(const SSegment &Segment : vRight)
	{
		XRight -= SegmentWidth(FontSize, Segment);
		DrawSegment(XRight, y, FontSize, Segment);
		XRight -= Gap;
	}

	// center group
	if(!vCenter.empty())
	{
		float TotalWidth = 0.0f;
		for(const SSegment &Segment : vCenter)
			TotalWidth += SegmentWidth(FontSize, Segment) + Gap;
		TotalWidth -= Gap;
		float XCenter = Bar.x + (Bar.w - TotalWidth) / 2.0f;
		for(const SSegment &Segment : vCenter)
			XCenter = DrawSegment(XCenter, y, FontSize, Segment) + Gap;
	}
}

void CWidgetBar::OnRender()
{
	if(!g_Config.m_ClMClientInfoBar || !g_Config.m_ClShowhud)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_Menus.IsActive())
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();
	const float Height = (float)g_Config.m_ClMClientInfoBarHeight;
	CUIRect Bar = {Screen.x, Screen.y + Screen.h - Height, Screen.w, Height};
	RenderBar(Bar);
}
