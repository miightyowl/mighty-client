#include "tictactoe.h"

#include "binds.h"
#include "menus.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>

namespace
{
	const char *PROTOCOL_PREFIX = "MTTT1 ";
	const char *VIEW_BIND = "+tictactoe";

	const float SEND_INTERVAL = 1.2f;
	const float CHAT_SCORE_PENALTY = 250.0f;
	const float CHAT_SCORE_DECAY = 50.0f;
	const float CHAT_SCORE_BUDGET = 750.0f;
	const float ACK_DELAY = 2.5f;
	const float RETRY_INTERVAL = 8.0f;
	const int MAX_RETRIES = 3;
	const int MAX_CHALLENGE_RETRIES = 1;
	const float ACCEPT_TIMEOUT = 60.0f;
	const float RESULT_HOLD = 5.0f;
	const float RESULT_FADE = 2.0f;

	const ColorRGBA COLOR_MARK_OTHER = ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f);
	const ColorRGBA COLOR_OUTLINE = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);

	const int g_aaWinLines[8][3] = {
		{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // rows
		{0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // columns
		{0, 4, 8}, {2, 4, 6}}; // diagonals
}

void CTicTacToe::ConTicTacToe(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CTicTacToe *>(pUserData)->Toggle();
}

void CTicTacToe::ConKeyTicTacToe(IConsole::IResult *pResult, void *pUserData)
{
	CTicTacToe *pSelf = static_cast<CTicTacToe *>(pUserData);

	if(pResult->GetInteger(0) == 0)
	{
		if(pSelf->m_State == STATE_SELECT)
			pSelf->Close();
		pSelf->m_ViewActive = false;
		pSelf->m_CursorActive = false;
		pSelf->m_IgnoreClick = false;
		pSelf->m_KeyBlocked = false;
		return;
	}

	pSelf->m_ViewActive = true;
	if(pSelf->m_State == STATE_IDLE && !pSelf->m_KeyBlocked)
		pSelf->OpenSelect();
}

void CTicTacToe::OnConsoleInit()
{
	Console()->Register("tictactoe", "", CFGFLAG_CLIENT, ConTicTacToe, this, "M-Client: challenge another M-Client player to tic tac toe");
	Console()->Register("+tictactoe", "", CFGFLAG_CLIENT, ConKeyTicTacToe, this, "M-Client: open the tic tac toe player selection, hold to show a running game");
}

void CTicTacToe::OnReset()
{
	m_State = STATE_IDLE;
	m_OpponentId = -1;
	m_LocalId = -1;
	m_SelectedId = -1;
	str_copy(m_aBoard, ".........");
	m_MoveCount = 0;
	m_MyMark = 'X';
	m_aWinLine[0] = m_aWinLine[1] = m_aWinLine[2] = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	m_HasBoard = false;
	m_FadeTime = 0.0f;
	m_ViewActive = false;
	m_CursorActive = false;
	m_IgnoreClick = false;
	m_KeyBlocked = false;
	m_ShowKeyHint = false;
	m_AcceptDeadline = 0.0f;
	m_PendingAck = -1;
	m_ChatScore = 0.0f;
	m_ChatScoreTime = LocalTime();
	m_vSendQueue.clear();
	m_NextSendTime = 0.0f;
	ClearRetry();
}

bool CTicTacToe::WhisperSupported() const
{
	return GameClient()->m_Chat.ServerHasCommand("w");
}

bool CTicTacToe::WindowVisible() const
{
	return !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive() && !GameClient()->m_Scoreboard.IsActive();
}

// -1 when no key is bound, 0 when it is up and 1 while it is held down
int CTicTacToe::ViewKeyState() const
{
	int State = -1;
	for(int Modifier = KeyModifier::NONE; Modifier < KeyModifier::COMBINATION_COUNT; Modifier++)
	{
		for(int Key = KEY_FIRST; Key < KEY_LAST; Key++)
		{
			const char *pBind = GameClient()->m_Binds.Get(Key, Modifier);
			if(!pBind[0] || str_comp(pBind, VIEW_BIND) != 0)
				continue;
			if(Input()->KeyIsPressed(Key))
				return 1;
			State = 0;
		}
	}
	return State;
}

bool CTicTacToe::ViewShown() const
{
	if(m_State == STATE_INVITED || m_State == STATE_PLAYING)
		return m_ViewActive;
	return m_ViewActive && m_State == STATE_OVER && m_HasBoard;
}

bool CTicTacToe::MyTurn() const
{
	return m_State == STATE_PLAYING && (m_MoveCount % 2 == 0) == (m_MyMark == 'X');
}

void CTicTacToe::StatusText(char *pBuf, size_t Size) const
{
	switch(m_State)
	{
	case STATE_CALLING:
		str_format(pBuf, Size, Localize("Waiting for %s..."), OpponentName());
		break;
	case STATE_RINGING:
		str_format(pBuf, Size, Localize("%s is deciding..."), OpponentName());
		break;
	case STATE_INVITED:
		str_format(pBuf, Size, Localize("%s challenged you"), OpponentName());
		break;
	case STATE_PLAYING:
		if(MyTurn())
			str_format(pBuf, Size, Localize("Your turn, you play %c"), m_MyMark);
		else
			str_format(pBuf, Size, Localize("%s is thinking..."), OpponentName());
		break;
	case STATE_OVER:
		str_copy(pBuf, m_aStatus, Size);
		break;
	default:
		pBuf[0] = '\0';
		break;
	}
}

const char *CTicTacToe::OpponentName() const
{
	if(m_OpponentId < 0 || m_OpponentId >= MAX_CLIENTS)
		return "";
	return GameClient()->m_aClients[m_OpponentId].m_aName;
}

void CTicTacToe::Toggle()
{
	if(IsActive())
		Close();
	else
		OpenSelect();
}

void CTicTacToe::OpenSelect()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!WhisperSupported())
	{
		GameClient()->Echo("Tic tac toe: this server does not support whispers.");
		return;
	}

	m_SelectedId = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	m_State = STATE_SELECT;
}

