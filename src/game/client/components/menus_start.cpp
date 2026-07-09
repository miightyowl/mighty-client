/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#include <algorithm>
#include <unordered_map>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	int NewPage = -1;

	auto MakeRect = [](float x, float y, float w, float h) {
		CUIRect r;
		r.x = x;
		r.y = y;
		r.w = w;
		r.h = h;
		return r;
	};

	auto HoverProgress = [&](const void *pId) -> float {
		static std::unordered_map<const void *, float> s_HoverAnim;
		const float Delta = std::min(Client()->RenderFrameTime(), 0.1f) * 9.0f;
		float &Progress = s_HoverAnim[pId];
		const float Target = Ui()->HotItem() == pId ? 1.0f : 0.0f;
		Progress += std::clamp(Target - Progress, -Delta, Delta);
		return Progress;
	};

	auto Mix = [](const ColorRGBA &A, const ColorRGBA &B, float t) {
		return ColorRGBA(A.r + (B.r - A.r) * t, A.g + (B.g - A.g) * t, A.b + (B.b - A.b) * t, A.a + (B.a - A.a) * t);
	};

	auto Label = [&](const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color, float MaxWidth = -1.0f) {
		SLabelProperties Props;
		Props.m_MaxWidth = MaxWidth;
		Props.m_EllipsisAtEnd = true;
		Props.SetColor(Color);
		CUIRect Local = Rect;
		Ui()->DoLabel(&Local, pText, Size, Align, Props);
	};

	auto IconLabel = [&](const CUIRect &Rect, const char *pIcon, float Size, int Align, ColorRGBA Color) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Label(Rect, pIcon, Size, Align, Color);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	auto StreamLabel = [&](CUIElement::SUIElementRect &RectEl, const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color, float MaxWidth = -1.0f) {
		SLabelProperties Props;
		Props.m_MaxWidth = MaxWidth;
		Props.m_EllipsisAtEnd = true;
		CUIRect Local = Rect;
		TextRender()->TextColor(Color);
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		Ui()->DoLabelStreamed(RectEl, &Local, pText, Size, Align, Props);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	};

	auto StreamIcon = [&](CUIElement::SUIElementRect &RectEl, const CUIRect &Rect, const char *pIcon, float Size, int Align, ColorRGBA Color) {
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		StreamLabel(RectEl, Rect, pIcon, Size, Align, Color);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	auto DividerH = [&](float x, float y, float w) {
		MakeRect(x, y, w, 1.0f).Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_NONE, 0.0f);
	};

	// navigation entry
	auto NavButton = [&](CButtonContainer *pId, const char *pIcon, const char *pLabel, const CUIRect &Rect, bool Primary) -> bool {
		const float Hover = HoverProgress(pId);
		ColorRGBA IconCol, TextCol;
		if(Primary)
		{
			CUIRect Rim;
			Rect.Margin(-1.5f, &Rim);
			Rim.Draw(ColorRGBA(0.13f, 0.28f, 0.10f, 1.0f), IGraphics::CORNER_ALL, 8.5f);
			Rect.Draw(Mix(ColorRGBA(0.33f, 0.71f, 0.24f, 1.0f), ColorRGBA(0.40f, 0.79f, 0.30f, 1.0f), Hover), IGraphics::CORNER_ALL, 7.0f);
			IconCol = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
			TextCol = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
		}
		else
		{
			// bordered card style
			Rect.Draw(Mix(ColorRGBA(0.063f, 0.063f, 0.063f, 1.0f), ColorRGBA(0.10f, 0.10f, 0.10f, 1.0f), Hover), IGraphics::CORNER_ALL, 7.0f);
			Rect.DrawOutline(Mix(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f), Hover));
			IconCol = Mix(ColorRGBA(0.50f, 0.50f, 0.50f, 1.0f), ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f), Hover);
			TextCol = Mix(ColorRGBA(0.92f, 0.92f, 0.92f, 1.0f), ColorRGBA(0.98f, 0.98f, 0.98f, 1.0f), Hover);
		}

		if(Primary)
		{
			static CUIElement s_PrimaryNav;
			if(!s_PrimaryNav.AreRectsInit())
				s_PrimaryNav.Init(Ui(), 3);
			StreamIcon(*s_PrimaryNav.Rect(0), MakeRect(Rect.x + 20.0f, Rect.y, 18.0f, Rect.h), pIcon, 14.0f, TEXTALIGN_MC, IconCol);
			StreamLabel(*s_PrimaryNav.Rect(1), MakeRect(Rect.x + 52.0f, Rect.y, Rect.w - 86.0f, Rect.h), pLabel, 16.0f, TEXTALIGN_ML, TextCol, Rect.w - 86.0f);
			StreamIcon(*s_PrimaryNav.Rect(2), MakeRect(Rect.x + Rect.w - 34.0f + Hover * 3.0f, Rect.y, 18.0f, Rect.h), FontIcon::CHEVRON_RIGHT, 12.0f, TEXTALIGN_MC, TextCol);
		}
		else
		{
			IconLabel(MakeRect(Rect.x + 20.0f, Rect.y, 18.0f, Rect.h), pIcon, 14.0f, TEXTALIGN_MC, IconCol);
			Label(MakeRect(Rect.x + 52.0f, Rect.y, Rect.w - 86.0f, Rect.h), pLabel, 16.0f, TEXTALIGN_ML, TextCol);
		}

		return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT);
	};

	// link cell inside the right panel grid
	auto GridCell = [&](CButtonContainer *pId, const char *pIcon, const char *pLabel, const CUIRect &Cell) -> bool {
		const float Hover = HoverProgress(pId);
		if(Hover > 0.001f)
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f * Hover), IGraphics::CORNER_NONE, 0.0f);
		IconLabel(MakeRect(Cell.x + 18.0f, Cell.y, 14.0f, Cell.h), pIcon, 10.5f, TEXTALIGN_MC, Mix(ColorRGBA(0.55f, 0.55f, 0.55f, 1.0f), ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f), Hover));
		Label(MakeRect(Cell.x + 42.0f, Cell.y, Cell.w - 52.0f, Cell.h), pLabel, 12.0f, TEXTALIGN_ML, Mix(ColorRGBA(0.90f, 0.90f, 0.90f, 1.0f), ColorRGBA(0.98f, 0.98f, 0.98f, 1.0f), Hover));
		return Ui()->DoButtonLogic(pId, 0, &Cell, BUTTONFLAG_LEFT);
	};

	// dark background
	Ui()->Screen()->Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_NONE, 0.0f);

	const float PanelW = std::min(880.0f, MainView.w - 120.0f);
	const float PanelH = 396.0f;
	CUIRect Inner = MakeRect(MainView.x + (MainView.w - PanelW) / 2.0f, MainView.y + (MainView.h - PanelH) / 2.0f, PanelW, PanelH);

	// header
	CUIRect Header, Body;
	Inner.HSplitTop(32.0f, &Header, &Body);
	Body.HSplitTop(24.0f, nullptr, &Body);

	CUIRect Logo = MakeRect(Header.x, Header.y, Header.w, 28.0f);
	const float LogoSize = 22.0f;
	const float MightyW = TextRender()->TextWidth(LogoSize, "mighty");
	Label(MakeRect(Logo.x, Logo.y, MightyW + 4.0f, Logo.h), "mighty", LogoSize, TEXTALIGN_ML, ColorRGBA(0.96f, 0.96f, 0.96f, 1.0f));
	Label(MakeRect(Logo.x + MightyW + 2.0f, Logo.y, Logo.w, Logo.h), "client", LogoSize, TEXTALIGN_ML, ColorRGBA(0.45f, 0.80f, 0.35f, 1.0f));

	char aVersion[32];
	str_format(aVersion, sizeof(aVersion), "v%s", GAME_RELEASE_VERSION);
	Label(MakeRect(Header.x, Header.y, Header.w, 28.0f), aVersion, 9.0f, TEXTALIGN_MR, ColorRGBA(0.45f, 0.45f, 0.45f, 1.0f));

	// running tee
	{
		const float ClientW = TextRender()->TextWidth(LogoSize, "client");
		const float VersionW = TextRender()->TextWidth(9.0f, aVersion);
		const float RunLeft = Logo.x + MightyW + 2.0f + ClientW + 26.0f;
		const float RunRight = Header.x + Header.w - VersionW - 26.0f;
		if(RunRight > RunLeft + 20.0f)
		{
			const float CycleLen = 12.0f;
			const float RunLen = 7.5f;
			const float Now = Client()->LocalTime();
			const float Phase = std::fmod(Now, CycleLen);
			if(Phase < RunLen)
			{
				const int PassIndex = (int)(Now / CycleLen);
				const bool LeftToRight = (PassIndex % 2) == 0;
				const float t = Phase / RunLen;
				const float PosX = LeftToRight ? RunLeft + (RunRight - RunLeft) * t : RunRight - (RunRight - RunLeft) * t;

				float Alpha = 1.0f;
				if(t < 0.2f)
					Alpha = t / 0.2f;
				else if(t > 0.8f)
					Alpha = (1.0f - t) / 0.2f;

				CTeeRenderInfo TeeInfo;
				TeeInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
				TeeInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
				TeeInfo.m_Size = 28.0f;

				const float StepLen = 44.0f;
				float WalkTime = std::fmod(PosX, StepLen) / StepLen;
				if(WalkTime < 0.0f)
					WalkTime += 1.0f;
				CAnimState State;
				State.Set(&g_pData->m_aAnimations[ANIM_BASE], 0.0f);
				State.Add(&g_pData->m_aAnimations[ANIM_WALK], WalkTime, 1.0f);

				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(&State, &TeeInfo, OffsetToMid);
				const vec2 TeePos = vec2(PosX, Header.y + Header.h / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(&State, &TeeInfo, EMOTE_NORMAL, vec2(LeftToRight ? 1.0f : -1.0f, 0.0f), TeePos, Alpha);

				// speech bubble
				static const char *const s_apBubbleMessages[] = {
					"thx for downloading!",
					"hope u enjoy the client!",
					"have fun out there!",
					"gl hf :)",
					"welcome back!",
					"made with love <3",
					"ur the best!",
				};
				const int NumMessages = (int)(sizeof(s_apBubbleMessages) / sizeof(s_apBubbleMessages[0]));
				const int MsgIndex = (int)(((unsigned)PassIndex * 2654435761u) >> 24) % NumMessages;
				const char *pMsg = s_apBubbleMessages[MsgIndex];

				const float BubbleFontSize = 8.0f;
				const float MsgW = TextRender()->TextWidth(BubbleFontSize, pMsg);
				CUIRect Bubble;
				Bubble.w = MsgW + 14.0f;
				Bubble.h = 15.0f;
				Bubble.x = std::clamp(PosX - Bubble.w / 2.0f, Header.x, Header.x + Header.w - Bubble.w);
				Bubble.y = Header.y - Bubble.h - 6.0f;

				if(Bubble.y > 2.0f)
				{
					const ColorRGBA BubbleColor(0.12f, 0.12f, 0.12f, 0.96f * Alpha);
					Bubble.Draw(BubbleColor, IGraphics::CORNER_ALL, 5.0f);
					Bubble.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f * Alpha));

					// tail pointing at the tee
					const float TailX = std::clamp(PosX, Bubble.x + 8.0f, Bubble.x + Bubble.w - 8.0f);
					const float TailTopY = Bubble.y + Bubble.h - 1.0f;
					Graphics()->TextureClear();
					Graphics()->QuadsBegin();
					Graphics()->SetColor(BubbleColor);
					IGraphics::CFreeformItem Tail(TailX - 4.5f, TailTopY, TailX + 4.5f, TailTopY, TailX, TailTopY + 5.0f, TailX, TailTopY + 5.0f);
					Graphics()->QuadsDrawFreeform(&Tail, 1);
					Graphics()->QuadsEnd();

					Label(Bubble, pMsg, BubbleFontSize, TEXTALIGN_MC, ColorRGBA(0.90f, 0.90f, 0.90f, Alpha));
				}
			}
		}
	}

	// body
	CUIRect Content, Footer;
	Body.HSplitBottom(26.0f, &Content, &Footer);
	Content.HSplitBottom(18.0f, &Content, nullptr);

	CUIRect Left, Right;
	Content.VSplitLeft(Content.w * 0.40f, &Left, &Right);
	Right.VSplitLeft(28.0f, nullptr, &Right);

	// left column
	const float NavH = 46.0f;
	const float NavGap = (Left.h - NavH * 5.0f) / 4.0f;
	CUIRect NavRow, NavRest = Left;

	NavRest.HSplitTop(NavH, &NavRow, &NavRest);
	static CButtonContainer s_PlayButton;
	if(NavButton(&s_PlayButton, FontIcon::PLAY, Localize("Play", "Start menu"), NavRow, true) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;

	NavRest.HSplitTop(NavGap, nullptr, &NavRest);
	NavRest.HSplitTop(NavH, &NavRow, &NavRest);
	static CButtonContainer s_DemoButton;
	if(NavButton(&s_DemoButton, FontIcon::FILM, Localize("Demos"), NavRow, false) || CheckHotKey(KEY_D))
		NewPage = CMenus::PAGE_DEMOS;

	NavRest.HSplitTop(NavGap, nullptr, &NavRest);
	NavRest.HSplitTop(NavH, &NavRow, &NavRest);
	static CButtonContainer s_MapEditorButton;
	if(NavButton(&s_MapEditorButton, FontIcon::PEN_TO_SQUARE, Localize("Editor"), NavRow, false) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	NavRest.HSplitTop(NavGap, nullptr, &NavRest);
	NavRest.HSplitTop(NavH, &NavRow, &NavRest);
	static CButtonContainer s_LocalServerButton;
	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(NavButton(&s_LocalServerButton, FontIcon::LAYER_GROUP, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), NavRow, false) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
			GameClient()->m_LocalServer.KillServer();
		else
			GameClient()->m_LocalServer.RunServer({});
	}

	NavRest.HSplitTop(NavGap, nullptr, &NavRest);
	NavRest.HSplitTop(NavH, &NavRow, &NavRest);
	static CButtonContainer s_SettingsButton;
	if(NavButton(&s_SettingsButton, FontIcon::GEAR, Localize("Settings"), NavRow, false) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	// right column
	Right.Draw(ColorRGBA(0.059f, 0.059f, 0.059f, 1.0f), IGraphics::CORNER_ALL, 12.0f);
	Right.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f));

	const float GridRowH = 38.0f;
	CUIRect News, GridArea;
	Right.HSplitBottom(GridRowH * 2.0f, &News, &GridArea);
	DividerH(Right.x, GridArea.y, Right.w);

	CUIRect NewsInner, NewsRow;
	News.Margin(20.0f, &NewsInner);
	NewsInner.HSplitTop(12.0f, &NewsRow, &NewsInner);
	Label(NewsRow, Localize("News"), 9.0f, TEXTALIGN_ML, ColorRGBA(0.50f, 0.50f, 0.50f, 1.0f));
	NewsInner.HSplitTop(8.0f, nullptr, &NewsInner);
	NewsInner.HSplitTop(22.0f, &NewsRow, &NewsInner);
	UpdateLatestRelease();
	const char *pNewsTitle;
	if(m_aReleaseTitle[0] != '\0')
		pNewsTitle = m_aReleaseTitle;
	else if(m_ReleaseLoaded)
		pNewsTitle = Localize("No releases published yet");
	else
		pNewsTitle = Localize("Checking for the latest release…");
	Label(NewsRow, pNewsTitle, 16.5f, TEXTALIGN_ML, ColorRGBA(0.96f, 0.96f, 0.96f, 1.0f), NewsRow.w);
	NewsInner.HSplitTop(7.0f, nullptr, &NewsInner);
	CUIRect NewsDesc;
	NewsInner.HSplitTop(28.0f, &NewsDesc, &NewsInner);
	if(m_aReleaseDesc[0] != '\0')
		Label(NewsDesc, m_aReleaseDesc, 11.0f, TEXTALIGN_TL, ColorRGBA(0.58f, 0.58f, 0.58f, 1.0f), NewsDesc.w);

	static CButtonContainer s_NewsButton;
	const float NewsHover = HoverProgress(&s_NewsButton);
	CUIRect NewsLink;
	NewsInner.HSplitBottom(14.0f, nullptr, &NewsLink);
	const char *pChangelog = Localize("Changelog");
	const ColorRGBA ChangelogCol = Mix(ColorRGBA(0.45f, 0.80f, 0.35f, 1.0f), ColorRGBA(0.55f, 0.90f, 0.42f, 1.0f), NewsHover);
	Label(NewsLink, pChangelog, 10.5f, TEXTALIGN_ML, ChangelogCol);
	const float ChangelogW = TextRender()->TextWidth(10.5f, pChangelog);
	IconLabel(MakeRect(NewsLink.x + ChangelogW + 6.0f + NewsHover * 2.0f, NewsLink.y, 10.0f, NewsLink.h), FontIcon::CHEVRON_RIGHT, 8.0f, TEXTALIGN_ML, ChangelogCol);

	if(Ui()->DoButtonLogic(&s_NewsButton, 0, &News, BUTTONFLAG_LEFT) || CheckHotKey(KEY_N))
		Client()->ViewLink("https://github.com/miightyowl/mighty-client/releases");

	// link grid
	CUIRect GridTop, GridBot;
	GridArea.HSplitMid(&GridTop, &GridBot, 0.0f);
	CUIRect WebsiteBtn, TutorialBtn, LearnBtn, DiscordBtn;
	GridTop.VSplitMid(&WebsiteBtn, &TutorialBtn, 0.0f);
	GridBot.VSplitMid(&LearnBtn, &DiscordBtn, 0.0f);
	DividerH(GridArea.x, GridBot.y, GridArea.w);
	MakeRect(GridArea.x + GridArea.w / 2.0f, GridArea.y + 8.0f, 1.0f, GridArea.h - 16.0f).Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.06f), IGraphics::CORNER_NONE, 0.0f);

	static CButtonContainer s_WebsiteButton;
	if(GridCell(&s_WebsiteButton, FontIcon::EARTH_AMERICAS, Localize("Website"), WebsiteBtn))
		Client()->ViewLink("https://ddnet.org/");

	static CButtonContainer s_TutorialButton;
	if(GridCell(&s_TutorialButton, FontIcon::FLAG_CHECKERED, Localize("Tutorial"), TutorialBtn))
		GameClient()->m_Menus.JoinTutorial();

	static CButtonContainer s_LearnButton;
	if(GridCell(&s_LearnButton, FontIcon::INFO, Localize("Learn"), LearnBtn))
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));

	static CButtonContainer s_DiscordButton;
	if(GridCell(&s_DiscordButton, FontIcon::COMMENT, Localize("Discord"), DiscordBtn))
		Client()->ViewLink(Localize("https://ddnet.org/discord"));

	// footer
	DividerH(Inner.x, Footer.y - 9.0f, Inner.w);

	CUIRect FooterLeft, ConsoleBtn;
	Footer.VSplitRight(100.0f, &FooterLeft, &ConsoleBtn);
	CUIRect QuitBtn, UpdateArea;
	FooterLeft.VSplitLeft(95.0f, &QuitBtn, &UpdateArea);
	UpdateArea.VSplitLeft(12.0f, nullptr, &UpdateArea);

	static CButtonContainer s_QuitButton;
	const float QuitHover = HoverProgress(&s_QuitButton);
	const ColorRGBA QuitCol = Mix(ColorRGBA(0.50f, 0.50f, 0.50f, 1.0f), ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f), QuitHover);
	IconLabel(MakeRect(QuitBtn.x, QuitBtn.y, 14.0f, QuitBtn.h), FontIcon::RIGHT_FROM_BRACKET, 11.0f, TEXTALIGN_MC, QuitCol);
	Label(MakeRect(QuitBtn.x + 22.0f, QuitBtn.y, QuitBtn.w - 22.0f, QuitBtn.h), Localize("Quit"), 12.0f, TEXTALIGN_ML, QuitCol);
	bool UsedEscape = false;
	if(Ui()->DoButtonLogic(&s_QuitButton, 0, &QuitBtn, BUTTONFLAG_LEFT) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
			GameClient()->m_Menus.ShowQuitPopup();
		else
			Client()->Quit();
	}

	static CButtonContainer s_ConsoleButton;
	const float ConsoleHover = HoverProgress(&s_ConsoleButton);
	const ColorRGBA ConsoleCol = Mix(ColorRGBA(0.50f, 0.50f, 0.50f, 1.0f), ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f), ConsoleHover);
	const char *pConsole = Localize("Console");
	IconLabel(MakeRect(ConsoleBtn.x + ConsoleBtn.w - TextRender()->TextWidth(12.0f, pConsole) - 22.0f, ConsoleBtn.y, 14.0f, ConsoleBtn.h), FontIcon::TERMINAL, 11.0f, TEXTALIGN_MC, ConsoleCol);
	Label(ConsoleBtn, pConsole, 12.0f, TEXTALIGN_MR, ConsoleCol);
	if(Ui()->DoButtonLogic(&s_ConsoleButton, 0, &ConsoleBtn, BUTTONFLAG_LEFT))
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);

	// update notification
