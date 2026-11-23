#include "minigames.h"

#include "binds.h"
#include "menus.h"

#include <base/math.h>
#include <base/secure.h>
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
#include <cmath>
#include <utility>

namespace
{
	const char *PROTOCOL_PREFIX = "MGAME1 ";
	const char *VIEW_BIND = "+minigames";
	const char *g_apGameNames[CMiniGames::NUM_GAMES] = {"Tic tac toe", "Chess", "Battleship", "Memory"};

	const int g_aShipSizes[10] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
	const int SHIP_CELLS = 20;
	const float BATTLE_NOTE_TIME = 4.0f;
	const float MEMORY_SECOND_DELAY = 0.6f;
	const float MEMORY_PEEK_TIME = 1.6f;
	const float MEMORY_FLIP_TIME = 0.28f;

	// handshake goes over whispers
	const float SEND_INTERVAL = 1.2f;
	const float CHAT_SCORE_PENALTY = 250.0f;
	const float CHAT_SCORE_DECAY = 50.0f;
	const float CHAT_SCORE_BUDGET = 750.0f;
	const float RETRY_INTERVAL = 8.0f;
	const int MAX_RETRIES = 3;
	const int MAX_CHALLENGE_RETRIES = 1;
	const float ACCEPT_TIMEOUT = 60.0f;
	const float RESULT_HOLD = 5.0f;
	const float RESULT_FADE = 2.0f;
	const float TURN_GLOW_SPEED = 1.6f;
	const float TURN_GLOW_BASE = 0.22f;
	const float TURN_GLOW_PULSE = 0.08f;

	const int EMOTE_RADIX = 12;
	const int EMOTE_OP_MOVE = 12;
	const int EMOTE_OP_ACK = 13;
	const int EMOTE_OP_CHESS = 14;

	const float EMOTE_ECHO_TIMEOUT = 2.0f;
	const int MAX_EMOTE_RETRIES = 4;
	const float MOVE_RETRY_INTERVAL = 6.0f;
	const int MAX_MOVE_RETRIES = 3;

	int FramePayload(int Op, int Game)
	{
		const bool Battle = Game == CMiniGames::GAME_BATTLESHIP;
		switch(Op)
		{
		case EMOTE_OP_MOVE:
			return Battle ? 3 : Game == CMiniGames::GAME_MEMORY ? 5 :
									      2;
		case EMOTE_OP_ACK:
			return Battle ? 2 : 1;
		case EMOTE_OP_CHESS:
			return 5;
		default:
			return -1;
		}
	}

	float BackgroundAlpha()
	{
		return g_Config.m_ClMClientMiniGamesAlpha / 100.0f;
	}

	float BarAlpha()
	{
		return g_Config.m_ClMClientMiniGamesBarAlpha / 100.0f;
	}

	const ColorRGBA COLOR_MARK_OTHER = ColorRGBA(0.95f, 0.95f, 0.95f, 1.0f);
	const ColorRGBA COLOR_OUTLINE = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);

	const char *PieceGlyph(char Piece)
	{
		switch(Piece >= 'a' ? Piece - ('a' - 'A') : Piece)
		{
		case 'K': return "\xe2\x99\x9a";
		case 'Q': return "\xe2\x99\x9b";
		case 'R': return "\xe2\x99\x9c";
		case 'B': return "\xe2\x99\x9d";
		case 'N': return "\xe2\x99\x9e";
		case 'P': return "\xe2\x99\x9f";
		default: return "";
		}
	}

	int ParseInts(const char *pText, int *pOut, int Max)
	{
		int Num = 0;
		while(Num < Max)
		{
			while(*pText == ' ')
				pText++;
			if(*pText < '0' || *pText > '9')
				break;
			int Value = 0;
			while(*pText >= '0' && *pText <= '9')
			{
				Value = Value * 10 + (*pText - '0');
				if(Value > 100000000)
					return -1;
				pText++;
			}
			pOut[Num++] = Value;
		}
		while(*pText == ' ')
			pText++;
		return *pText == '\0' ? Num : -1;
	}

	const int g_aaWinLines[8][3] = {
		{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // rows
		{0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // columns
		{0, 4, 8}, {2, 4, 6}}; // diagonals
}

void CMiniGames::ConMiniGames(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CMiniGames *>(pUserData)->Toggle();
}

void CMiniGames::ConKeyMiniGames(IConsole::IResult *pResult, void *pUserData)
{
	CMiniGames *pSelf = static_cast<CMiniGames *>(pUserData);

	const bool ToggleMode = g_Config.m_ClMClientMiniGamesHold == 0;

	if(pResult->GetInteger(0) == 0)
	{
		if(!ToggleMode)
			pSelf->HideView();
		return;
	}

	if(ToggleMode && pSelf->m_ViewActive)
	{
		pSelf->HideView();
		return;
	}

	pSelf->m_ViewActive = true;
	if(pSelf->m_State == STATE_IDLE && !pSelf->m_KeyBlocked)
		pSelf->OpenGames();
}

void CMiniGames::OnConsoleInit()
{
	Console()->Register("minigames", "", CFGFLAG_CLIENT, ConMiniGames, this, "M-Client: challenge another M-Client player to a game");
	Console()->Register("+minigames", "", CFGFLAG_CLIENT, ConKeyMiniGames, this, "M-Client: open the game selection, hold to show a running game");
}

void CMiniGames::OnReset()
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
	m_ChatScore = 0.0f;
	m_ChatScoreTime = LocalTime();
	m_vSendQueue.clear();
	m_NextSendTime = 0.0f;
	ClearRetry();
	ResetEmoteChannel();
}

bool CMiniGames::WhisperSupported() const
{
	return GameClient()->m_Chat.ServerHasCommand("w");
}

bool CMiniGames::WindowVisible() const
{
	return !GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive() && !GameClient()->m_Scoreboard.IsActive();
}

// -1 when no key is bound, 0 when it is up and 1 while it is held down
int CMiniGames::ViewKeyState() const
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

bool CMiniGames::NeedsAttention() const
{
	return m_State == STATE_INVITED || MyTurn();
}

bool CMiniGames::ViewShown() const
{
	if(m_State == STATE_INVITED || m_State == STATE_PLAYING)
		return m_ViewActive;
	return m_ViewActive && m_State == STATE_OVER && m_HasBoard;
}

bool CMiniGames::MyTurn() const
{
	if(m_State != STATE_PLAYING)
		return false;
	if(m_Game == GAME_CHESS)
		return m_Chess.WhiteToMove() == AmWhite();
	if(m_Game == GAME_BATTLESHIP)
		return m_BattleMyTurn;
	if(m_Game == GAME_MEMORY)
		return m_MemoryTurn == m_MyMark;
	return (m_MoveCount % 2 == 0) == (m_MyMark == 'X');
}

void CMiniGames::StatusText(char *pBuf, size_t Size) const
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
		if((m_Game == GAME_BATTLESHIP || (m_Game == GAME_MEMORY && m_MemoryPick < 0)) && m_BattleNote != NOTE_NONE && LocalTime() - m_BattleNoteTime < BATTLE_NOTE_TIME)
		{
			switch(m_BattleNote)
			{
			case NOTE_MISS:
				str_copy(pBuf, Localize("Water."), Size);
				break;
			case NOTE_HIT:
				str_copy(pBuf, Localize("Hit! Fire again."), Size);
				break;
			case NOTE_SUNK:
				str_copy(pBuf, Localize("Sunk! Fire again."), Size);
				break;
			case NOTE_FOE_MISS:
				str_copy(pBuf, Localize("Missed you, your turn."), Size);
				break;
			case NOTE_FOE_HIT:
				str_copy(pBuf, Localize("One of your ships was hit."), Size);
				break;
			case NOTE_PAIR:
				str_copy(pBuf, Localize("Pair! Flip again."), Size);
				break;
			case NOTE_NO_PAIR:
				str_format(pBuf, Size, Localize("No pair, %s goes next."), OpponentName());
				break;
			case NOTE_FOE_PAIR:
				str_format(pBuf, Size, Localize("%s found a pair."), OpponentName());
				break;
			case NOTE_FOE_NO_PAIR:
				str_copy(pBuf, Localize("No pair, your turn."), Size);
				break;
			default:
				str_copy(pBuf, Localize("One of your ships went down."), Size);
				break;
			}
		}
		else if(!MyTurn())
			str_format(pBuf, Size, Localize("%s is thinking..."), OpponentName());
		else if(m_Game == GAME_BATTLESHIP)
			str_copy(pBuf, Localize("Your turn, pick a target"), Size);
		else if(m_Game == GAME_MEMORY)
			str_copy(pBuf, m_MemoryPick < 0 ? Localize("Your turn, flip two cards") : Localize("Now find the matching card"), Size);
		else if(m_Game == GAME_CHESS)
			str_format(pBuf, Size, Localize("Your turn, you play %s"), AmWhite() ? Localize("white") : Localize("black"));
		else
			str_format(pBuf, Size, Localize("Your turn, you play %c"), m_MyMark);
		break;
	case STATE_OVER:
		str_copy(pBuf, m_aStatus, Size);
		break;
	default:
		pBuf[0] = '\0';
		break;
	}
}