void CTicTacToe::Close()
{
	if(m_State == STATE_PLAYING || m_State == STATE_CALLING || m_State == STATE_RINGING)
		SendTo(m_OpponentId, "Q");
	else if(m_State == STATE_INVITED)
		SendTo(m_OpponentId, "D");

	m_KeyBlocked = m_ViewActive;
	m_State = STATE_IDLE;
	m_OpponentId = -1;
	m_LocalId = -1;
	m_SelectedId = -1;
	m_HasBoard = false;
	m_CursorActive = false;
	m_IgnoreClick = false;
	m_PendingAck = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	ClearRetry();
}

void CTicTacToe::Finish(char Result, const char *pStatus)
{
	m_Result = Result;
	str_copy(m_aStatus, pStatus);
	m_State = STATE_OVER;
	m_FadeTime = LocalTime() + RESULT_HOLD;
	ClearRetry();
}

void CTicTacToe::Challenge(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return;

	m_OpponentId = ClientId;
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	m_MyMark = 'X';
	str_copy(m_aBoard, ".........");
	m_MoveCount = 0;
	m_aWinLine[0] = m_aWinLine[1] = m_aWinLine[2] = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	m_HasBoard = false;
	m_State = STATE_CALLING;
	SendProtocol("C", MAX_CHALLENGE_RETRIES);
}

void CTicTacToe::StartGame(int OpponentId, bool Challenger)
{
	m_OpponentId = OpponentId;
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	m_MyMark = Challenger ? 'X' : 'O';
	str_copy(m_aBoard, ".........");
	m_MoveCount = 0;
	m_aWinLine[0] = m_aWinLine[1] = m_aWinLine[2] = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	m_HasBoard = true;
	m_State = STATE_PLAYING;
	ClearRetry();
}

void CTicTacToe::SendTo(int ClientId, const char *pMessage)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return;

	char aName[2 * MAX_NAME_LENGTH];
	const char *pName = GameClient()->m_aClients[ClientId].m_aRealName;
	size_t Out = 0;
	for(size_t i = 0; pName[i] != '\0' && Out + 2 < sizeof(aName); i++)
	{
		if(pName[i] == '"' || pName[i] == '\\')
			aName[Out++] = '\\';
		aName[Out++] = pName[i];
	}
	aName[Out] = '\0';

	char aLine[256];
	str_format(aLine, sizeof(aLine), "/w \"%s\" %s%s", aName, PROTOCOL_PREFIX, pMessage);
	m_vSendQueue.emplace_back(aLine);
}

