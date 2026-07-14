#include "ads.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/types.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cstdlib>

namespace
{
	constexpr float TITLE_BAR_HEIGHT = 28.0f;
	constexpr float FONT_SIZE = 16.0f;
	constexpr float MAX_WIDTH_FRACTION = 0.8f;
	constexpr float MAX_HEIGHT_FRACTION = 0.8f;
}

void CAds::OnInit()
{
	m_vAds.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, "ads", AdScan, this);
}

int CAds::AdScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	CAds *pSelf = static_cast<CAds *>(pUser);
	if(IsDir || !str_endswith(pName, ".png"))
		return 0;

	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "ads/%s", pName);

	CImageInfo Info;
	if(!pSelf->Graphics()->LoadPng(Info, aPath, DirType))
	{
		log_error("mclient/ads", "Failed to load advertisement '%s'", aPath);
		return 0;
	}

	SAd Ad;
	Ad.m_AspectRatio = Info.m_Height > 0 ? (float)Info.m_Width / (float)Info.m_Height : 1.0f;
	Ad.m_Texture = pSelf->Graphics()->LoadTextureRawMove(Info, 0, aPath);
	pSelf->m_vAds.push_back(Ad);
	return 0;
}

void CAds::OnConsoleInit()
{
	Console()->Register("ad", "", CFGFLAG_CLIENT, ConShowAd, this, "M-Client: show a random advertisement/quote pop-up");
}

void CAds::ConShowAd(IConsole::IResult *pResult, void *pUserData)
{
	CAds *pSelf = static_cast<CAds *>(pUserData);
	if(pSelf->m_Active)
		pSelf->Close();
	else
		pSelf->Show();
}

void CAds::OnReset()
{
	m_Active = false;
	m_CurrentAd = -1;
	m_NextAdTime = 0.0f;
}

void CAds::ScheduleNext()
{
	// random interval
	const float Factor = 0.5f + (rand() % 1001) / 1000.0f;
	m_NextAdTime = LocalTime() + g_Config.m_ClMClientAdsInterval * Factor;
}

void CAds::Show()
{
	if(m_vAds.empty())
		return;
	// rotate through ads
	m_RotationIndex %= (int)m_vAds.size();
	m_CurrentAd = m_RotationIndex;
	m_RotationIndex = (m_RotationIndex + 1) % (int)m_vAds.size();
	m_Active = true;
	// put cursor in the middle of the screen
	const CUIRect Screen = *Ui()->Screen();
	m_MousePos = vec2(Screen.x + Screen.w / 2.0f, Screen.y + Screen.h / 2.0f);
}

void CAds::Close()
{
	m_Active = false;
	m_CurrentAd = -1;
	ScheduleNext();
}

CAds::SLayout CAds::ComputeLayout() const
{
	SLayout Layout;
	const CUIRect Screen = *Ui()->Screen();

	float Aspect = 16.0f / 9.0f;
	if(m_CurrentAd >= 0 && m_CurrentAd < (int)m_vAds.size())
		Aspect = m_vAds[m_CurrentAd].m_AspectRatio;

	// fit image into screen
	const float MaxImageW = Screen.w * MAX_WIDTH_FRACTION;
	const float MaxImageH = Screen.h * MAX_HEIGHT_FRACTION - TITLE_BAR_HEIGHT;
	float ImageW = MaxImageW;
	float ImageH = ImageW / Aspect;
	if(ImageH > MaxImageH)
	{
		ImageH = MaxImageH;
		ImageW = ImageH * Aspect;
	}

	const float WindowW = ImageW;
	const float WindowH = ImageH + TITLE_BAR_HEIGHT;
	Layout.m_Window.x = Screen.x + (Screen.w - WindowW) / 2.0f;
	Layout.m_Window.y = Screen.y + (Screen.h - WindowH) / 2.0f;
	Layout.m_Window.w = WindowW;
	Layout.m_Window.h = WindowH;

	Layout.m_Window.HSplitTop(TITLE_BAR_HEIGHT, &Layout.m_TitleBar, &Layout.m_Image);
	Layout.m_TitleBar.VSplitRight(TITLE_BAR_HEIGHT, nullptr, &Layout.m_CloseButton);
	return Layout;
}

void CAds::OnRender()
{
	const bool Ingame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
	if(!Ingame)
	{
		if(m_Active)
			Close();
		m_NextAdTime = 0.0f;
		return;
	}

	// ads only show while playing not in the menu
	if(!m_Active && g_Config.m_ClMClientAds && !m_vAds.empty() && !GameClient()->m_Menus.IsActive())
	{
		if(m_NextAdTime <= 0.0f)
			ScheduleNext();
		else if(LocalTime() >= m_NextAdTime)
			Show();
	}

	if(!m_Active || m_CurrentAd < 0 || m_CurrentAd >= (int)m_vAds.size())
		return;

	Ui()->MapScreen();
	const CUIRect Screen = *Ui()->Screen();

	// dim background
	Screen.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_NONE, 0.0f);

	const SLayout Layout = ComputeLayout();

	// window frame and title bar
	Layout.m_Window.Draw(ColorRGBA(0.85f, 0.85f, 0.88f, 1.0f), IGraphics::CORNER_T, 8.0f);
	Layout.m_TitleBar.Draw(ColorRGBA(0.12f, 0.16f, 0.30f, 1.0f), IGraphics::CORNER_T, 8.0f);

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// close button
	const bool Hovered = Layout.m_CloseButton.Inside(m_MousePos);
	Layout.m_CloseButton.Draw(Hovered ? ColorRGBA(0.90f, 0.20f, 0.20f, 1.0f) : ColorRGBA(0.70f, 0.12f, 0.12f, 1.0f), IGraphics::CORNER_TR, 8.0f);
	const float XWidth = TextRender()->TextWidth(FONT_SIZE, "X");
	TextRender()->Text(Layout.m_CloseButton.x + (Layout.m_CloseButton.w - XWidth) / 2.0f, Layout.m_CloseButton.y + (TITLE_BAR_HEIGHT - FONT_SIZE) / 2.0f, FONT_SIZE, "X", -1.0f);

	// advertisement image
	Graphics()->TextureSet(m_vAds[m_CurrentAd].m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem QuadItem(Layout.m_Image.x, Layout.m_Image.y, Layout.m_Image.w, Layout.m_Image.h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	// border outline
	Layout.m_Window.DrawOutline(ColorRGBA(0.0f, 0.0f, 0.0f, 0.8f));

	// mouse cursor on top of everything
	RenderTools()->RenderCursor(m_MousePos, 24.0f);
}

bool CAds::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_MousePos += vec2(x, y);

	const CUIRect Screen = *Ui()->Screen();
	m_MousePos.x = std::clamp(m_MousePos.x, Screen.x, Screen.x + Screen.w);
	m_MousePos.y = std::clamp(m_MousePos.y, Screen.y, Screen.y + Screen.h);
	return true;
}

bool CAds::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		if(Event.m_Key == KEY_ESCAPE)
			Close();
		else if(Event.m_Key == KEY_MOUSE_1)
		{
			const SLayout Layout = ComputeLayout();
			if(Layout.m_CloseButton.Inside(m_MousePos))
				Close();
		}
	}
	return true;
}