const char *CMiniGames::OpponentName() const
{
	if(m_OpponentId < 0 || m_OpponentId >= MAX_CLIENTS)
		return "";
	return GameClient()->m_aClients[m_OpponentId].m_aName;
}

const char *CMiniGames::GameName() const
{
	return g_apGameNames[m_Game];
}

void CMiniGames::HideView()
{
	if(m_State == STATE_GAMES || m_State == STATE_SELECT)
		Close();
	m_ViewActive = false;
	m_CursorActive = false;
	m_IgnoreClick = false;
	m_KeyBlocked = false;
}

void CMiniGames::Toggle()
{
	if(IsActive())
		Close();
	else
		OpenGames();
}

void CMiniGames::OpenGames()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(!WhisperSupported())
	{
		GameClient()->Echo("Mini games: this server does not support whispers.");
		return;
	}

	m_Result = 0;
	m_aStatus[0] = '\0';
	m_State = STATE_GAMES;

	GameClient()->m_MClientDetect.Announce();
}

void CMiniGames::OpenSelect()
{
	m_SelectedId = -1;
	m_State = STATE_SELECT;
	if(GameClient()->m_MClientDetect.NumDetected() == 0)
		GameClient()->m_MClientDetect.Refresh();
}

void CMiniGames::Close()
{
	if(m_State == STATE_PLAYING || m_State == STATE_CALLING || m_State == STATE_RINGING)
		SendTo(m_OpponentId, "Q");
	else if(m_State == STATE_INVITED)
		SendTo(m_OpponentId, "D");

	m_KeyBlocked = m_ViewActive && g_Config.m_ClMClientMiniGamesHold;
	if(!g_Config.m_ClMClientMiniGamesHold)
		m_ViewActive = false;
	m_State = STATE_IDLE;
	m_OpponentId = -1;
	m_LocalId = -1;
	m_SelectedId = -1;
	m_HasBoard = false;
	m_CursorActive = false;
	m_IgnoreClick = false;
	m_Result = 0;
	m_aStatus[0] = '\0';
	ClearRetry();
	ResetEmoteChannel();
}

void CMiniGames::Finish(char Result, const char *pStatus)
{
	m_Result = Result;
	str_copy(m_aStatus, pStatus);
	m_State = STATE_OVER;
	m_FadeTime = LocalTime() + RESULT_HOLD;
	ClearRetry();
}

void CMiniGames::Challenge(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return;

	m_OpponentId = ClientId;
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	m_MyMark = 'X';
	str_copy(m_aBoard, ".........");
	m_Chess.Reset();
	m_ChessSelected = -1;
	PlaceShips();
	for(char &Cell : m_aFoeWaters)
		Cell = '.';
	m_BattleMyTurn = true;
	m_BattleShot = -1;
	m_BattleNote = NOTE_NONE;
	m_MoveCount = 0;
	m_aWinLine[0] = m_aWinLine[1] = m_aWinLine[2] = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	m_HasBoard = false;
	m_State = STATE_CALLING;
	char aMessage[32];
	if(m_Game == GAME_MEMORY)
	{
		m_MemorySeed = secure_rand_below(1000000);
		str_format(aMessage, sizeof(aMessage), "C %d %d %d", (int)m_Game, m_MemorySeed, MemoryCards::Version());
	}
	else
		str_format(aMessage, sizeof(aMessage), "C %d", (int)m_Game);
	SendProtocol(aMessage, MAX_CHALLENGE_RETRIES);
}

void CMiniGames::StartGame(int OpponentId, bool Challenger)
{
	m_OpponentId = OpponentId;
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	m_MyMark = Challenger ? 'X' : 'O';
	str_copy(m_aBoard, ".........");
	m_Chess.Reset();
	m_ChessSelected = -1;
	PlaceShips();
	for(char &Cell : m_aFoeWaters)
		Cell = '.';
	m_BattleMyTurn = Challenger;
	m_BattleShot = -1;
	m_BattleNote = NOTE_NONE;
	m_MoveCount = 0;
	m_aWinLine[0] = m_aWinLine[1] = m_aWinLine[2] = -1;
	m_Result = 0;
	m_aStatus[0] = '\0';
	ResetMemory(Challenger);
	m_HasBoard = true;
	m_State = STATE_PLAYING;
	ClearRetry();
	ResetEmoteChannel();
}

void CMiniGames::QueueWhisper(int ClientId, const char *pMessage, bool Priority, int ReplaceKey, int Conn)
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
	str_format(aLine, sizeof(aLine), "/w \"%s\" %s", aName, pMessage);
	if(ReplaceKey != 0)
	{
		m_vSendQueue.erase(std::remove_if(m_vSendQueue.begin(), m_vSendQueue.end(), [ClientId, ReplaceKey, Conn](const CQueuedWhisper &Queued) {
			return Queued.m_ClientId == ClientId && Queued.m_ReplaceKey == ReplaceKey && Queued.m_Conn == Conn;
		}),
			m_vSendQueue.end());
	}
	CQueuedWhisper Queued{ClientId, ReplaceKey, aLine, Conn, Conn < 0 ? -1 : GameClient()->m_aLocalIds[Conn]};
	if(Priority)
		m_vSendQueue.insert(m_vSendQueue.begin(), std::move(Queued));
	else if(Queued.m_ReplaceKey == WHISPER_REPLACE_PET)
		m_vSendQueue.push_back(std::move(Queued));
	else
	{
		const auto FirstPet = std::find_if(m_vSendQueue.begin(), m_vSendQueue.end(), [](const CQueuedWhisper &Other) {
			return Other.m_ReplaceKey == WHISPER_REPLACE_PET;
		});
		m_vSendQueue.insert(FirstPet, std::move(Queued));
	}
}

void CMiniGames::SendTo(int ClientId, const char *pMessage, bool Priority, int ReplaceKey)
{
	char aProtocolMessage[256];
	str_format(aProtocolMessage, sizeof(aProtocolMessage), "%s%s", PROTOCOL_PREFIX, pMessage);
	QueueWhisper(ClientId, aProtocolMessage, Priority, ReplaceKey);
}

void CMiniGames::SendProtocol(const char *pMessage, int MaxRetries)
{
	SendTo(m_OpponentId, pMessage);
	m_RetryMessage = pMessage;
	m_RetryTime = LocalTime() + RETRY_INTERVAL;
	m_RetryCount = 0;
	m_RetryMax = MaxRetries;
}

void CMiniGames::ClearRetry()
{
	m_RetryMessage.clear();
	m_RetryTime = 0.0f;
	m_RetryCount = 0;
	m_RetryMax = MAX_RETRIES;
}

void CMiniGames::FlushSendQueue()
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

	// Pet updates belong to the tee that queued them, even after a dummy switch.
	m_vSendQueue.erase(std::remove_if(m_vSendQueue.begin(), m_vSendQueue.end(), [this](const CQueuedWhisper &Queued) {
		return Queued.m_Conn >= 0 &&
		       (GameClient()->m_aLocalIds[Queued.m_Conn] != Queued.m_SenderId ||
			       (Queued.m_ReplaceKey == WHISPER_REPLACE_PET && !g_Config.m_ClMClientUserDetection));
	}),
		m_vSendQueue.end());
	if(m_vSendQueue.empty())
		return;

	if(Now < m_NextSendTime)
		return;

	const float Reserve = m_vSendQueue.front().m_ReplaceKey == WHISPER_REPLACE_PET ? CHAT_SCORE_PENALTY : 0.0f;
	if(m_ChatScore + CHAT_SCORE_PENALTY + Reserve > CHAT_SCORE_BUDGET)
		return;

	GameClient()->m_Chat.SendChat(0, m_vSendQueue.front().m_Command.c_str(), m_vSendQueue.front().m_Conn);
	m_vSendQueue.erase(m_vSendQueue.begin());
	m_ChatScore += CHAT_SCORE_PENALTY;
	m_NextSendTime = Now + SEND_INTERVAL;
}

void CMiniGames::UpdateRetry()
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

