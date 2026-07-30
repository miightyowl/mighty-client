#include "practice_setup.h"

#include "menus.h"

#include <base/math.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>

// client refuses to send faster than once per second
static const float COMMAND_INTERVAL = 1.2f;

void CPracticeSetup::ConPracticeSetup(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CPracticeSetup *>(pUserData)->Toggle();
}

void CPracticeSetup::OnConsoleInit()
{
	Console()->Register("practice_setup", "", CFGFLAG_CLIENT, ConPracticeSetup, this, "M-Client: open the fast practice");
}

void CPracticeSetup::OnReset()
{
	m_State = STATE_IDLE;
	m_vSelectedIds.clear();
	m_TeamReady = false;
	m_PracticeTeam = -1;
	m_vPendingCommands.clear();
	m_NextCommandTime = 0.0f;
	m_WaitingForDummy = false;
}

const char *CPracticeSetup::MainTeeName() const
{
	const int MainId = GameClient()->m_aLocalIds[0];
	if(MainId < 0)
		return "";
	return GameClient()->m_aClients[MainId].m_aRealName;
}

void CPracticeSetup::Toggle()
{
	if(IsActive())
	{
		Close();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(m_TeamReady && Client()->DummyConnected())
	{
		m_State = STATE_CONFIRM_REDO;
		return;
	}

	m_State = STATE_SELECT;
}

void CPracticeSetup::Close()
{
	m_State = STATE_IDLE;
}

int CPracticeSetup::PickEmptyTeam() const
{
	bool aUsed[TEAM_SUPER] = {false};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_aClients[i].m_Active)
			continue;
		const int Team = GameClient()->m_Teams.Team(i);
		if(Team > TEAM_FLOCK && Team < TEAM_SUPER)
			aUsed[Team] = true;
	}

	int aFree[TEAM_SUPER];
	int NumFree = 0;
	for(int Team = TEAM_FLOCK + 1; Team < TEAM_SUPER; Team++)
	{
		if(!aUsed[Team])
			aFree[NumFree++] = Team;
	}

	if(NumFree == 0)
		return -1;
	return aFree[secure_rand_below(NumFree)];
}

int CPracticeSetup::CollectPlayers(int *pIds) const
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const int DummyId = GameClient()->m_aLocalIds[1];
	int NumPlayers = 0;
	for(int Pass = 0; Pass < 2; Pass++)
	{
		for(const auto &pInfoByName : GameClient()->m_Snap.m_apInfoByName)
		{
			if(!pInfoByName)
				continue;
			const int ClientId = pInfoByName->m_ClientId;
			if(ClientId == LocalId || ClientId == DummyId)
				continue;
			if(GameClient()->m_aClients[ClientId].m_Friend != (Pass == 0))
				continue;
			pIds[NumPlayers++] = ClientId;
		}
	}
	return NumPlayers;
}

bool CPracticeSetup::IsSelected(int ClientId) const
{
	return std::find(m_vSelectedIds.begin(), m_vSelectedIds.end(), ClientId) != m_vSelectedIds.end();
}

void CPracticeSetup::ToggleSelected(int ClientId)
{
	const auto Existing = std::find(m_vSelectedIds.begin(), m_vSelectedIds.end(), ClientId);
	if(Existing == m_vSelectedIds.end())
		m_vSelectedIds.push_back(ClientId);
	else
		m_vSelectedIds.erase(Existing);
}

void CPracticeSetup::QueueTeleportAndInvites()
{
	char aCommand[128];
	if(MainTeeName()[0] != '\0')
	{
		str_format(aCommand, sizeof(aCommand), "/tp %s", MainTeeName());
		m_vPendingCommands.emplace_back(aCommand);
	}
	for(const int ClientId : m_vSelectedIds)
	{
		str_format(aCommand, sizeof(aCommand), "/invite %s", GameClient()->m_aClients[ClientId].m_aRealName);
		m_vPendingCommands.emplace_back(aCommand);
	}
}