void CTicTacToe::SendProtocol(const char *pMessage, int MaxRetries)
{
	SendTo(m_OpponentId, pMessage);
	m_RetryMessage = pMessage;
	m_RetryTime = LocalTime() + RETRY_INTERVAL;
	m_RetryCount = 0;
	m_RetryMax = MaxRetries;
}

void CTicTacToe::ClearRetry()
{
	m_RetryMessage.clear();
	m_RetryTime = 0.0f;
	m_RetryCount = 0;
	m_RetryMax = MAX_RETRIES;
}

void CTicTacToe::FlushSendQueue()
{
	const float Now = LocalTime();
	m_ChatScore = std::max(0.0f, m_ChatScore - (Now - m_ChatScoreTime) * CHAT_SCORE_DECAY);
	m_ChatScoreTime = Now;

	if(m_vSendQueue.empty())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		m_vSendQueue.clear();
		return;
	}

	if(Now < m_NextSendTime)
		return;

	if(m_ChatScore + CHAT_SCORE_PENALTY > CHAT_SCORE_BUDGET)
		return;

	GameClient()->m_Chat.SendChat(0, m_vSendQueue.front().c_str());
	m_vSendQueue.erase(m_vSendQueue.begin());
	m_ChatScore += CHAT_SCORE_PENALTY;
	m_NextSendTime = Now + SEND_INTERVAL;
}

void CTicTacToe::UpdatePendingAck()
{
	if(m_PendingAck < 0 || LocalTime() < m_PendingAckTime)
		return;

	SendAck(m_OpponentId, m_PendingAck);
	m_PendingAck = -1;
}

void CTicTacToe::UpdateRetry()
{
	if(m_RetryMessage.empty() || LocalTime() < m_RetryTime)
		return;

	m_RetryCount++;
	if(m_RetryCount > m_RetryMax)
	{
		if(m_State == STATE_CALLING)
			Finish(0, "No answer. The player is probably not using M-Client.");
		else if(m_State == STATE_PLAYING)
			Finish(0, "Your opponent is not responding.");
		else
			ClearRetry();
		return;
	}

	SendTo(m_OpponentId, m_RetryMessage.c_str());
	m_RetryTime = LocalTime() + RETRY_INTERVAL;
}

void CTicTacToe::SendState()
{
	char aMessage[32];
	str_format(aMessage, sizeof(aMessage), "S %d %s", m_MoveCount, m_aBoard);
	m_PendingAck = -1;
	SendProtocol(aMessage, MAX_RETRIES);
}

void CTicTacToe::SendAck(int ClientId, int Moves)
{
	char aMessage[32];
	str_format(aMessage, sizeof(aMessage), "K %d", Moves);
	SendTo(ClientId, aMessage);
}

bool CTicTacToe::ApplyState(const char *pArgs)
{
	char aMoves[8];
	const char *pRest = str_next_token(pArgs, " ", aMoves, sizeof(aMoves));
	if(!pRest)
		return false;

	char aBoard[16];
	str_next_token(pRest, " ", aBoard, sizeof(aBoard));
	if(str_length(aBoard) != 9)
		return false;
	for(int i = 0; i < 9; i++)
	{
		if(aBoard[i] != '.' && aBoard[i] != 'X' && aBoard[i] != 'O')
			return false;
	}

	int Moves;
	if(!str_toint(aMoves, &Moves))
		return false;

	if(Moves == m_MoveCount)
	{
		if(str_comp(aBoard, m_aBoard) != 0)
			return false;
		SendAck(m_OpponentId, Moves);
		return true;
	}
	if(Moves < m_MoveCount)
	{
		SendState();
		return true;
	}
	if(Moves != m_MoveCount + 1)
		return false;

	const char OpponentMark = m_MyMark == 'X' ? 'O' : 'X';
	int Cell = -1;
	for(int i = 0; i < 9; i++)
	{
		if(aBoard[i] == m_aBoard[i])
			continue;
		if(Cell >= 0 || m_aBoard[i] != '.' || aBoard[i] != OpponentMark)
			return false;
		Cell = i;
	}
	if(Cell < 0)
		return false;

	m_aBoard[Cell] = OpponentMark;
	m_MoveCount = Moves;
	ClearRetry();
	if(CheckGameEnd())
	{
		SendAck(m_OpponentId, Moves);
	}
	else
	{
		m_PendingAck = Moves;
		m_PendingAckTime = LocalTime() + ACK_DELAY;
	}
	return true;
}