int CMiniGames::BoardHash() const
{
	char aState[208];
	const char *pState = m_aBoard;
	if(m_Game == GAME_CHESS)
	{
		m_Chess.Serialize(aState, sizeof(aState));
		pState = aState;
	}
	else if(m_Game == GAME_BATTLESHIP)
	{
		const char *apGrids[2] = {AmWhite() ? m_aMyWaters : m_aFoeWaters, AmWhite() ? m_aFoeWaters : m_aMyWaters};
		int Out = 0;
		for(const char *pGrid : apGrids)
		{
			for(int i = 0; i < 100; i++)
				aState[Out++] = pGrid[i] == 'o' || pGrid[i] == 'X' ? pGrid[i] : '.';
		}
		aState[Out++] = m_BattleMyTurn == AmWhite() ? 'X' : 'O';
		aState[Out] = '\0';
		pState = aState;
	}
	else if(m_Game == GAME_MEMORY)
	{
		for(int i = 0; i < MemoryCards::NUM_CARDS; i++)
			aState[i] = m_aMemoryOwner[i];
		aState[MemoryCards::NUM_CARDS] = m_MemoryTurn;
		aState[MemoryCards::NUM_CARDS + 1] = '\0';
		pState = aState;
	}

	unsigned Hash = 2166136261u;
	for(int i = 0; pState[i] != '\0'; i++)
	{
		Hash ^= (unsigned char)pState[i];
		Hash *= 16777619u;
	}
	return (int)(Hash % (unsigned)EMOTE_RADIX);
}

void CMiniGames::ResetEmoteChannel()
{
	m_vEmoteQueue.clear();
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;
	m_FrameOp = -1;
	m_FrameExpect = 0;
	m_FrameLen = 0;
	ClearMoveRetry();
}

void CMiniGames::SendFrame(int Op, const int *pDigits, int NumDigits)
{
	int Check = Op;
	m_vEmoteQueue.push_back({Op, true});
	for(int i = 0; i < NumDigits; i++)
	{
		m_vEmoteQueue.push_back({pDigits[i], true});
		Check += pDigits[i];
	}
	m_vEmoteQueue.push_back({Check % EMOTE_RADIX, true});
}

void CMiniGames::SendMoveAck(int Cell)
{
	int aDigits[2];
	aDigits[0] = BoardHash();
	if(m_Game != GAME_BATTLESHIP)
	{
		SendFrame(EMOTE_OP_ACK, aDigits, 1);
		return;
	}

	aDigits[1] = Cell >= 0 ? ShotResult(Cell) : 0;
	SendFrame(EMOTE_OP_ACK, aDigits, 2);
}

void CMiniGames::ClearMoveRetry()
{
	m_MoveFrameLen = 0;
	m_MovePending = false;
	m_MoveRetryTime = 0.0f;
	m_MoveRetryCount = 0;
}

void CMiniGames::UpdateMoveRetry()
{
	if(!m_MovePending || LocalTime() < m_MoveRetryTime)
		return;

	m_MoveRetryCount++;
	if(m_MoveRetryCount > MAX_MOVE_RETRIES)
	{
		const bool Running = m_State == STATE_PLAYING;
		ClearMoveRetry();
		if(Running)
			Finish(0, "Your opponent is not responding.");
		return;
	}

	SendFrame(m_Game == GAME_CHESS ? EMOTE_OP_CHESS : EMOTE_OP_MOVE, m_aMoveFrame, m_MoveFrameLen);
	m_MoveRetryTime = LocalTime() + MOVE_RETRY_INTERVAL;
}

void CMiniGames::FlushEmoteQueue()
{
	if(m_vEmoteQueue.empty())
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		ResetEmoteChannel();
		return;
	}

	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return;

	if(!m_EmoteWaiting && GameClient()->m_MClientDetect.EmoteChannelBusy())
	{
		// user detection sends long frames, it has to step aside for a game
		GameClient()->m_MClientDetect.YieldEmoteChannel();
		return;
	}

	if(m_EmoteWaiting)
	{
		if(LocalTime() < m_EmoteTime + EMOTE_ECHO_TIMEOUT)
			return;

		m_EmoteRetries++;
		if(m_EmoteRetries > MAX_EMOTE_RETRIES)
		{
			const bool Running = m_State == STATE_PLAYING;
			ResetEmoteChannel();
			if(Running)
				Finish(0, "Stopped, this server does not pass emoticons through.");
			return;
		}
	}

	CNetMsg_Cl_Emoticon Msg;
	Msg.m_Emoticon = m_vEmoteQueue.front().m_Emoticon;
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
	m_EmoteWaiting = true;
	m_EmoteTime = LocalTime();
}

bool CMiniGames::QueueManualEmote(int Emoticon)
{
	if(m_vEmoteQueue.empty())
		return false;

	m_vEmoteQueue.push_back({Emoticon, false});
	return true;
}

bool CMiniGames::HandleEmoteEcho(int Emoticon)
{
	if(!m_EmoteWaiting || m_vEmoteQueue.empty() || m_vEmoteQueue.front().m_Emoticon != Emoticon)
		return false;

	const bool Protocol = m_vEmoteQueue.front().m_Protocol;
	m_vEmoteQueue.erase(m_vEmoteQueue.begin());
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;
	return Protocol;
}

bool CMiniGames::HandleEmoteFrame(int Emoticon)
{
	if(Emoticon >= EMOTE_RADIX)
	{
		m_FrameExpect = FramePayload(Emoticon, m_Game);
		m_FrameOp = m_FrameExpect < 0 ? -1 : Emoticon;
		m_FrameLen = 0;
		return m_FrameOp >= 0;
	}

	if(m_FrameOp < 0)
		return false;

	m_aFrame[m_FrameLen++] = Emoticon;
	if(m_FrameLen <= m_FrameExpect)
		return true;

	int Check = m_FrameOp;
	for(int i = 0; i < m_FrameExpect; i++)
		Check += m_aFrame[i];
	if(Check % EMOTE_RADIX == m_aFrame[m_FrameExpect])
		ProcessFrame();
	m_FrameOp = -1;
	return true;
}

void CMiniGames::ProcessFrame()
{
	if(m_FrameOp == EMOTE_OP_ACK)
	{
		if(!m_MovePending)
			return;

		if(m_Game == GAME_BATTLESHIP && m_BattleShot >= 0)
		{
			ApplyShotResult(m_BattleShot, m_aFrame[1]);
			m_BattleShot = -1;
		}
		if(m_aFrame[0] == BoardHash())
			ClearMoveRetry();
		return;
	}

	const bool Chess = m_FrameOp == EMOTE_OP_CHESS;
	if(Chess != (m_Game == GAME_CHESS))
		return;

	if(m_Game == GAME_BATTLESHIP)
	{
		const int Cell = m_aFrame[1] * EMOTE_RADIX + m_aFrame[2];
		if(Cell >= 100)
			return;

		const bool Fresh = m_aMyWaters[Cell] != 'o' && m_aMyWaters[Cell] != 'X';
		if(Fresh && m_State == STATE_PLAYING && !MyTurn() && m_aFrame[0] == BoardHash())
		{
			const bool Hit = m_aMyWaters[Cell] == 'S';
			m_aMyWaters[Cell] = Hit ? 'X' : 'o';
			m_MoveCount++;
			m_BattleMyTurn = !Hit;
			m_BattleNote = !Hit ? NOTE_FOE_MISS : ShipSunk(Cell) ? NOTE_FOE_SUNK :
									       NOTE_FOE_HIT;
			m_BattleNoteTime = LocalTime();
			CheckGameEnd();
		}

		if(m_aMyWaters[Cell] == 'o' || m_aMyWaters[Cell] == 'X')
			SendMoveAck(Cell);
		return;
	}

	if(m_Game == GAME_MEMORY)
	{
		const int First = m_aFrame[1] * EMOTE_RADIX + m_aFrame[2];
		const int Second = m_aFrame[3] * EMOTE_RADIX + m_aFrame[4];
		const bool Valid = First < MemoryCards::NUM_CARDS && Second < MemoryCards::NUM_CARDS &&
				   m_aMemoryOwner[First] == '.' && m_aMemoryOwner[Second] == '.';
		const bool Current = Valid && m_State == STATE_PLAYING && !MyTurn() && m_aFrame[0] == BoardHash();
		if(First == Second)
		{
			if(Current)
			{
				m_MemoryFoePick = First;
				m_aMemoryPeek[0] = m_aMemoryPeek[1] = -1;
				m_MemoryPeekUntil = 0.0f;
			}
			return;
		}
		if(Current)
			PlayMemoryTurn(First, Second, false);
		SendMoveAck(-1);
		return;
	}

	if(m_State == STATE_PLAYING && !MyTurn() && m_aFrame[0] == BoardHash())
	{
		if(Chess)
		{
			const int From = m_aFrame[1] * EMOTE_RADIX + m_aFrame[2];
			const int To = m_aFrame[3] * EMOTE_RADIX + m_aFrame[4];
			if(From < 64 && To < 64 && m_Chess.ApplyMove(From, To))
			{
				m_ChessSelected = -1;
				m_MoveCount++;
				CheckGameEnd();
			}
		}
		else if(m_aFrame[1] < 9 && m_aBoard[m_aFrame[1]] == '.')
		{
			m_aBoard[m_aFrame[1]] = m_MyMark == 'X' ? 'O' : 'X';
			m_MoveCount++;
			CheckGameEnd();
		}
	}

	SendMoveAck(-1);
}