void CPracticeSetup::Start(bool KillExistingDummy)
{
	m_PracticeTeam = PickEmptyTeam();
	if(m_PracticeTeam < 0)
	{
		GameClient()->Echo("Practice setup: no empty team available.");
		Close();
		return;
	}

	m_vPendingCommands.clear();

	if(KillExistingDummy)
		m_vPendingCommands.emplace_back("/kill");

	char aCommand[128];
	str_format(aCommand, sizeof(aCommand), "/team %d", m_PracticeTeam);
	m_vPendingCommands.emplace_back(aCommand);
	m_vPendingCommands.emplace_back("/practice");
	m_vPendingCommands.emplace_back("/lock");
	QueueTeleportAndInvites();

	if(Client()->DummyConnected())
	{
		g_Config.m_ClDummy = 1;
		m_NextCommandTime = LocalTime() + COMMAND_INTERVAL;
	}
	else
	{
		Client()->DummyConnect();
		m_WaitingForDummy = true;
	}

	Close();
}

void CPracticeSetup::RunPendingCommands()
{
	if(m_vPendingCommands.empty())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_vPendingCommands.clear();
		m_WaitingForDummy = false;
		return;
	}

	if(m_WaitingForDummy)
	{
		if(!Client()->DummyConnected() || GameClient()->m_aLocalIds[1] < 0)
			return;
		g_Config.m_ClDummy = 1;
		m_WaitingForDummy = false;
		m_NextCommandTime = LocalTime() + COMMAND_INTERVAL;
		return;
	}

	if(LocalTime() < m_NextCommandTime)
		return;

	if(Client()->DummyConnected())
		g_Config.m_ClDummy = 1;

	GameClient()->m_Chat.SendChat(0, m_vPendingCommands.front().c_str());
	m_vPendingCommands.erase(m_vPendingCommands.begin());
	m_NextCommandTime = LocalTime() + COMMAND_INTERVAL;

	if(m_vPendingCommands.empty())
		m_TeamReady = true;
}

void CPracticeSetup::RenderSelectModal()
{
	const CUIRect Screen = *Ui()->Screen();
	Screen.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_NONE, 0.0f);

	CUIRect Window;
	Screen.Margin((Screen.w - 460.0f) / 2.0f, &Window);
	Window.h = 240.0f;
	Window.y = Screen.y + (Screen.h - Window.h) / 2.0f;
	Window.Draw(ColorRGBA(0.08f, 0.08f, 0.09f, 0.95f), IGraphics::CORNER_ALL, 8.0f);
	Window.Margin(14.0f, &Window);

	CUIRect Title, Hint, Grid, ButtonRow;
	Window.HSplitTop(22.0f, &Title, &Window);
	Ui()->DoLabel(&Title, Localize("Fast practice"), 16.0f, TEXTALIGN_ML);
	Window.HSplitTop(16.0f, &Hint, &Window);
	Ui()->DoLabel(&Hint, Localize("Optionally pick players to invite, then press Start."), 9.0f, TEXTALIGN_ML);
	Window.HSplitTop(6.0f, nullptr, &Window);
	Window.HSplitBottom(24.0f, &Grid, &ButtonRow);

	int aPlayerIds[MAX_CLIENTS];
	const int NumPlayers = CollectPlayers(aPlayerIds);

	const float CellWidth = 60.0f;
	const float CellHeight = 44.0f;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = CellHeight;

	const int PerRow = std::max(1, (int)((Grid.w - ScrollParams.m_ScrollbarThickness) / CellWidth));

	static CScrollRegion s_ScrollRegion;
	s_ScrollRegion.Begin(&Grid, &ScrollParams);

	static char s_aTeeButtonIds[MAX_CLIENTS];
	CUIRect Row = {};
	for(int i = 0; i < NumPlayers; i++)
	{
		if(i % PerRow == 0)
		{
			Grid.HSplitTop(CellHeight, &Row, &Grid);
			s_ScrollRegion.AddRect(Row);
		}

		CUIRect Cell;
		Row.VSplitLeft(CellWidth, &Cell, &Row);

		const int ClientId = aPlayerIds[i];
		if(Ui()->DoButtonLogic(&s_aTeeButtonIds[ClientId], 0, &Cell, BUTTONFLAG_LEFT))
			ToggleSelected(ClientId);

		const bool Selected = IsSelected(ClientId);
		if(Selected)
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.25f), IGraphics::CORNER_ALL, 4.0f);
		else if(Ui()->HotItem() == &s_aTeeButtonIds[ClientId])
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect TeeArea, NameArea;
		Cell.HSplitBottom(11.0f, &TeeArea, &NameArea);

		const float Alpha = 1.0f;
		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[ClientId].m_RenderInfo;
		TeeInfo.m_Size = std::min(TeeArea.h, 32.0f);

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		const vec2 TeeRenderPos(TeeArea.x + TeeArea.w / 2, TeeArea.y + TeeArea.h / 2 + OffsetToMid.y);
		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, Alpha);

		SLabelProperties Props;
		Props.m_MaxWidth = NameArea.w - 2.0f;
		Props.m_EllipsisAtEnd = true;
		if(!Selected)
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.8f);
		Ui()->DoLabel(&NameArea, GameClient()->m_aClients[ClientId].m_aName, 8.0f, TEXTALIGN_MC, Props);
		if(!Selected)
			TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_ScrollRegion.End();

	CUIRect StartButton, CancelButton;
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &StartButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, nullptr, &CancelButton);

	static CButtonContainer s_StartButton;
	static CButtonContainer s_CancelButton;

	StartButton.Draw(Ui()->HotItem() == &s_StartButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&StartButton, Localize("Start"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_StartButton, 0, &StartButton, BUTTONFLAG_LEFT))
	{
		if(Client()->DummyConnected())
			m_State = STATE_CONFIRM_KILL;
		else
			Start(false);
		return;
	}

	CancelButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_CancelButton ? 0.2f : 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&CancelButton, Localize("Cancel"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_CancelButton, 0, &CancelButton, BUTTONFLAG_LEFT))
		Close();
}