#if defined(CONF_AUTOUPDATE)
	CUIRect UpdateButton;
	UpdateArea.VSplitRight(90.0f, &UpdateArea, &UpdateButton);
	UpdateArea.VSplitRight(8.0f, &UpdateArea, nullptr);
	UpdateButton.HMargin(3.0f, &UpdateButton);

	char aBuf[128];
	const IUpdater::EUpdaterState State = Updater()->GetCurrentState();
	const bool NeedUpdate = str_comp(Client()->LatestVersion(), "0");

	if(State == IUpdater::CLEAN && NeedUpdate)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Update now"), 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 6.0f, 0.0f, ColorRGBA(0.33f, 0.71f, 0.24f, 0.7f)))
			Updater()->InitiateUpdate();
	}
	else if(State == IUpdater::NEED_RESTART)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Restart"), 0, &UpdateButton, BUTTONFLAG_LEFT, 0, IGraphics::CORNER_ALL, 6.0f, 0.0f, ColorRGBA(0.33f, 0.71f, 0.24f, 0.7f)))
			Client()->Restart();
	}
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		Ui()->RenderProgressBar(UpdateButton, Updater()->GetCurrentPercent() / 100.0f);
	}

	if(State == IUpdater::CLEAN && NeedUpdate)
		str_format(aBuf, sizeof(aBuf), Localize("DDNet %s is out!"), Client()->LatestVersion());
	else if(State == IUpdater::CLEAN)
		aBuf[0] = '\0';
	else if(State >= IUpdater::GETTING_MANIFEST && State < IUpdater::NEED_RESTART)
	{
		char aCurrentFile[64];
		Updater()->GetCurrentFile(aCurrentFile, sizeof(aCurrentFile));
		str_format(aBuf, sizeof(aBuf), Localize("Downloading %s:"), aCurrentFile);
	}
	else if(State == IUpdater::FAIL)
		str_copy(aBuf, Localize("Update failed! Check log…"));
	else if(State == IUpdater::NEED_RESTART)
		str_copy(aBuf, Localize("DDNet Client updated!"));
	Label(UpdateArea, aBuf, 10.0f, TEXTALIGN_MR, ColorRGBA(0.86f, 0.42f, 0.36f, 1.0f));