bool CMiniGames::OnEmoticon(int ClientId, int Emoticon)
{
	if(Emoticon < 0 || Emoticon >= NUM_EMOTICONS)
		return false;

	if(ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		return HandleEmoteEcho(Emoticon);
	if(ClientId == m_OpponentId && (m_State == STATE_PLAYING || m_State == STATE_OVER))
		return HandleEmoteFrame(Emoticon);
	return false;
}

void CMiniGames::PlayCell(int Cell)
{
	if(m_State != STATE_PLAYING || m_aBoard[Cell] != '.')
		return;
	if(!MyTurn())
		return;

	// the hash describes the board the opponent still has in front of them
	m_aMoveFrame[0] = BoardHash();
	m_aMoveFrame[1] = Cell;

	m_aBoard[Cell] = m_MyMark;
	m_MoveCount++;
	CheckGameEnd();

	m_MoveFrameLen = 2;
	m_MovePending = true;
	m_MoveRetryCount = 0;
	m_MoveRetryTime = LocalTime() + MOVE_RETRY_INTERVAL;
	SendFrame(EMOTE_OP_MOVE, m_aMoveFrame, m_MoveFrameLen);
}

void CMiniGames::ChessClick(int Square)
{
	if(m_State != STATE_PLAYING || !MyTurn())
		return;

	if(CChessBoard::IsOwn(m_Chess.Piece(Square), AmWhite()))
	{
		m_ChessSelected = m_ChessSelected == Square ? -1 : Square;
		return;
	}

	if(m_ChessSelected < 0)
		return;

	if(!PlayChessMove(m_ChessSelected, Square))
		m_ChessSelected = -1;
}

bool CMiniGames::PlayChessMove(int From, int To)
{
	const int Hash = BoardHash();
	if(!m_Chess.ApplyMove(From, To))
		return false;

	m_ChessSelected = -1;
	m_MoveCount++;
	CheckGameEnd();

	m_aMoveFrame[0] = Hash;
	m_aMoveFrame[1] = From / EMOTE_RADIX;
	m_aMoveFrame[2] = From % EMOTE_RADIX;
	m_aMoveFrame[3] = To / EMOTE_RADIX;
	m_aMoveFrame[4] = To % EMOTE_RADIX;
	m_MoveFrameLen = 5;
	m_MovePending = true;
	m_MoveRetryCount = 0;
	m_MoveRetryTime = LocalTime() + MOVE_RETRY_INTERVAL;
	SendFrame(EMOTE_OP_CHESS, m_aMoveFrame, m_MoveFrameLen);
	return true;
}

void CMiniGames::PlaceShips()
{
	for(int Attempt = 0; Attempt < 200; Attempt++)
	{
		for(char &Water : m_aMyWaters)
			Water = '.';

		bool Complete = true;
		for(const int Length : g_aShipSizes)
		{
			bool Placed = false;
			for(int Try = 0; Try < 500 && !Placed; Try++)
			{
				const bool Horizontal = secure_rand_below(2) == 0;
				const int StartX = secure_rand_below(Horizontal ? 11 - Length : 10);
				const int StartY = secure_rand_below(Horizontal ? 10 : 11 - Length);

				bool Free = true;
				for(int i = 0; i < Length && Free; i++)
				{
					const int Cx = StartX + (Horizontal ? i : 0);
					const int Cy = StartY + (Horizontal ? 0 : i);
					for(int Dy = -1; Dy <= 1 && Free; Dy++)
					{
						for(int Dx = -1; Dx <= 1 && Free; Dx++)
						{
							const int Nx = Cx + Dx;
							const int Ny = Cy + Dy;
							if(Nx < 0 || Nx > 9 || Ny < 0 || Ny > 9)
								continue;
							Free = m_aMyWaters[Ny * 10 + Nx] == '.';
						}
					}
				}
				if(!Free)
					continue;

				for(int i = 0; i < Length; i++)
					m_aMyWaters[(StartY + (Horizontal ? 0 : i)) * 10 + StartX + (Horizontal ? i : 0)] = 'S';
				Placed = true;
			}

			if(!Placed)
			{
				Complete = false;
				break;
			}
		}

		if(Complete)
			return;
	}
}

bool CMiniGames::ShipSunk(int Cell) const
{
	bool aSeen[100] = {false};
	int aStack[100];
	int Num = 0;
	aStack[Num++] = Cell;
	aSeen[Cell] = true;

	while(Num > 0)
	{
		const int At = aStack[--Num];
		if(m_aMyWaters[At] == 'S')
			return false;

		const int aDx[4] = {1, -1, 0, 0};
		const int aDy[4] = {0, 0, 1, -1};
		for(int i = 0; i < 4; i++)
		{
			const int Nx = At % 10 + aDx[i];
			const int Ny = At / 10 + aDy[i];
			if(Nx < 0 || Nx > 9 || Ny < 0 || Ny > 9)
				continue;
			const int Next = Ny * 10 + Nx;
			if(aSeen[Next] || (m_aMyWaters[Next] != 'S' && m_aMyWaters[Next] != 'X'))
				continue;
			aSeen[Next] = true;
			aStack[Num++] = Next;
		}
	}
	return true;
}

int CMiniGames::ShotResult(int Cell) const
{
	if(m_aMyWaters[Cell] != 'X')
		return 0;
	return ShipSunk(Cell) ? 2 : 1;
}

void CMiniGames::Shoot(int Cell)
{
	if(m_State != STATE_PLAYING || !MyTurn() || m_MovePending || m_aFoeWaters[Cell] != '.')
		return;

	m_BattleShot = Cell;
	m_aMoveFrame[0] = BoardHash();
	m_aMoveFrame[1] = Cell / EMOTE_RADIX;
	m_aMoveFrame[2] = Cell % EMOTE_RADIX;
	m_MoveFrameLen = 3;
	m_MovePending = true;
	m_MoveRetryCount = 0;
	m_MoveRetryTime = LocalTime() + MOVE_RETRY_INTERVAL;
	SendFrame(EMOTE_OP_MOVE, m_aMoveFrame, m_MoveFrameLen);
}

void CMiniGames::ApplyShotResult(int Cell, int Result)
{
	if(m_aFoeWaters[Cell] != '.')
		return;

	m_aFoeWaters[Cell] = Result == 0 ? 'o' : 'X';
	m_MoveCount++;
	m_BattleMyTurn = Result != 0;
	m_BattleNote = Result == 0 ? NOTE_MISS : Result == 2 ? NOTE_SUNK :
							       NOTE_HIT;
	m_BattleNoteTime = LocalTime();
	CheckGameEnd();
}

void CMiniGames::ResetMemory(bool Challenger)
{
	MemoryCards::Deal(m_MemorySeed, m_aMemoryFaces, m_aMemoryCards);
	for(int i = 0; i < MemoryCards::NUM_CARDS; i++)
	{
		m_aMemoryOwner[i] = '.';
		m_aMemoryShown[i] = false;
		m_aMemoryFlipAt[i] = -MEMORY_FLIP_TIME;
	}
	m_MemoryTurn = 'X';
	m_MemoryPick = -1;
	m_MemoryFoePick = -1;
	m_aMemoryPeek[0] = m_aMemoryPeek[1] = -1;
	m_MemoryPeekSecondAt = 0.0f;
	m_MemoryPeekUntil = 0.0f;

	if(m_Game != GAME_MEMORY || m_MemorySkinsLoaded)
		return;
	m_MemorySkinsLoaded = true;
	m_vpMemorySkins.clear();
	for(int i = 0; i < MemoryCards::NumStored(); i++)
		m_vpMemorySkins.push_back(GameClient()->m_Skins.LoadDetachedSkin("memory", MemoryCards::StoredPath(i)));
}

void CMiniGames::MemoryClick(int Card)
{
	if(m_State != STATE_PLAYING || !MyTurn() || m_aMemoryOwner[Card] != '.' || Card == m_MemoryPick || LocalTime() < m_MemoryPeekSecondAt)
		return;

	if(m_MemoryPick < 0)
	{
		m_MemoryPick = Card;
		m_aMemoryPeek[0] = m_aMemoryPeek[1] = -1;
		m_MemoryPeekUntil = 0.0f;
		const int aPick[5] = {BoardHash(), Card / EMOTE_RADIX, Card % EMOTE_RADIX, Card / EMOTE_RADIX, Card % EMOTE_RADIX};
		SendFrame(EMOTE_OP_MOVE, aPick, 5);
		return;
	}
	if(m_MovePending)
		return;

	const int First = m_MemoryPick;
	m_aMoveFrame[0] = BoardHash();
	m_aMoveFrame[1] = First / EMOTE_RADIX;
	m_aMoveFrame[2] = First % EMOTE_RADIX;
	m_aMoveFrame[3] = Card / EMOTE_RADIX;
	m_aMoveFrame[4] = Card % EMOTE_RADIX;
	PlayMemoryTurn(First, Card, true);

	m_MoveFrameLen = 5;
	m_MovePending = true;
	m_MoveRetryCount = 0;
	m_MoveRetryTime = LocalTime() + MOVE_RETRY_INTERVAL;
	SendFrame(EMOTE_OP_MOVE, m_aMoveFrame, m_MoveFrameLen);
}

void CMiniGames::PlayMemoryTurn(int First, int Second, bool Mine)
{
	const char Mark = Mine ? m_MyMark : OtherMark();
	const bool Pair = m_aMemoryCards[First] == m_aMemoryCards[Second];
	const float Now = LocalTime();

	const bool FirstShown = Mine || m_MemoryFoePick == First;
	m_aMemoryPeek[0] = First;
	m_aMemoryPeek[1] = Second;
	m_MemoryPeekSecondAt = Mine ? Now : Now + (FirstShown ? MEMORY_SECOND_DELAY / 3.0f : MEMORY_SECOND_DELAY);
	m_MemoryFoePick = -1;
	m_MemoryPeekUntil = m_MemoryPeekSecondAt + (Pair ? 0.0f : MEMORY_PEEK_TIME);
	m_MemoryPick = -1;

	if(Pair)
		m_aMemoryOwner[First] = m_aMemoryOwner[Second] = Mark;
	else
		m_MemoryTurn = Mark == 'X' ? 'O' : 'X';

	m_MoveCount++;
	m_BattleNote = Mine ? (Pair ? NOTE_PAIR : NOTE_NO_PAIR) : (Pair ? NOTE_FOE_PAIR : NOTE_FOE_NO_PAIR);
	m_BattleNoteTime = Now;
	CheckGameEnd();
}

bool CMiniGames::MemoryCardFaceUp(int Card) const
{
	const float Now = LocalTime();
	if(Card == m_aMemoryPeek[1] && Now < m_MemoryPeekSecondAt)
		return false;
	if(m_aMemoryOwner[Card] != '.' || Card == m_MemoryPick || Card == m_MemoryFoePick)
		return true;
	return (Card == m_aMemoryPeek[0] || Card == m_aMemoryPeek[1]) && Now < m_MemoryPeekUntil;
}

int CMiniGames::MemoryScore(char Mark) const
{
	int Cards = 0;
	for(const char Owner : m_aMemoryOwner)
		Cards += Owner == Mark ? 1 : 0;
	return Cards / 2;
}

bool CMiniGames::CheckGameEnd()
{
	if(m_Game == GAME_MEMORY)
	{
		const int Mine = MemoryScore(m_MyMark);
		const int Theirs = MemoryScore(OtherMark());
		if(Mine + Theirs < MemoryCards::NUM_PAIRS)
			return false;

		char aStatus[128];
		if(Mine > Theirs)
			str_format(aStatus, sizeof(aStatus), "You found %d pairs, you won!", Mine);
		else if(Mine < Theirs)
			str_format(aStatus, sizeof(aStatus), "%s found %d pairs and won.", OpponentName(), Theirs);
		else
			str_format(aStatus, sizeof(aStatus), "Draw, %d pairs each.", Mine);
		Finish(Mine > Theirs ? 'W' : Mine < Theirs ? 'L' :
							     'D',
			aStatus);
		return true;
	}

	if(m_Game == GAME_BATTLESHIP)
	{
		int MyHits = 0;
		int FoeHits = 0;
		for(int i = 0; i < 100; i++)
		{
			if(m_aMyWaters[i] == 'X')
				MyHits++;
			if(m_aFoeWaters[i] == 'X')
				FoeHits++;
		}

		if(FoeHits >= SHIP_CELLS)
		{
			Finish('W', "The whole fleet is sunk, you won!");
			return true;
		}
		if(MyHits >= SHIP_CELLS)
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s sank your fleet.", OpponentName());
			Finish('L', aStatus);
			return true;
		}
		return false;
	}

	if(m_Game == GAME_CHESS)
	{
		const CChessBoard::EResult Result = m_Chess.Result();
		if(Result == CChessBoard::RESULT_RUNNING)
			return false;

		if(Result == CChessBoard::RESULT_DRAW)
		{
			Finish('D', "Draw.");
			return true;
		}

		const bool IWon = (Result == CChessBoard::RESULT_WHITE_WON) == AmWhite();
		char aStatus[128];
		if(IWon)
			str_copy(aStatus, "You took the king, you won!");
		else
			str_format(aStatus, sizeof(aStatus), "%s took your king.", OpponentName());
		Finish(IWon ? 'W' : 'L', aStatus);
		return true;
	}

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

void CMiniGames::OnChatMessage(int ClientId, const char *pMessage)
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(ClientId >= 0 && ClientId == LocalId)
	{
		m_ChatScore += CHAT_SCORE_PENALTY;
		return;
	}

	if(ClientId >= 0 || LocalId < 0)
		return;
	if(m_State != STATE_CALLING && m_State != STATE_RINGING && m_State != STATE_INVITED)
		return;

	const bool Muted = str_find(pMessage, "You are not permitted to talk") != nullptr ||
			   (str_find(pMessage, "has been muted") != nullptr && str_find(pMessage, GameClient()->m_aClients[LocalId].m_aRealName) != nullptr);
	if(!Muted)
		return;

	m_vSendQueue.clear();
	Finish(0, "Stopped, the server muted you.");
}

