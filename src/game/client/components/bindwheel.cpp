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

int CBindWheel::NumSlots()
{
	return std::clamp(g_Config.m_ClMClientBindWheelSlots, MIN_BINDS, MAX_BINDS);
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
	pSelf->m_Active = Activate;
}

void CBindWheel::OnConsoleInit()
{
	m_vBinds.assign(MAX_BINDS, CBind());
	LoadBinds();
	Console()->Register("+bindwheel", "", CFGFLAG_CLIENT, ConKeyBindWheel, this, "M-Client: hold to open the bind wheel");
}

void CBindWheel::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_SelectedBind = -1;
	m_SelectorMouse = vec2(0.0f, 0.0f);
	if((int)m_vBinds.size() != MAX_BINDS)
		m_vBinds.resize(MAX_BINDS);
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
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
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
		if(m_WasActive && m_SelectedBind >= 0 && m_SelectedBind < (int)m_vBinds.size())
		{
			const char *pCommand = m_vBinds[m_SelectedBind].m_aCommand;
			if(pCommand[0] != '\0')
				Console()->ExecuteLine(pCommand, -1);
		}
		m_WasActive = false;
		m_SelectedBind = -1;
		return;
	}

	m_WasActive = true;

	const CUIRect Screen = *Ui()->Screen();
	const vec2 Center = Screen.Center();

	const int SliceCount = NumSlots();

	const float WheelRadius = 170.0f;
	if(length(m_SelectorMouse) > WheelRadius)
		m_SelectorMouse = normalize(m_SelectorMouse) * WheelRadius;

	float SelectedAngle = angle(m_SelectorMouse) + pi / (float)SliceCount;
	if(SelectedAngle < 0)
		SelectedAngle += 2 * pi;

	m_SelectedBind = -1;
	if(length(m_SelectorMouse) > 60.0f)
		m_SelectedBind = (int)(SelectedAngle / (2 * pi) * (float)SliceCount);

	Ui()->MapScreen();

	const float BackgroundAlpha = g_Config.m_ClMClientBindWheelAlpha / 100.0f;

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, BackgroundAlpha);
	Graphics()->DrawCircle(Center.x, Center.y, 190.0f, 64);
	Graphics()->QuadsEnd();

	for(int i = 0; i < SliceCount; i++)
	{
		float Angle = 2 * pi * i / (float)SliceCount;
		if(Angle > pi)
			Angle -= 2 * pi;
		const vec2 Pos = Center + direction(Angle) * 130.0f;
		const bool Selected = m_SelectedBind == i;

		if(Selected)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			const ColorRGBA Accent = CMenus::AccentColor();
			Graphics()->SetColor(Accent.r, Accent.g, Accent.b, 0.85f);
			Graphics()->DrawCircle(Pos.x, Pos.y, 34.0f, 32);
			Graphics()->QuadsEnd();
		}

		const CBind &Bind = m_vBinds[i];
		const char *pName = Bind.m_aName[0] != '\0' ? Bind.m_aName : (Bind.m_aCommand[0] != '\0' ? Bind.m_aCommand : "-");
		CUIRect Label = {Pos.x - 65.0f, Pos.y - 10.0f, 130.0f, 20.0f};
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

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, BackgroundAlpha);
	Graphics()->DrawCircle(Center.x, Center.y, 40.0f, 32);
	Graphics()->QuadsEnd();

	RenderTools()->RenderCursor(Center + m_SelectorMouse, 24.0f);
}

void CBindWheel::LoadBinds()
{
	m_vBinds.assign(MAX_BINDS, CBind());

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

	const json_value &BindWheel = (*pData)["bindwheel"];
	if(BindWheel.type == json_array)
	{
		for(unsigned i = 0; i < BindWheel.u.array.length && i < (unsigned)MAX_BINDS; ++i)
		{
			const json_value &Entry = BindWheel[i];
			if(Entry.type != json_object)
				continue;
			const json_value &Name = Entry["name"];
			const json_value &Command = Entry["command"];
			if(Name.type == json_string)
				str_copy(m_vBinds[i].m_aName, Name);
			if(Command.type == json_string)
				str_copy(m_vBinds[i].m_aCommand, Command);
		}
	}
	json_value_free(pData);
}