#elif defined(CONF_INFORM_UPDATE)
	if(str_comp(Client()->LatestVersion(), "0") != 0)
	{
		CUIRect DownloadButton;
		UpdateArea.VSplitRight(90.0f, &UpdateArea, &DownloadButton);
		UpdateArea.VSplitRight(8.0f, &UpdateArea, nullptr);
		DownloadButton.HMargin(3.0f, &DownloadButton);

		static CButtonContainer s_DownloadButton;
		if(GameClient()->m_Menus.DoButton_Menu(&s_DownloadButton, Localize("Download"), 0, &DownloadButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 6.0f, 0.0f, ColorRGBA(0.33f, 0.71f, 0.24f, 0.7f)))
			Client()->ViewLink("https://ddnet.org/downloads/");

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("DDNet %s is out!"), Client()->LatestVersion());
		Label(UpdateArea, aBuf, 10.0f, TEXTALIGN_MR, ColorRGBA(0.86f, 0.42f, 0.36f, 1.0f));
	}
#endif

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}

void CMenusStart::UpdateLatestRelease()
{
	if(!m_ReleaseRequested)
	{
		m_ReleaseRequested = true;
		m_pReleaseTask = HttpGet("https://api.github.com/repos/miightyowl/mighty-client/releases/latest");
		m_pReleaseTask->Timeout(CTimeout{10000, 0, 500, 10});
		m_pReleaseTask->HeaderString("Accept", "application/vnd.github+json");
		Http()->Run(m_pReleaseTask);
	}

	if(m_ReleaseLoaded || m_pReleaseTask == nullptr)
		return;

	const EHttpState State = m_pReleaseTask->State();
	if(State == EHttpState::QUEUED || State == EHttpState::RUNNING)
		return;

	if(State == EHttpState::DONE)
	{
		json_value *pJson = m_pReleaseTask->ResultJson();
		if(pJson != nullptr)
		{
			const json_value &Release = *pJson;

			const json_value &Name = Release["name"];
			const json_value &Tag = Release["tag_name"];
			if(Name.type == json_string && ((const char *)Name)[0] != '\0')
				str_copy(m_aReleaseTitle, Name);
			else if(Tag.type == json_string)
				str_copy(m_aReleaseTitle, Tag);

			const json_value &Body = Release["body"];
			if(Body.type == json_string)
			{
				char aFirstLine[256];
				str_copy(aFirstLine, Body);
				for(char *pCur = aFirstLine; *pCur != '\0'; ++pCur)
				{
					if(*pCur == '\r' || *pCur == '\n')
					{
						*pCur = '\0';
						break;
					}
				}
				const char *pStart = str_skip_whitespaces(aFirstLine);
				while(*pStart == '#' || *pStart == '*' || *pStart == '-' || *pStart == ' ')
					++pStart;
				str_copy(m_aReleaseDesc, pStart);
			}
		}
		json_value_free(pJson);
	}

	m_ReleaseLoaded = true;
	m_pReleaseTask = nullptr;
}