bool CMiniGames::OnWhisper(int ClientId, int Team, const char *pMessage)
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
		else if(!g_Config.m_ClMClientMiniGames || Busy || Local)
		{
			SendTo(ClientId, "D");
		}
		else
		{
			int aArgs[3];
			const int NumArgs = ParseInts(pRest, aArgs, 3);
			const int Game = NumArgs >= 1 ? aArgs[0] : -1;
			const bool Valid = Game >= 0 && Game < NUM_GAMES && (Game == GAME_MEMORY ? NumArgs == 3 && aArgs[2] == MemoryCards::Version() : NumArgs == 1);
			if(!Valid)
			{
				SendTo(ClientId, "D");
				break;
			}
			if(Game == GAME_MEMORY)
				m_MemorySeed = aArgs[1];

			ClearRetry();
			m_Game = (EGame)Game;
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
			SendTo(ClientId, "K");
		}
		else if(FromOpponent && m_State == STATE_PLAYING && m_MoveCount == 0)
		{
			SendTo(ClientId, "K");
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

	case 'K':
		if(FromOpponent)
			ClearRetry();
		break;

	case 'Q':
		if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING || m_State == STATE_INVITED || m_State == STATE_PLAYING))
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s left the game.", OpponentName());
			Finish(0, aStatus);
			ResetEmoteChannel();
		}
		break;

	default:
		break;
	}

	return true;
}

int CMiniGames::CollectPlayers(int *pIds) const
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
			if(GameClient()->m_MClientDetect.Enabled() && !GameClient()->m_MClientDetect.IsMClient(ClientId))
				continue;
			pIds[NumPlayers++] = ClientId;
		}
	}
	return NumPlayers;
}

CUIRect CMiniGames::OpenWindow(float Width, float Height) const
{
	const CUIRect Screen = *Ui()->Screen();

	CUIRect Window;
	Screen.Margin((Screen.w - Width) / 2.0f, &Window);
	Window.h = Height;
	Window.y = Screen.y + (Screen.h - Window.h) / 2.0f;
	Window.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, BackgroundAlpha()), IGraphics::CORNER_ALL, 8.0f);
	Window.Margin(14.0f, &Window);
	return Window;
}

