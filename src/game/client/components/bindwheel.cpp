#include "bindwheel.h"

#include <base/math.h>

#include <engine/external/json-parser/json.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>

static const char *const MCLIENT_FILE = "mclient.json";
static constexpr int LEGACY_PAGE_BINDS = 16;

int CBindWheel::NumSlots()
{
	return std::clamp(g_Config.m_ClMClientBindWheelSlots, MIN_BINDS, MAX_BINDS);
}

int CBindWheel::NumPages()
{
	return std::clamp(g_Config.m_ClMClientBindWheelPages, 1, MAX_PAGES);
}

void CBindWheel::ConKeyBindWheel(IConsole::IResult *pResult, void *pUserData)
{
	CBindWheel *pSelf = static_cast<CBindWheel *>(pUserData);
	if(pSelf->GameClient()->m_Scoreboard.IsActive())
		return;
	const bool Activate = pResult->GetInteger(0) != 0;
	// don't open while the emote wheel is open
	if(Activate && pSelf->GameClient()->m_Emoticon.IsActive())
		return;
	// the wheel always opens on the first page
	if(Activate && !pSelf->m_Active)
		pSelf->m_Page = 0;
	pSelf->m_Active = Activate;
}

void CBindWheel::OnConsoleInit()
{
	m_vBinds.assign(TOTAL_BINDS, CBind());
	LoadBinds();
	Console()->Register("+bindwheel", "", CFGFLAG_CLIENT, ConKeyBindWheel, this, "M-Client: hold to open the bind wheel");
}

void CBindWheel::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_SelectedBind = -1;
	m_Page = 0;
	m_SelectedPage = 0;
	m_SelectorMouse = vec2(0.0f, 0.0f);
	if((int)m_vBinds.size() != TOTAL_BINDS)
		m_vBinds.resize(TOTAL_BINDS);
}

void CBindWheel::OnRelease()
{
	m_Active = false;
}

bool CBindWheel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse += vec2(x, y);
	return true;
}

bool CBindWheel::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
	{
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			const int PageCount = NumPages();
			int Direction = Event.m_Key == KEY_MOUSE_WHEEL_UP ? 1 : -1;
			if(g_Config.m_ClMClientBindWheelScrollInvert)
				Direction = -Direction;
			m_Page = (std::clamp(m_Page, 0, PageCount - 1) + Direction + PageCount) % PageCount;
		}
		return true;
	}

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		m_SelectedBind = -1;
		m_WasActive = false;
		return true;
	}
	return false;
}