void CTicTacToe::PlayCell(int Cell)
{
	if(m_State != STATE_PLAYING || m_aBoard[Cell] != '.')
		return;
	if(!MyTurn())
		return;

	m_aBoard[Cell] = m_MyMark;
	m_MoveCount++;
	CheckGameEnd();
	SendState();
}

bool CTicTacToe::CheckGameEnd()
{
	for(const auto &aLine : g_aaWinLines)
	{
		const char Mark = m_aBoard[aLine[0]];
		if(Mark == '.' || m_aBoard[aLine[1]] != Mark || m_aBoard[aLine[2]] != Mark)
			continue;

		for(int i = 0; i < 3; i++)
			m_aWinLine[i] = aLine[i];

		char aStatus[128];
		if(Mark == m_MyMark)
			str_copy(aStatus, "You won!");
		else
			str_format(aStatus, sizeof(aStatus), "%s won.", OpponentName());
		Finish(Mark == m_MyMark ? 'W' : 'L', aStatus);
		return true;
	}

	if(m_MoveCount >= 9)
	{
		Finish('D', "Draw.");
		return true;
	}
	return false;
}

void CTicTacToe::OnChatMessage(int ClientId, const char *pMessage)
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId >= 0 && ClientId == LocalId)
	{
		m_ChatScore += CHAT_SCORE_PENALTY;
		return;
	}

	if(ClientId >= 0 || m_State == STATE_IDLE || m_State == STATE_SELECT || LocalId < 0)
		return;

	const bool Muted = str_find(pMessage, "You are not permitted to talk") != nullptr ||
			   (str_find(pMessage, "has been muted") != nullptr && str_find(pMessage, GameClient()->m_aClients[LocalId].m_aRealName) != nullptr);
	if(!Muted)
		return;

	m_vSendQueue.clear();
	m_PendingAck = -1;
	Finish(0, "Stopped, the server muted you.");
}

bool CTicTacToe::OnWhisper(int ClientId, int Team, const char *pMessage)
{
	const char *pArgs = str_startswith(pMessage, PROTOCOL_PREFIX);
	if(!pArgs)
		return false;
	if(Team == TEAM_WHISPER_SEND)
		return true;
	if(Team != TEAM_WHISPER_RECV || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return true;
	if(Client()->State() != IClient::STATE_ONLINE)
		return true;

	const char Verb = pArgs[0];
	const char *pRest = pArgs + 1;
	while(*pRest == ' ')
		pRest++;

	const bool FromOpponent = ClientId == m_OpponentId;

	switch(Verb)
	{
	case 'C':
	{
		const bool Busy = m_State == STATE_CALLING || m_State == STATE_RINGING || m_State == STATE_INVITED || m_State == STATE_PLAYING;
		const bool Local = ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
		if(FromOpponent && m_State == STATE_INVITED)
		{
			SendTo(ClientId, "R");
		}
		else if(!g_Config.m_ClMClientTicTacToe || Busy || Local)
		{
			SendTo(ClientId, "D");
		}
		else
		{
			ClearRetry();
			m_OpponentId = ClientId;
			m_LocalId = GameClient()->m_Snap.m_LocalClientId;
			m_MyMark = 'O';
			m_HasBoard = false;
			m_Result = 0;
			m_aStatus[0] = '\0';
			m_State = STATE_INVITED;
			m_AcceptDeadline = LocalTime() + ACCEPT_TIMEOUT;
			SendTo(ClientId, "R");
		}
		break;
	}

	case 'R':
		if(FromOpponent && m_State == STATE_CALLING)
		{
			m_State = STATE_RINGING;
			m_AcceptDeadline = LocalTime() + ACCEPT_TIMEOUT;
			ClearRetry();
		}
		break;

	case 'A':
		if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING))
		{
			StartGame(ClientId, true);
			SendAck(ClientId, 0);
		}
		else if(FromOpponent && m_State == STATE_PLAYING && m_MoveCount == 0)
		{
			SendAck(ClientId, 0);
		}
		break;

	case 'D':
		if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING))
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s declined the challenge.", OpponentName());
			Finish(0, aStatus);
		}
		break;

	case 'S':
		if(FromOpponent && m_State == STATE_PLAYING && !ApplyState(pRest))
		{
			SendTo(m_OpponentId, "Q");
			Finish(0, "The game got out of sync.");
		}
		break;

	case 'K':
	{
		int Moves;
		if(FromOpponent && str_toint(pRest, &Moves) && Moves == m_MoveCount)
			ClearRetry();
		break;
	}

	case 'Q':
		if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING || m_State == STATE_INVITED || m_State == STATE_PLAYING))
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s left the game.", OpponentName());
			Finish(0, aStatus);
		}
		break;

	default:
		break;
	}

	return true;
}