void CMiniGames::RenderGameIcon(int Game, CUIRect Area, float Alpha)
{
	const float Size = std::min(Area.w, Area.h);
	CUIRect Grid;
	Grid.x = Area.x + (Area.w - Size) / 2.0f;
	Grid.y = Area.y + (Area.h - Size) / 2.0f;
	Grid.w = Size;
	Grid.h = Size;

	TextRender()->TextOutlineColor(COLOR_OUTLINE.WithAlpha(Alpha));
	if(Game == GAME_CHESS)
	{
		const float CellSize = Size / 4.0f;
		for(int i = 0; i < 16; i++)
		{
			CUIRect Cell;
			Cell.x = Grid.x + (i % 4) * CellSize;
			Cell.y = Grid.y + (i / 4) * CellSize;
			Cell.w = CellSize;
			Cell.h = CellSize;

			const bool Light = ((i % 4) + (i / 4)) % 2 == 0;
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (Light ? 0.22f : 0.06f) * Alpha), IGraphics::CORNER_NONE, 0.0f);
		}
		TextRender()->TextColor(CMenus::AccentColor().WithAlpha(Alpha));
		Ui()->DoLabel(&Grid, PieceGlyph('Q'), Size * 0.7f, TEXTALIGN_MC);
	}
	else if(Game == GAME_MEMORY)
	{
		const float CellW = Size / 3.0f;
		const float CellH = Size / 2.0f;
		for(int i = 0; i < 6; i++)
		{
			CUIRect Cell;
			Cell.x = Grid.x + (i % 3) * CellW;
			Cell.y = Grid.y + (i / 3) * CellH;
			Cell.w = CellW;
			Cell.h = CellH;
			Cell.Margin(1.5f, &Cell);

			Cell.Draw(CMenus::AccentColor().WithAlpha(0.55f * Alpha), IGraphics::CORNER_ALL, 2.0f);
			CUIRect Inner;
			Cell.Margin(Cell.w * 0.15f, &Inner);
			Inner.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f * Alpha), IGraphics::CORNER_ALL, 1.5f);
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * Alpha));
			Ui()->DoLabel(&Cell, "?", Cell.h * 0.45f, TEXTALIGN_MC);
		}
	}
	else if(Game == GAME_BATTLESHIP)
	{
		const char aCells[26] = "..X...o......X..o....X...";
		const float CellSize = Size / 5.0f;
		for(int i = 0; i < 25; i++)
		{
			CUIRect Cell;
			Cell.x = Grid.x + (i % 5) * CellSize;
			Cell.y = Grid.y + (i / 5) * CellSize;
			Cell.w = CellSize;
			Cell.h = CellSize;
			Cell.Margin(1.0f, &Cell);

			if(aCells[i] == 'X')
				Cell.Draw(CMenus::AccentColor().WithAlpha(Alpha), IGraphics::CORNER_ALL, 1.5f);
			else if(aCells[i] == 'o')
				Cell.Draw(COLOR_MARK_OTHER.WithAlpha(0.5f * Alpha), IGraphics::CORNER_ALL, 1.5f);
			else
				Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		}
	}
	else
	{
		const char *apMarks[9] = {"X", "", "O", "", "X", "", "O", "", "X"};
		const float CellSize = Size / 3.0f;
		for(int i = 0; i < 9; i++)
		{
			CUIRect Cell;
			Cell.x = Grid.x + (i % 3) * CellSize;
			Cell.y = Grid.y + (i / 3) * CellSize;
			Cell.w = CellSize;
			Cell.h = CellSize;
			Cell.Margin(1.5f, &Cell);
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.12f * Alpha), IGraphics::CORNER_ALL, 2.0f);

			if(apMarks[i][0] == '\0')
				continue;
			TextRender()->TextColor((apMarks[i][0] == 'X' ? CMenus::AccentColor() : COLOR_MARK_OTHER).WithAlpha(Alpha));
			Ui()->DoLabel(&Cell, apMarks[i], CellSize * 0.7f, TEXTALIGN_MC);
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
}

void CMiniGames::RenderGameMenu()
{
	const float TileWidth = 100.0f;
	const float TileHeight = 112.0f;
	const float Gap = 12.0f;
	const float Margin = 14.0f;
	const float TitleHeight = 22.0f;
	const float HintHeight = 14.0f;
	const float Spacing = 8.0f;

	CUIRect Window = OpenWindow(
		2.0f * Margin + (int)NUM_GAMES * TileWidth + ((int)NUM_GAMES - 1) * Gap,
		2.0f * Margin + TitleHeight + HintHeight + Spacing + TileHeight);

	CUIRect Title, Hint, Row;
	Window.HSplitTop(TitleHeight, &Title, &Window);
	Ui()->DoLabel(&Title, Localize("Games"), 16.0f, TEXTALIGN_ML);
	Window.HSplitTop(HintHeight, &Hint, &Window);
	Ui()->DoLabel(&Hint, Localize("Only works when the other player is nearby and not in solo or a different team."), 9.0f, TEXTALIGN_ML);
	Window.HSplitTop(Spacing, nullptr, &Window);
	Window.HSplitTop(TileHeight, &Row, &Window);

	static CButtonContainer s_aGameButtons[NUM_GAMES];
	for(int Game = 0; Game < NUM_GAMES; Game++)
	{
		CUIRect Tile;
		Row.VSplitLeft(TileWidth, &Tile, &Row);
		Row.VSplitLeft(Gap, nullptr, &Row);

		const bool Hovered = Ui()->HotItem() == &s_aGameButtons[Game];
		Tile.Draw(Hovered ? CMenus::AccentColor().WithAlpha(0.3f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 6.0f);

		CUIRect Icon, Name;
		Tile.Margin(10.0f, &Icon);
		Icon.HSplitBottom(13.0f, &Icon, &Name);
		Icon.HSplitBottom(6.0f, &Icon, nullptr);
		RenderGameIcon(Game, Icon, 1.0f);
		Ui()->DoLabel(&Name, Localize(g_apGameNames[Game]), 10.0f, TEXTALIGN_MC);

		if(Ui()->DoButtonLogic(&s_aGameButtons[Game], 0, &Tile, BUTTONFLAG_LEFT))
		{
			m_Game = (EGame)Game;
			OpenSelect();
			return;
		}
	}
}

void CMiniGames::RenderSelectModal()
{
	CUIRect Window = OpenWindow(460.0f, 240.0f);

	CUIRect Title, Hint, Grid, ButtonRow;
	Window.HSplitTop(22.0f, &Title, &Window);
	Ui()->DoLabel(&Title, Localize(GameName()), 16.0f, TEXTALIGN_ML);
	int aPlayerIds[MAX_CLIENTS];
	const int NumPlayers = CollectPlayers(aPlayerIds);

	Window.HSplitTop(16.0f, &Hint, &Window);
	const char *pHint;
	if(GameClient()->m_MClientDetect.Refreshing())
		pHint = Localize("Looking for other M-Client players on this server...");
	else if(NumPlayers > 0)
		pHint = Localize("Pick the M-Client player you want to challenge.");
	else if(GameClient()->m_MClientDetect.Enabled())
		pHint = Localize("No other M-Client player answered on this server.");
	else
		pHint = Localize("There is nobody here to challenge.");
	Ui()->DoLabel(&Hint, pHint, 9.0f, TEXTALIGN_ML);
	Window.HSplitTop(6.0f, nullptr, &Window);
	Window.HSplitBottom(24.0f, &Grid, &ButtonRow);

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

	CUIRect ChallengeButton, RefreshButton, CancelButton;
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &ChallengeButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &RefreshButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, nullptr, &CancelButton);

	static CButtonContainer s_ChallengeButton;
	static CButtonContainer s_RefreshButton;
	static CButtonContainer s_CancelButton;

	const bool CanChallenge = m_SelectedId >= 0;
	ChallengeButton.Draw(CanChallenge ? (Ui()->HotItem() == &s_ChallengeButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&ChallengeButton, Localize("Challenge"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_ChallengeButton, 0, &ChallengeButton, BUTTONFLAG_LEFT) && CanChallenge)
	{
		Challenge(m_SelectedId);
		return;
	}

	RefreshButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_RefreshButton ? 0.2f : 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&RefreshButton, Localize("Refresh"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_RefreshButton, 0, &RefreshButton, BUTTONFLAG_LEFT))
		GameClient()->m_MClientDetect.Refresh();

	CancelButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_CancelButton ? 0.2f : 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&CancelButton, Localize("Cancel"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_CancelButton, 0, &CancelButton, BUTTONFLAG_LEFT))
		Close();
}