void CPracticeSetup::RenderConfirmModal()
{
	const CUIRect Screen = *Ui()->Screen();
	Screen.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f), IGraphics::CORNER_NONE, 0.0f);

	CUIRect Window;
	Screen.Margin((Screen.w - 420.0f) / 2.0f, &Window);
	Window.h = 130.0f;
	Window.y = Screen.y + (Screen.h - Window.h) / 2.0f;
	Window.Draw(ColorRGBA(0.08f, 0.08f, 0.09f, 0.95f), IGraphics::CORNER_ALL, 8.0f);
	Window.Margin(14.0f, &Window);

	CUIRect Title, Text, ButtonRow;
	Window.HSplitTop(22.0f, &Title, &Window);
	Window.HSplitBottom(24.0f, &Text, &ButtonRow);

	const bool Redo = m_State == STATE_CONFIRM_REDO;
	Ui()->DoLabel(&Title, Redo ? Localize("Practice team already set up") : Localize("Use the connected dummy?"), 15.0f, TEXTALIGN_ML);

	SLabelProperties Props;
	Props.m_MaxWidth = Text.w;
	Ui()->DoLabel(&Text, Redo ? Localize("Teleport the dummy to you again and re-invite the selected players?") : Localize("The dummy that is already connected will be killed to start the practice run."), 10.0f, TEXTALIGN_TL, Props);

	CUIRect YesButton, NoButton;
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &YesButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, nullptr, &NoButton);

	static CButtonContainer s_YesButton;
	static CButtonContainer s_NoButton;

	YesButton.Draw(Ui()->HotItem() == &s_YesButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&YesButton, Localize("Yes"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_YesButton, 0, &YesButton, BUTTONFLAG_LEFT))
	{
		if(Redo)
		{
			m_vPendingCommands.clear();
			g_Config.m_ClDummy = 1;
			QueueTeleportAndInvites();
			m_NextCommandTime = LocalTime() + COMMAND_INTERVAL;
			Close();
		}
		else
		{
			Start(true);
		}
		return;
	}

	NoButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_NoButton ? 0.2f : 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&NoButton, Localize("No"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_NoButton, 0, &NoButton, BUTTONFLAG_LEFT))
	{
		m_State = Redo ? STATE_IDLE : STATE_SELECT;
	}
}

void CPracticeSetup::OnRender()
{
	RunPendingCommands();

	if(!IsActive())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Close();
		return;
	}

	if(GameClient()->m_Menus.IsActive() || GameClient()->m_Chat.IsActive() || GameClient()->m_Scoreboard.IsActive())
	{
		Close();
		return;
	}

	Ui()->MapScreen();
	Ui()->StartCheck();
	Ui()->Update();

	if(m_State == STATE_SELECT)
		RenderSelectModal();
	else
		RenderConfirmModal();

	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f * g_Config.m_ClMClientMenuCursorSize / 100.0f);
	Ui()->FinishCheck();
}

bool CPracticeSetup::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive())
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CPracticeSetup::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
	{
		Close();
		return true;
	}

	Ui()->OnInput(Event);

	return true;
}
