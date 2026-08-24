#include "widgetbar.h"

#include <base/color.h>
#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <cmath>
#include <ctime>

static const int s_aEdgeRight[] = {62, 63, 66, 81};
static const int s_aEdgeRightDouble[] = {56, 69, 72, 84};
static const int s_aEdgeLeft[] = {16, 31, 34};
static const int s_aEdgeLeftDouble[] = {13, 25, 28, 41};

static bool ContainsCoordinate(const int *pValues, int Count, int Decimal)
{
	for(int i = 0; i < Count; i++)
	{
		if(pValues[i] == Decimal)
			return true;
	}
	return false;
}

static int NearestCoordinate(const int *pValues, int Count, int Decimal, int Best)
{
	for(int i = 0; i < Count; i++)
	{
		if(Best < 0 || absolute(pValues[i] - Decimal) < absolute(Best - Decimal))
			Best = pValues[i];
	}
	return Best;
}

bool CWidgetBar::IsFreezeAt(int TileX, int TileY) const
{
	if(TileX < 0 || TileY < 0 || TileX >= Collision()->GetWidth() || TileY >= Collision()->GetHeight())
		return false;
	const int Index = TileY * Collision()->GetWidth() + TileX;
	const auto &&Freeze = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE || Tile == TILE_LFREEZE;
	};
	return Freeze(Collision()->GetTileIndex(Index)) || Freeze(Collision()->GetFrontTileIndex(Index));
}

int CWidgetBar::EdgeJumpDirection(vec2 Pos) const
{
	const int Row = (int)std::floor(Pos.y / 32.0f);
	const int TeeX = (int)std::floor(Pos.x / 32.0f);

	if(IsFreezeAt(TeeX, Row))
		return 0;

	for(const int Dir : {1, -1})
	{
		if(!IsFreezeAt(TeeX + Dir, Row))
			continue;
		int Run = 1;
		while(Run < 4 && IsFreezeAt(TeeX + Dir * (Run + 1), Row))
			Run++;
		if(Run != 3)
			continue;

		bool High = true;
		for(int Step = 1; Step <= 3 && High; Step++)
			High = IsFreezeAt(TeeX + Dir * Step, Row - 1);
		if(High)
			return Dir;
	}
	return 0;
}

int CWidgetBar::CurrentEdgeJumpDirection(vec2 *pPos, bool *pDouble) const
{
	if(pDouble != nullptr)
		*pDouble = false;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return 0;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return 0;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || !GameClient()->m_Snap.m_aCharacters[LocalId].m_Active)
		return 0;

	const CNetObj_Character *pPrev = &GameClient()->m_Snap.m_aCharacters[LocalId].m_Prev;
	const CNetObj_Character *pCur = &GameClient()->m_Snap.m_aCharacters[LocalId].m_Cur;
	const vec2 Pos = mix(vec2(pPrev->m_X, pPrev->m_Y), vec2(pCur->m_X, pCur->m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
	if(pPos != nullptr)
		*pPos = Pos;

	if(!Collision()->IsOnGround(Pos, CCharacterCore::PhysicalSize()))
		return 0;

	const int Dir = EdgeJumpDirection(Pos);
	if(Dir != 0 && pDouble != nullptr)
	{
		const int Decimal = ((round_to_int(Pos.x / 32.0f * 100.0f) % 100) + 100) % 100;
		const int *pList = Dir > 0 ? s_aEdgeRightDouble : s_aEdgeLeftDouble;
		const int Count = Dir > 0 ? (int)std::size(s_aEdgeRightDouble) : (int)std::size(s_aEdgeLeftDouble);
		*pDouble = ContainsCoordinate(pList, Count, Decimal);
	}
	return Dir;
}

bool CWidgetBar::EdgeJumpInfo(char *pBuf, int BufSize, bool *pOnSpot) const
{
	vec2 Pos;
	const int Dir = CurrentEdgeJumpDirection(&Pos);
	if(Dir == 0)
		return false;

	const int Decimal = ((round_to_int(Pos.x / 32.0f * 100.0f) % 100) + 100) % 100;
	const int *pSpots = Dir > 0 ? s_aEdgeRight : s_aEdgeLeft;
	const int Count = Dir > 0 ? (int)std::size(s_aEdgeRight) : (int)std::size(s_aEdgeLeft);
	const int *pDouble = Dir > 0 ? s_aEdgeRightDouble : s_aEdgeLeftDouble;
	const int DoubleCount = Dir > 0 ? (int)std::size(s_aEdgeRightDouble) : (int)std::size(s_aEdgeLeftDouble);
	const char *pArrow = Dir > 0 ? ">" : "<";

	if(ContainsCoordinate(pSpots, Count, Decimal))
		str_format(pBuf, BufSize, "%s .%02d single", pArrow, Decimal);
	else if(ContainsCoordinate(pDouble, DoubleCount, Decimal))
		str_format(pBuf, BufSize, "%s .%02d double", pArrow, Decimal);
	else
	{
		int Nearest = NearestCoordinate(pSpots, Count, Decimal, -1);
		Nearest = NearestCoordinate(pDouble, DoubleCount, Decimal, Nearest);
		str_format(pBuf, BufSize, "%s .%02d to .%02d", pArrow, Decimal, Nearest);
		*pOnSpot = false;
		return true;
	}
	*pOnSpot = true;
	return true;
}

void CWidgetBar::BuildSegments(std::vector<SSegment> &vLeft, std::vector<SSegment> &vCenter, std::vector<SSegment> &vRight)
{
	const bool InGame = (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK);
	const int Conn = g_Config.m_ClDummy;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const bool HasChar = InGame && LocalId >= 0 && GameClient()->m_Snap.m_aCharacters[LocalId].m_Active;

	const auto &&Add = [&](int Mode, const char *pLabel, const char *pValue, bool Highlight = false) {
		std::vector<SSegment> *pTarget = Mode == 1 ? &vLeft : (Mode == 2 ? &vCenter : (Mode == 3 ? &vRight : nullptr));
		if(pTarget == nullptr)
			return;
		SSegment Segment;
		str_copy(Segment.m_aLabel, pLabel);
		str_copy(Segment.m_aValue, pValue);
		Segment.m_Highlight = Highlight;
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

	if(HasChar && g_Config.m_ClMClientInfoBarEdgeJump != 0)
	{
		bool OnSpot = false;
		if(EdgeJumpInfo(aValue, sizeof(aValue), &OnSpot))
			Add(g_Config.m_ClMClientInfoBarEdgeJump, "Edge", aValue, OnSpot);
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
	TextRender()->TextColor(Segment.m_Highlight ? ColorRGBA(0.42f, 0.87f, 0.45f, 1.0f) : ValueColor);
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