void CMiniGames::RenderStatusBar(float Alpha)
{
	const CUIRect Screen = *Ui()->Screen();

	CUIRect Bar;
	Bar.x = Screen.x;
	Bar.w = 132.0f;
	Bar.h = m_ShowKeyHint ? 46.0f : 36.0f;
	Bar.y = Screen.y + (Screen.h - Bar.h) / 2.0f;

	if(NeedsAttention())
	{
		const float Glow = TURN_GLOW_BASE + TURN_GLOW_PULSE * std::sin(LocalTime() * TURN_GLOW_SPEED);

		CUIRect Halo;
		Halo.x = Bar.x;
		Halo.y = Bar.y - 3.0f;
		Halo.w = Bar.w + 3.0f;
		Halo.h = Bar.h + 6.0f;
		Halo.Draw(CMenus::AccentColor().WithAlpha(Glow * Alpha), IGraphics::CORNER_R, 8.0f);
	}

	Bar.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, BarAlpha() * Alpha), IGraphics::CORNER_R, 5.0f);

	CUIRect Content;
	Bar.Margin(5.0f, &Content);

	SLabelProperties Props;
	Props.m_MaxWidth = Content.w;
	Props.m_EllipsisAtEnd = true;

	CUIRect Line;
	Content.HSplitTop(10.0f, &Line, &Content);
	TextRender()->TextColor(CMenus::AccentColor().WithAlpha(Alpha));
	TextRender()->TextOutlineColor(COLOR_OUTLINE.WithAlpha(Alpha));
	Ui()->DoLabel(&Line, Localize(GameName()), 8.0f, TEXTALIGN_ML, Props);
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
		else if(!g_Config.m_ClMClientMiniGamesHold)
			str_format(aHint, sizeof(aHint), Localize("Press %s to view"), aKey);
		else
			str_format(aHint, sizeof(aHint), Localize("Hold %s to view"), aKey);

		Content.HSplitTop(10.0f, &Line, &Content);
		TextRender()->TextColor(ColorRGBA(0.6f, 0.6f, 0.6f, Alpha));
		Ui()->DoLabel(&Line, aHint, 7.0f, TEXTALIGN_ML, Props);
	}

	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CMiniGames::RenderView(bool Interactive, float Alpha)
{
	const bool ShowBoard = m_HasBoard && (m_State == STATE_PLAYING || m_State == STATE_OVER);

	const CUIRect Screen = *Ui()->Screen();
	CUIRect Panel;
	Panel.w = 260.0f;
	Panel.h = 84.0f;
	if(ShowBoard)
	{
		switch(m_Game)
		{
		case GAME_CHESS:
			Panel.w = 280.0f;
			Panel.h = 320.0f;
			break;
		case GAME_BATTLESHIP:
			Panel.w = 330.0f;
			Panel.h = 240.0f;
			break;
		case GAME_MEMORY:
			Panel.w = 300.0f;
			Panel.h = 300.0f;
			break;
		default:
			Panel.w = 210.0f;
			Panel.h = 250.0f;
			break;
		}
	}
	Panel.x = Screen.x + (Screen.w - Panel.w) / 2.0f;
	Panel.y = Screen.y + (Screen.h - Panel.h) / 2.0f;
	Panel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, BackgroundAlpha() * Alpha), IGraphics::CORNER_ALL, 8.0f);

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
	Ui()->DoLabel(&Title, Localize(GameName()), 11.0f, TEXTALIGN_ML, Props);

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

void CMiniGames::RenderBoard(CUIRect Area, bool Interactive, float Alpha)
{
	if(m_Game == GAME_BATTLESHIP)
	{
		RenderBattleshipBoard(Area, Interactive, Alpha);
		return;
	}
	if(m_Game == GAME_MEMORY)
	{
		RenderMemoryBoard(Area, Interactive, Alpha);
		return;
	}

	const float BoardSize = std::min(Area.w, Area.h);
	CUIRect Grid;
	Grid.x = Area.x + (Area.w - BoardSize) / 2.0f;
	Grid.y = Area.y + (Area.h - BoardSize) / 2.0f;
	Grid.w = BoardSize;
	Grid.h = BoardSize;

	if(m_Game == GAME_CHESS)
		RenderChessBoard(Grid, Interactive, Alpha);
	else
		RenderTicTacToeBoard(Grid, Interactive, Alpha);
}

void CMiniGames::RenderTicTacToeBoard(CUIRect Grid, bool Interactive, float Alpha)
{
	const float BoardSize = Grid.w;
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

void CMiniGames::RenderBattleshipBoard(CUIRect Area, bool Interactive, float Alpha)
{
	CUIRect Enemy, Own, EnemyLabel, OwnLabel;
	Area.VSplitMid(&Enemy, &Own, 14.0f);
	Enemy.HSplitTop(11.0f, &EnemyLabel, &Enemy);
	Own.HSplitTop(11.0f, &OwnLabel, &Own);

	TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, Alpha));
	Ui()->DoLabel(&EnemyLabel, Localize("Enemy waters"), 8.0f, TEXTALIGN_MC);
	Ui()->DoLabel(&OwnLabel, Localize("Your waters"), 8.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	RenderWaterGrid(Enemy, false, Interactive, Alpha);
	RenderWaterGrid(Own, true, false, Alpha);
}

void CMiniGames::RenderWaterGrid(CUIRect Area, bool Own, bool Interactive, float Alpha)
{
	const float Size = std::min(Area.w, Area.h);
	CUIRect Grid;
	Grid.x = Area.x + (Area.w - Size) / 2.0f;
	Grid.y = Area.y + (Area.h - Size) / 2.0f;
	Grid.w = Size;
	Grid.h = Size;

	const char *pCells = Own ? m_aMyWaters : m_aFoeWaters;
	const bool CanShoot = Interactive && !Own && m_State == STATE_PLAYING && MyTurn() && !m_MovePending;
	static char s_aCellIds[100];
	const float CellSize = Size / 10.0f;
	for(int i = 0; i < 100; i++)
	{
		CUIRect Cell;
		Cell.x = Grid.x + (i % 10) * CellSize;
		Cell.y = Grid.y + (i / 10) * CellSize;
		Cell.w = CellSize;
		Cell.h = CellSize;
		Cell.Margin(0.7f, &Cell);

		const bool Targetable = CanShoot && pCells[i] == '.';
		if(Targetable && Ui()->DoButtonLogic(&s_aCellIds[i], 0, &Cell, BUTTONFLAG_LEFT))
		{
			Shoot(i);
			return;
		}

		if(pCells[i] == 'X')
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.9f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		else if(pCells[i] == 'o')
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.32f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		else if(pCells[i] == 'S')
			Cell.Draw(COLOR_MARK_OTHER.WithAlpha(0.55f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		else if(Targetable && Ui()->HotItem() == &s_aCellIds[i])
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f * Alpha), IGraphics::CORNER_ALL, 1.5f);
		else
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f * Alpha), IGraphics::CORNER_ALL, 1.5f);
	}
}

const CSkin *CMiniGames::MemorySkin(const MemoryCards::SFace &Face)
{
	if(Face.m_Stored >= 0 && Face.m_Stored < (int)m_vpMemorySkins.size() && m_vpMemorySkins[Face.m_Stored])
		return m_vpMemorySkins[Face.m_Stored].get();
	return GameClient()->m_Skins.Find(Face.m_pSkin ? Face.m_pSkin : "default");
}

void CMiniGames::RenderMemoryTee(const MemoryCards::SFace &Face, vec2 Pos, float Size, float Alpha)
{
	CTeeRenderInfo Info;
	Info.Apply(MemorySkin(Face));
	Info.ApplyColors(Face.m_CustomColors, Face.m_ColorBody, Face.m_ColorFeet);
	Info.m_Size = Size;
	RenderTools()->RenderIdleTeeAtCenter(&Info, Pos, EMOTE_NORMAL, vec2(1.0f, 0.0f), Alpha);
}