void CBindWheel::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!m_Active)
	{
		if(m_WasActive && m_SelectedBind >= 0)
		{
			const int Index = BindIndex(std::clamp(m_SelectedPage, 0, MAX_PAGES - 1), m_SelectedBind);
			if(Index < (int)m_vBinds.size())
			{
				const char *pCommand = m_vBinds[Index].m_aCommand;
				if(pCommand[0] != '\0')
					Console()->ExecuteLine(pCommand, -1);
			}
		}
		m_WasActive = false;
		m_SelectedBind = -1;
		return;
	}

	m_WasActive = true;

	const CUIRect Screen = *Ui()->Screen();
	const vec2 Center = Screen.Center();

	const int SliceCount = NumSlots();
	const int PageCount = NumPages();
	m_Page = std::clamp(m_Page, 0, PageCount - 1);
	m_SelectedPage = m_Page;

	const float SizeScale = g_Config.m_ClMClientBindWheelSize / 100.0f;
	const float RingRadius = 215.0f * SizeScale;
	const float BackgroundRadius = RingRadius + 20.0f;

	const float ButtonWidth = (float)g_Config.m_ClMClientBindWheelBoxWidth;
	const float ButtonHeight = (float)g_Config.m_ClMClientBindWheelBoxHeight;

	if(length(m_SelectorMouse) > RingRadius)
		m_SelectorMouse = normalize(m_SelectorMouse) * RingRadius;

	m_SelectedBind = -1;
	for(int i = 0; i < SliceCount; i++)
	{
		float Angle = 2 * pi * i / (float)SliceCount;
		if(Angle > pi)
			Angle -= 2 * pi;
		const vec2 Off = direction(Angle) * RingRadius;
		if(m_SelectorMouse.x >= Off.x - ButtonWidth / 2.0f && m_SelectorMouse.x <= Off.x + ButtonWidth / 2.0f &&
			m_SelectorMouse.y >= Off.y - ButtonHeight / 2.0f && m_SelectorMouse.y <= Off.y + ButtonHeight / 2.0f)
		{
			m_SelectedBind = i;
			break;
		}
	}

	Ui()->MapScreen();

	const float BackgroundAlpha = g_Config.m_ClMClientBindWheelAlpha / 100.0f;
	const ColorRGBA Highlight = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMClientBindWheelBoxColor, false));

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, BackgroundAlpha);
	Graphics()->DrawCircle(Center.x, Center.y, BackgroundRadius, 64);
	Graphics()->QuadsEnd();

	const float PageNumberSize = (float)g_Config.m_ClMClientBindWheelPageNumberSize;
	if(PageNumberSize > 0.0f)
	{
		char aPage[8];
		str_format(aPage, sizeof(aPage), "%d", m_Page + 1);
		CUIRect PageLabel = {Center.x - PageNumberSize * 1.5f, Center.y - PageNumberSize * 0.65f, PageNumberSize * 3.0f, PageNumberSize * 1.3f};
		const float PageNumberAlpha = g_Config.m_ClMClientBindWheelPageNumberAlpha / 100.0f;
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, PageNumberAlpha);
		TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.35f * PageNumberAlpha);
		Ui()->DoLabel(&PageLabel, aPage, PageNumberSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	}

	const float Rounding = 8.0f;
	const float BoxAlpha = g_Config.m_ClMClientBindWheelBoxAlpha / 100.0f;

	for(int i = 0; i < SliceCount; i++)
	{
		float Angle = 2 * pi * i / (float)SliceCount;
		if(Angle > pi)
			Angle -= 2 * pi;
		const vec2 Pos = Center + direction(Angle) * RingRadius;
		const bool Selected = m_SelectedBind == i;

		CUIRect Button = {Pos.x - ButtonWidth / 2.0f, Pos.y - ButtonHeight / 2.0f, ButtonWidth, ButtonHeight};
		if(Selected)
			Button.Draw(ColorRGBA(Highlight.r, Highlight.g, Highlight.b, BoxAlpha), IGraphics::CORNER_ALL, Rounding);
		else
			Button.Draw(ColorRGBA(0.04f, 0.045f, 0.055f, BoxAlpha), IGraphics::CORNER_ALL, Rounding);

		const CBind &Bind = m_vBinds[BindIndex(m_Page, i)];
		const bool HasEmoji = Bind.m_aEmoji[0] != '\0';
		const char *pName;
		if(Bind.m_aName[0] != '\0')
			pName = Bind.m_aName;
		else if(HasEmoji)
			pName = "";
		else if(Bind.m_aCommand[0] != '\0')
			pName = Bind.m_aCommand;
		else
			pName = "-";
		CUIRect Label = {Button.x + 6.0f, Pos.y - 10.0f, ButtonWidth - 12.0f, 20.0f};
		const bool HasText = pName[0] != '\0' && (!HasEmoji || Label.w >= 44.0f);
		const ColorRGBA TextColor = Selected ? ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f) : ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);

		if(HasEmoji)
		{
			CUIRect IconRect = Label;
			if(HasText)
			{
				IconRect.w = 16.0f;
				Label.x += 18.0f;
				Label.w -= 18.0f;
			}

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
			TextRender()->TextColor(TextColor);
			TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, Selected ? 0.0f : 0.3f);
			Ui()->DoLabel(&IconRect, Bind.m_aEmoji, HasText ? 12.0f : 16.0f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}

		if(!HasText)
			continue;

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_EllipsisAtEnd = true;
		if(Selected)
		{
			static CUIElement s_SelectedLabel;
			if(!s_SelectedLabel.AreRectsInit())
				s_SelectedLabel.Init(Ui(), 1);
			TextRender()->TextColor(0.0f, 0.0f, 0.0f, 1.0f);
			TextRender()->TextOutlineColor(0.0f, 0.0f, 0.0f, 0.0f);
			Ui()->DoLabelStreamed(*s_SelectedLabel.Rect(0), &Label, pName, 14.0f, TEXTALIGN_MC, Props);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		}
		else
		{
			Props.SetColor(ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f));
			Ui()->DoLabel(&Label, pName, 12.0f, TEXTALIGN_MC, Props);
		}
	}

	RenderTools()->RenderCursor(Center + m_SelectorMouse, 24.0f);
}

void CBindWheel::LoadBinds()
{
	m_vBinds.assign(TOTAL_BINDS, CBind());

	void *pFileData;
	unsigned FileSize;
	if(!Storage()->ReadFile(MCLIENT_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pData = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileSize, aError);
	free(pFileData);
	if(pData == nullptr)
		return;

	const auto &&LoadBind = [&](const json_value &Entry, int Index) {
		if(Entry.type != json_object || Index < 0 || Index >= TOTAL_BINDS)
			return;
		const json_value &Name = Entry["name"];
		const json_value &Command = Entry["command"];
		const json_value &Emoji = Entry["emoji"];
		if(Name.type == json_string)
			str_copy(m_vBinds[Index].m_aName, Name);
		if(Command.type == json_string)
			str_copy(m_vBinds[Index].m_aCommand, Command);
		if(Emoji.type == json_string)
			str_copy(m_vBinds[Index].m_aEmoji, Emoji);
	};

	const json_value &Pages = (*pData)["bindwheel_pages"];
	if(Pages.type == json_array)
	{
		for(unsigned Page = 0; Page < Pages.u.array.length && Page < (unsigned)MAX_PAGES; ++Page)
		{
			const json_value &Slices = Pages[Page];
			if(Slices.type != json_array)
				continue;
			for(unsigned i = 0; i < Slices.u.array.length && i < (unsigned)MAX_BINDS; ++i)
				LoadBind(Slices[i], BindIndex(Page, i));
		}
	}
	else
	{
		const json_value &BindWheel = (*pData)["bindwheel"];
		if(BindWheel.type == json_array)
		{
			for(unsigned i = 0; i < BindWheel.u.array.length && i < (unsigned)(LEGACY_PAGE_BINDS * MAX_PAGES); ++i)
				LoadBind(BindWheel[i], BindIndex(i / LEGACY_PAGE_BINDS, i % LEGACY_PAGE_BINDS));
		}
	}
	json_value_free(pData);
}