int CTicTacToe::CollectPlayers(int *pIds) const
{
	const int LocalId = GameClient()->m_aLocalIds[0];
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

CUIRect CTicTacToe::OpenWindow(float Width, float Height) const
{
	const CUIRect Screen = *Ui()->Screen();

	CUIRect Window;
	Screen.Margin((Screen.w - Width) / 2.0f, &Window);
	Window.h = Height;
	Window.y = Screen.y + (Screen.h - Window.h) / 2.0f;
	Window.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 8.0f);
	Window.Margin(14.0f, &Window);
	return Window;
}

void CTicTacToe::RenderSelectModal()
{
	CUIRect Window = OpenWindow(460.0f, 240.0f);

	CUIRect Title, Hint, Grid, ButtonRow;
	Window.HSplitTop(22.0f, &Title, &Window);
	Ui()->DoLabel(&Title, Localize("Tic tac toe"), 16.0f, TEXTALIGN_ML);
	Window.HSplitTop(16.0f, &Hint, &Window);
	Ui()->DoLabel(&Hint, Localize("Pick the player you want to challenge. Only players using M-Client can play."), 9.0f, TEXTALIGN_ML);
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
			m_SelectedId = m_SelectedId == ClientId ? -1 : ClientId;

		const bool Selected = m_SelectedId == ClientId;
		if(Selected)
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.25f), IGraphics::CORNER_ALL, 4.0f);
		else if(Ui()->HotItem() == &s_aTeeButtonIds[ClientId])
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect TeeArea, NameArea;
		Cell.HSplitBottom(11.0f, &TeeArea, &NameArea);

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[ClientId].m_RenderInfo;
		TeeInfo.m_Size = std::min(TeeArea.h, 32.0f);

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		const vec2 TeeRenderPos(TeeArea.x + TeeArea.w / 2, TeeArea.y + TeeArea.h / 2 + OffsetToMid.y);
		RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, 1.0f);

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

	CUIRect ChallengeButton, CancelButton;
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &ChallengeButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, nullptr, &CancelButton);

	static CButtonContainer s_ChallengeButton;
	static CButtonContainer s_CancelButton;

	const bool CanChallenge = m_SelectedId >= 0;
	ChallengeButton.Draw(CanChallenge ? (Ui()->HotItem() == &s_ChallengeButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&ChallengeButton, Localize("Challenge"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_ChallengeButton, 0, &ChallengeButton, BUTTONFLAG_LEFT) && CanChallenge)
	{
		Challenge(m_SelectedId);
		return;
	}

	CancelButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_CancelButton ? 0.2f : 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&CancelButton, Localize("Cancel"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_CancelButton, 0, &CancelButton, BUTTONFLAG_LEFT))
		Close();
}