void CMiniGames::RenderMemoryBoard(CUIRect Area, bool Interactive, float Alpha)
{
	using namespace MemoryCards;

	CUIRect Score, Grid;
	Area.HSplitTop(13.0f, &Score, &Grid);
	Grid.HSplitTop(5.0f, nullptr, &Grid);

	CUIRect MineLabel, TheirsLabel;
	Score.VSplitMid(&MineLabel, &TheirsLabel);
	char aMine[64];
	char aTheirs[64];
	str_format(aMine, sizeof(aMine), "%s  %d", Localize("You"), MemoryScore(m_MyMark));
	str_format(aTheirs, sizeof(aTheirs), "%d  %s", MemoryScore(OtherMark()), OpponentName());
	SLabelProperties Props;
	Props.m_MaxWidth = TheirsLabel.w;
	Props.m_EllipsisAtEnd = true;
	const bool Running = m_State == STATE_PLAYING;
	TextRender()->TextColor(CMenus::AccentColor().WithAlpha((Running && !MyTurn() ? 0.6f : 1.0f) * Alpha));
	Ui()->DoLabel(&MineLabel, aMine, 10.0f, TEXTALIGN_ML, Props);
	TextRender()->TextColor(COLOR_MARK_OTHER.WithAlpha((Running && MyTurn() ? 0.6f : 1.0f) * Alpha));
	Ui()->DoLabel(&TheirsLabel, aTheirs, 10.0f, TEXTALIGN_MR, Props);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	const float CardSize = std::min(Grid.w / NUM_COLUMNS, Grid.h / NUM_ROWS);
	const float Left = Grid.x + (Grid.w - CardSize * NUM_COLUMNS) / 2.0f;
	const float Top = Grid.y + (Grid.h - CardSize * NUM_ROWS) / 2.0f;
	const float Now = LocalTime();
	const bool CanFlip = Interactive && Running && MyTurn() && Now >= m_MemoryPeekSecondAt;

	static char s_aCardIds[NUM_CARDS];
	for(int i = 0; i < NUM_CARDS; i++)
	{
		CUIRect Cell;
		Cell.x = Left + (i % NUM_COLUMNS) * CardSize;
		Cell.y = Top + (i / NUM_COLUMNS) * CardSize;
		Cell.w = CardSize;
		Cell.h = CardSize;
		Cell.Margin(2.5f, &Cell);

		const bool Clickable = CanFlip && m_aMemoryOwner[i] == '.' && i != m_MemoryPick && (m_MemoryPick < 0 || !m_MovePending);
		if(Clickable && Ui()->DoButtonLogic(&s_aCardIds[i], 0, &Cell, BUTTONFLAG_LEFT))
			MemoryClick(i);

		const bool FaceUp = MemoryCardFaceUp(i);
		if(FaceUp != m_aMemoryShown[i])
		{
			m_aMemoryShown[i] = FaceUp;
			m_aMemoryFlipAt[i] = Now;
		}
		const float Progress = std::clamp((Now - m_aMemoryFlipAt[i]) / MEMORY_FLIP_TIME, 0.0f, 1.0f);
		const bool ShowFace = Progress < 0.5f ? !FaceUp : FaceUp;
		const float Scale = std::fabs(1.0f - 2.0f * Progress);

		CUIRect Card = Cell;
		Card.w = Cell.w * Scale;
		Card.x = Cell.x + (Cell.w - Card.w) / 2.0f;
		if(Card.w < 1.0f)
			continue;

		if(!ShowFace)
		{
			const bool Hot = Clickable && Ui()->HotItem() == &s_aCardIds[i];
			Card.Draw(CMenus::AccentColor().WithAlpha((Hot ? 0.8f : 0.5f) * Alpha), IGraphics::CORNER_ALL, 4.0f);
			CUIRect Inner;
			Card.Margin(std::min(4.0f, Card.w * 0.15f), &Inner);
			Inner.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f * Alpha), IGraphics::CORNER_ALL, 3.0f);
			if(Scale > 0.6f)
			{
				TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.45f * Alpha));
				Ui()->DoLabel(&Card, "?", Cell.h * 0.42f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			continue;
		}

		const char Owner = m_aMemoryOwner[i];
		ColorRGBA Background = ColorRGBA(1.0f, 1.0f, 1.0f, i == m_MemoryPick ? 0.32f : 0.2f);
		if(Owner == m_MyMark)
			Background = CMenus::AccentColor().WithAlpha(0.35f);
		else if(Owner != '.')
			Background = COLOR_MARK_OTHER.WithAlpha(0.14f);
		Card.Draw(Background.WithMultipliedAlpha(Alpha), IGraphics::CORNER_ALL, 4.0f);

		if(Scale > 0.15f)
			RenderMemoryTee(m_aMemoryFaces[m_aMemoryCards[i]], Cell.Center() + vec2(0.0f, Cell.h * 0.04f), Cell.h * 0.74f * Scale, Alpha);
	}
}

void CMiniGames::RenderChessBoard(CUIRect Grid, bool Interactive, float Alpha)
{
	const bool Flip = !AmWhite();
	const bool MyMove = MyTurn();
	const float CellSize = Grid.w / 8.0f;

	static char s_aSquareIds[64];
	for(int Square = 0; Square < 64; Square++)
	{
		const int File = Square % 8;
		const int Rank = Square / 8;
		CUIRect Cell;
		Cell.x = Grid.x + (Flip ? 7 - File : File) * CellSize;
		Cell.y = Grid.y + (Flip ? 7 - Rank : Rank) * CellSize;
		Cell.w = CellSize;
		Cell.h = CellSize;

		const char Piece = m_Chess.Piece(Square);
		const bool CanPick = MyMove && CChessBoard::IsOwn(Piece, AmWhite());
		const bool CanMoveTo = MyMove && m_ChessSelected >= 0 && m_Chess.CanMove(m_ChessSelected, Square);
		if(Interactive && (CanPick || CanMoveTo) && Ui()->DoButtonLogic(&s_aSquareIds[Square], 0, &Cell, BUTTONFLAG_LEFT))
		{
			ChessClick(Square);
			return;
		}

		const bool Light = (File + Rank) % 2 == 0;
		if(Square == m_ChessSelected)
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.5f * Alpha), IGraphics::CORNER_NONE, 0.0f);
		else if(CanMoveTo)
			Cell.Draw(CMenus::AccentColor().WithAlpha(0.28f * Alpha), IGraphics::CORNER_NONE, 0.0f);
		else if(Interactive && CanPick && Ui()->HotItem() == &s_aSquareIds[Square])
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f * Alpha), IGraphics::CORNER_NONE, 0.0f);
		else
			Cell.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, (Light ? 0.16f : 0.05f) * Alpha), IGraphics::CORNER_NONE, 0.0f);

		if(Piece == '.')
			continue;

		TextRender()->TextColor((CChessBoard::IsWhite(Piece) ? COLOR_MARK_OTHER : CMenus::AccentColor()).WithAlpha(Alpha));
		TextRender()->TextOutlineColor(COLOR_OUTLINE.WithAlpha(Alpha));
		Ui()->DoLabel(&Cell, PieceGlyph(Piece), CellSize * 0.8f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	}
}

void CMiniGames::OnShutdown()
{
	for(auto &pSkin : m_vpMemorySkins)
	{
		if(!pSkin)
			continue;
		pSkin->m_OriginalSkin.Unload(Graphics());
		pSkin->m_ColorableSkin.Unload(Graphics());
	}
	m_vpMemorySkins.clear();
	m_MemorySkinsLoaded = false;
}

void CMiniGames::OnUpdate()
{
	FlushSendQueue();
}

void CMiniGames::OnRender()
{
	FlushEmoteQueue();

	if(g_Config.m_ClMClientMiniGamesHold && m_ViewActive && ViewKeyState() == 0)
		HideView();

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
		ResetEmoteChannel();
	}
	else if(m_State != STATE_SELECT && m_State != STATE_OVER && (m_OpponentId < 0 || !GameClient()->m_aClients[m_OpponentId].m_Active))
	{
		Finish(0, "Your opponent left the server.");
		ResetEmoteChannel();
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
	UpdateMoveRetry();

	if(m_State == STATE_OVER && !m_HasBoard && m_ViewActive && !m_KeyBlocked)
		OpenGames();

	if(m_State == STATE_GAMES || m_State == STATE_SELECT)
	{
		if(!WindowVisible())
			return;

		Ui()->MapScreen();
		Ui()->StartCheck();
		Ui()->Update();
		if(m_State == STATE_GAMES)
			RenderGameMenu();
		else
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

	const bool ViewVisible = ViewShown();
	const bool CursorShown = ViewVisible && m_CursorActive;
	const bool Interactive = CursorShown && !m_IgnoreClick;
	Ui()->MapScreen();
	if(ViewVisible)
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	RenderStatusBar(Alpha);
	if(ViewVisible)
		RenderView(Interactive, Alpha);

	if(ViewVisible)
	{
		if(CursorShown)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f * g_Config.m_ClMClientMenuCursorSize / 100.0f);
		Ui()->FinishCheck();
	}
}

void CMiniGames::OnRelease()
{
	m_ViewActive = false;
	m_CursorActive = false;
}

bool CMiniGames::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(m_State == STATE_GAMES || m_State == STATE_SELECT)
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

bool CMiniGames::OnInput(const IInput::CEvent &Event)
{
	if(m_State == STATE_GAMES || m_State == STATE_SELECT)
	{
		if(!WindowVisible())
			return false;

		if((Event.m_Flags & IInput::FLAG_PRESS) && Event.m_Key == KEY_ESCAPE)
		{
			if(m_State == STATE_SELECT)
				m_State = STATE_GAMES;
			else
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