void CTicTacToe::RenderStatusBar(float Alpha)
{
	const CUIRect Screen = *Ui()->Screen();

	CUIRect Bar;
	Bar.x = Screen.x;
	Bar.w = 132.0f;
	Bar.h = m_ShowKeyHint ? 46.0f : 36.0f;
	Bar.y = Screen.y + (Screen.h - Bar.h) / 2.0f;
	Bar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha), IGraphics::CORNER_R, 5.0f);

	CUIRect Content;
	Bar.Margin(5.0f, &Content);

	SLabelProperties Props;
	Props.m_MaxWidth = Content.w;
	Props.m_EllipsisAtEnd = true;

	CUIRect Line;
	Content.HSplitTop(10.0f, &Line, &Content);
	TextRender()->TextColor(CMenus::AccentColor().WithAlpha(Alpha));
	TextRender()->TextOutlineColor(COLOR_OUTLINE.WithAlpha(Alpha));
	Ui()->DoLabel(&Line, Localize("Tic tac toe"), 8.0f, TEXTALIGN_ML, Props);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());

	char aStatus[128];
	StatusText(aStatus, sizeof(aStatus));
	Content.HSplitTop(13.0f, &Line, &Content);
	if(m_Result == 'W')
		TextRender()->TextColor(ColorRGBA(0.4f, 1.0f, 0.4f, Alpha));
	else if(m_Result == 'L')
		TextRender()->TextColor(ColorRGBA(1.0f, 0.5f, 0.5f, Alpha));
	else if(m_State == STATE_PLAYING && MyTurn())
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	else
		TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, Alpha));
	Ui()->DoLabel(&Line, aStatus, 8.0f, TEXTALIGN_ML, Props);

	if(m_ShowKeyHint)
	{
		char aKey[64];
		GameClient()->m_Binds.GetKey(VIEW_BIND, aKey, sizeof(aKey));
		char aHint[128];
		if(aKey[0] == '\0')
			str_copy(aHint, Localize("Bind a key in the controls"));
		else
			str_format(aHint, sizeof(aHint), Localize("Hold %s to view"), aKey);

		Content.HSplitTop(10.0f, &Line, &Content);
		TextRender()->TextColor(ColorRGBA(0.6f, 0.6f, 0.6f, Alpha));
		Ui()->DoLabel(&Line, aHint, 7.0f, TEXTALIGN_ML, Props);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CTicTacToe::RenderView(bool Interactive, float Alpha)
{
	const bool ShowBoard = m_HasBoard && (m_State == STATE_PLAYING || m_State == STATE_OVER);

	const CUIRect Screen = *Ui()->Screen();
	CUIRect Panel;
	Panel.w = ShowBoard ? 210.0f : 260.0f;
	Panel.h = ShowBoard ? 250.0f : 84.0f;
	Panel.x = Screen.x + (Screen.w - Panel.w) / 2.0f;
	Panel.y = Screen.y + (Screen.h - Panel.h) / 2.0f;
	Panel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * Alpha), IGraphics::CORNER_ALL, 8.0f);

	CUIRect Content;
	Panel.Margin(10.0f, &Content);

	CUIRect Title, Status, Body, ButtonRow;
	Content.HSplitTop(16.0f, &Title, &Content);
	Content.HSplitTop(14.0f, &Status, &Content);
	Content.HSplitBottom(20.0f, &Body, &ButtonRow);
	Body.HSplitBottom(6.0f, &Body, nullptr);

	SLabelProperties Props;
	Props.m_MaxWidth = Title.w;
	Props.m_EllipsisAtEnd = true;

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	Ui()->DoLabel(&Title, Localize("Tic tac toe"), 11.0f, TEXTALIGN_ML, Props);

	char aStatus[128];
	StatusText(aStatus, sizeof(aStatus));
	if(m_Result == 'W')
		TextRender()->TextColor(ColorRGBA(0.4f, 1.0f, 0.4f, Alpha));
	else if(m_Result == 'L')
		TextRender()->TextColor(ColorRGBA(1.0f, 0.5f, 0.5f, Alpha));
	else
		TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, Alpha));
	Ui()->DoLabel(&Status, aStatus, 9.0f, TEXTALIGN_ML, Props);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(ShowBoard)
		RenderBoard(Body, Interactive, Alpha);

	if(!Interactive)
	{
		TextRender()->TextColor(ColorRGBA(0.6f, 0.6f, 0.6f, Alpha));
		Ui()->DoLabel(&ButtonRow, Localize("Left click to use the mouse"), 8.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	const char *pRightLabel = nullptr;
	const char *pLeftLabel = nullptr;
	switch(m_State)
	{
	case STATE_PLAYING:
		pRightLabel = Localize("Leave");
		break;
	case STATE_INVITED:
		pRightLabel = Localize("Decline");
		pLeftLabel = Localize("Accept");
		break;
	case STATE_OVER:
		pRightLabel = Localize("Close");
		if(m_OpponentId >= 0 && GameClient()->m_aClients[m_OpponentId].m_Active)
			pLeftLabel = Localize("Rematch");
		break;
	default:
		return;
	}

	CUIRect RightButton, LeftButton;
	ButtonRow.VSplitRight(80.0f, &ButtonRow, &RightButton);
	ButtonRow.VSplitRight(6.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(80.0f, nullptr, &LeftButton);

	static CButtonContainer s_RightButton;
	static CButtonContainer s_LeftButton;

	RightButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (Ui()->HotItem() == &s_RightButton ? 0.25f : 0.12f) * Alpha), IGraphics::CORNER_ALL, 5.0f);
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	Ui()->DoLabel(&RightButton, pRightLabel, 10.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(Ui()->DoButtonLogic(&s_RightButton, 0, &RightButton, BUTTONFLAG_LEFT))
	{
		Close();
		return;
	}

	if(!pLeftLabel)
		return;

	LeftButton.Draw((Ui()->HotItem() == &s_LeftButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f)).WithAlpha(Alpha), IGraphics::CORNER_ALL, 5.0f);
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
	Ui()->DoLabel(&LeftButton, pLeftLabel, 10.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	if(Ui()->DoButtonLogic(&s_LeftButton, 0, &LeftButton, BUTTONFLAG_LEFT))
	{
		if(m_State == STATE_INVITED)
		{
			StartGame(m_OpponentId, false);
			SendProtocol("A", MAX_RETRIES);
		}
		else
		{
			Challenge(m_OpponentId);
		}
	}
}

void CTicTacToe::RenderBoard(CUIRect Area, bool Interactive, float Alpha)
{
	const float BoardSize = std::min(Area.w, Area.h);
	CUIRect Grid;
	Grid.x = Area.x + (Area.w - BoardSize) / 2.0f;
	Grid.y = Area.y + (Area.h - BoardSize) / 2.0f;
	Grid.w = BoardSize;
	Grid.h = BoardSize;

	const bool MyMove = MyTurn();
	static char s_aCellIds[9];
	const float CellSize = BoardSize / 3.0f;
	for(int i = 0; i < 9; i++)
	{
		CUIRect Cell;
		Cell.x = Grid.x + (i % 3) * CellSize;
		Cell.y = Grid.y + (i / 3) * CellSize;
		Cell.w = CellSize;
		Cell.h = CellSize;
		Cell.Margin(3.0f, &Cell);

		const bool Playable = MyMove && m_aBoard[i] == '.';
		if(Interactive && Playable && Ui()->DoButtonLogic(&s_aCellIds[i], 0, &Cell, BUTTONFLAG_LEFT))
		{
			PlayCell(i);
			return;
		}

		const bool InWinLine = m_aWinLine[0] == i || m_aWinLine[1] == i || m_aWinLine[2] == i;
		if(InWinLine)
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.4f * Alpha), IGraphics::CORNER_ALL, 5.0f);
		else if(Interactive && Playable && Ui()->HotItem() == &s_aCellIds[i])
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f * Alpha), IGraphics::CORNER_ALL, 5.0f);
		else
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f * Alpha), IGraphics::CORNER_ALL, 5.0f);

		if(m_aBoard[i] == '.')
			continue;

		const char aMark[2] = {m_aBoard[i], '\0'};
		if(m_aBoard[i] == 'X')
			TextRender()->TextColor(CMenus::AccentColor().WithAlpha(Alpha));
		else
			TextRender()->TextColor(COLOR_MARK_OTHER.WithAlpha(Alpha));
		TextRender()->TextOutlineColor(COLOR_OUTLINE.WithAlpha(Alpha));
		Ui()->DoLabel(&Cell, aMark, CellSize * 0.6f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	}
}

void CTicTacToe::OnRender()
{
	FlushSendQueue();

	if(m_ViewActive && ViewKeyState() == 0)
	{
		m_ViewActive = false;
		m_CursorActive = false;
		m_IgnoreClick = false;
		m_KeyBlocked = false;
		if(m_State == STATE_SELECT)
			Close();
	}

	if(!IsActive())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		OnReset();
		return;
	}

	if(m_State != STATE_SELECT && m_State != STATE_OVER && m_LocalId != GameClient()->m_Snap.m_LocalClientId)
	{
		Finish(0, "Aborted, you switched to another tee.");
	}
	else if(m_State != STATE_SELECT && m_State != STATE_OVER && (m_OpponentId < 0 || !GameClient()->m_aClients[m_OpponentId].m_Active))
	{
		Finish(0, "Your opponent left the server.");
	}
	else if((m_State == STATE_INVITED || m_State == STATE_RINGING) && LocalTime() > m_AcceptDeadline)
	{
		if(m_State == STATE_INVITED)
		{
			Close();
			return;
		}
		char aStatus[128];
		str_format(aStatus, sizeof(aStatus), "%s did not answer.", OpponentName());
		Finish(0, aStatus);
	}

	UpdateRetry();
	UpdatePendingAck();

	if(m_State == STATE_OVER && !m_HasBoard && m_ViewActive && !m_KeyBlocked)
		OpenSelect();

	if(m_State == STATE_SELECT)
	{
		if(!WindowVisible())
			return;

		Ui()->MapScreen();
		Ui()->StartCheck();
		Ui()->Update();
		RenderSelectModal();
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f * g_Config.m_ClMClientMenuCursorSize / 100.0f);
		Ui()->FinishCheck();
		return;
	}

	float Alpha = 1.0f;
	if(m_State == STATE_OVER)
	{
		if(m_ViewActive)
			m_FadeTime = LocalTime() + RESULT_HOLD;
		const float Left = m_FadeTime + RESULT_FADE - LocalTime();
		if(Left <= 0.0f)
		{
			Close();
			return;
		}
		Alpha = std::clamp(Left / RESULT_FADE, 0.0f, 1.0f);
	}

	if(!WindowVisible())
		return;

	m_ShowKeyHint = m_State == STATE_INVITED || m_State == STATE_PLAYING || (m_State == STATE_OVER && m_HasBoard);

	if(m_IgnoreClick && !Input()->KeyIsPressed(KEY_MOUSE_1))
		m_IgnoreClick = false;

	const bool CursorShown = ViewShown() && m_CursorActive;
	const bool Interactive = CursorShown && !m_IgnoreClick;
	Ui()->MapScreen();
	if(CursorShown)
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	RenderStatusBar(Alpha);
	if(ViewShown())
		RenderView(Interactive, Alpha);

	if(CursorShown)
	{
		RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f * g_Config.m_ClMClientMenuCursorSize / 100.0f);
		Ui()->FinishCheck();
	}
}

void CTicTacToe::OnRelease()
{
	m_ViewActive = false;
	m_CursorActive = false;
}

bool CTicTacToe::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_State == STATE_SELECT)
	{
		if(!WindowVisible())
			return false;
	}
	else if(!m_CursorActive || !ViewShown())
	{
		return false;
	}

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CTicTacToe::OnInput(const IInput::CEvent &Event)
{
	if(m_State == STATE_SELECT)
	{
		if(!WindowVisible())
			return false;

		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
		{
			Close();
			return true;
		}

		if(Event.m_Key != KEY_MOUSE_1 && Event.m_Key != KEY_MOUSE_2 && Event.m_Key != KEY_MOUSE_3)
			return false;

		Ui()->OnInput(Event);
		return true;
	}

	if(!ViewShown() || !WindowVisible())
		return false;

	if(!m_CursorActive)
	{
		if(Event.m_Key != KEY_MOUSE_1)
			return false;
		if(Event.m_Flags & IInput::FLAG_PRESS)
		{
			m_CursorActive = true;
			m_IgnoreClick = true;
		}
		return true;
	}

	if(Event.m_Key != KEY_MOUSE_1 && Event.m_Key != KEY_MOUSE_2 && Event.m_Key != KEY_MOUSE_3)
		return false;

	Ui()->OnInput(Event);
	return true;
}
