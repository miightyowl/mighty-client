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
	const char *g_apGameNames[CMiniGames::NUM_GAMES] = {"Tic tac toe", "Chess", "Battleship", "Tag"};

	const int g_aShipSizes[10] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
	const int SHIP_CELLS = 20;
	const float BATTLE_NOTE_TIME = 4.0f;

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
	const int EMOTE_OP_TAG = 15;
	const int TAG_FRAME_START = 0;
	const int TAG_FRAME_HIT = 1;
	const int TAG_FRAME_PAYLOAD = 8;
	const int TAG_COUNTDOWN_SECONDS = 5;
	const float TAG_HAMMER_HIT_RADIUS = 40.0f;
	const int TAG_START_MAX_RETRIES = 3;
	const int SEND_REPLACE_TAG_ROSTER = 1;

	const float EMOTE_ECHO_TIMEOUT = 2.0f;
	const int MAX_EMOTE_RETRIES = 4;
	const float MOVE_RETRY_INTERVAL = 6.0f;
	const int MAX_MOVE_RETRIES = 3;

	int FramePayload(int Op, int Game)
	{
		if(Op == EMOTE_OP_TAG)
			return Game == CMiniGames::GAME_TAG ? TAG_FRAME_PAYLOAD : -1;
		const bool Battle = Game == CMiniGames::GAME_BATTLESHIP;
		switch(Op)
		{
		case EMOTE_OP_MOVE:
			return Battle ? 3 : 2;
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
		if(pSelf->m_Game == GAME_TAG && pSelf->m_TagPhase == TAG_PHASE_RESULTS)
			pSelf->Close();
		else
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

void CMiniGames::ResetTag()
{
	m_TagPhase = TAG_PHASE_NONE;
	m_TagAdminId = -1;
	m_TagLobbyId = 0;
	m_TagNumPlayers = 0;
	m_TagRound = 0;
	m_TagRoundStartTick = 0;
	m_TagStartRetries = 0;
	m_TagStartRetryTime = 0.0f;
	m_TagStarting = false;
	m_TagStartFramePending = false;
	m_TagHitFramePending = false;
	m_TagPendingTimeCs = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		m_aTagSelected[ClientId] = false;
		m_aTagInvited[ClientId] = false;
		m_aTagAccepted[ClientId] = false;
		m_aTagReady[ClientId] = false;
		m_aTagTimeCs[ClientId] = -1;
	}
	for(int &ClientId : m_aTagOrder)
		ClientId = -1;
}

void CMiniGames::OnReset()
{
	SetEmoteGameShowAll(false);
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
	ResetTag();
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
	if(m_Game == GAME_TAG)
		return m_State == STATE_INVITED || m_TagPhase == TAG_PHASE_COUNTDOWN || m_TagPhase == TAG_PHASE_RUNNING;
	return m_State == STATE_INVITED || MyTurn();
}

bool CMiniGames::ViewShown() const
{
	if(m_Game == GAME_TAG)
		return m_State == STATE_INVITED || (m_State == STATE_TAG_LOBBY && m_ViewActive) || (m_TagPhase == TAG_PHASE_RESULTS && m_ViewActive);
	if(m_State == STATE_INVITED || m_State == STATE_PLAYING)
		return m_ViewActive;
	return m_ViewActive && m_State == STATE_OVER && m_HasBoard;
}

bool CMiniGames::MyTurn() const
{
	if(m_State != STATE_PLAYING)
		return false;
	if(m_Game == GAME_TAG)
		return false;
	if(m_Game == GAME_CHESS)
		return m_Chess.WhiteToMove() == AmWhite();
	if(m_Game == GAME_BATTLESHIP)
		return m_BattleMyTurn;
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
	case STATE_TAG_LOBBY:
		if(m_TagStarting)
			str_copy(pBuf, Localize("Starting when everyone is ready..."), Size);
		else if(IsTagAdmin())
			str_format(pBuf, Size, Localize("%d players joined. Start with 3 or more."), NumTagAccepted());
		else
			str_format(pBuf, Size, Localize("Waiting for %s to start..."), OpponentName());
		break;
	case STATE_PLAYING:
		if(m_Game == GAME_TAG)
		{
			const int TargetId = TagTargetId();
			if(m_TagPhase == TAG_PHASE_COUNTDOWN)
				str_copy(pBuf, Localize("Get ready..."), Size);
			else if(TargetId == GameClient()->m_Snap.m_LocalClientId)
				str_copy(pBuf, Localize("Run! Avoid the hammers."), Size);
			else
				str_format(pBuf, Size, Localize("Hammer %s!"), TargetId >= 0 ? GameClient()->m_aClients[TargetId].m_aName : "");
			break;
		}
		if(m_Game == GAME_BATTLESHIP && m_BattleNote != NOTE_NONE && LocalTime() - m_BattleNoteTime < BATTLE_NOTE_TIME)
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
			default:
				str_copy(pBuf, Localize("One of your ships went down."), Size);
				break;
			}
		}
		else if(!MyTurn())
			str_format(pBuf, Size, Localize("%s is thinking..."), OpponentName());
		else if(m_Game == GAME_BATTLESHIP)
			str_copy(pBuf, Localize("Your turn, pick a target"), Size);
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
	if(m_Game == GAME_TAG && m_State == STATE_INVITED)
		return;
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
	for(bool &Selected : m_aTagSelected)
		Selected = false;
	m_State = STATE_SELECT;
	if(GameClient()->m_MClientDetect.NumDetected() == 0)
		GameClient()->m_MClientDetect.Refresh();
}

void CMiniGames::Close()
{
	if(m_Game == GAME_TAG && m_TagAdminId >= 0 && m_TagPhase != TAG_PHASE_RESULTS)
	{
		if(IsTagAdmin())
		{
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			{
				if(ClientId != m_TagAdminId && m_aTagInvited[ClientId])
					SendTo(ClientId, "Q");
			}
		}
		else if(m_State == STATE_INVITED)
			SendTo(m_TagAdminId, "D");
		else
			SendTo(m_TagAdminId, "Q");
	}
	else if(m_State == STATE_PLAYING || m_State == STATE_CALLING || m_State == STATE_RINGING)
		SendTo(m_OpponentId, "Q");
	else if(m_State == STATE_INVITED)
		SendTo(m_OpponentId, "D");
	SetEmoteGameShowAll(false);

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
	ResetTag();
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
	char aMessage[16];
	str_format(aMessage, sizeof(aMessage), "C %d", (int)m_Game);
	SendProtocol(aMessage, MAX_CHALLENGE_RETRIES);
}

bool CMiniGames::IsTagAdmin() const
{
	return m_TagAdminId >= 0 && m_TagAdminId == GameClient()->m_Snap.m_LocalClientId;
}

int CMiniGames::NumTagAccepted() const
{
	int Count = 0;
	for(bool Accepted : m_aTagAccepted)
		Count += Accepted ? 1 : 0;
	return Count;
}

int CMiniGames::TagTargetId() const
{
	if(m_TagRound < 0 || m_TagRound >= m_TagNumPlayers)
		return -1;
	return m_aTagOrder[m_TagRound];
}

bool CMiniGames::IsTagTarget(int ClientId) const
{
	return m_Game == GAME_TAG && m_State == STATE_PLAYING && m_TagPhase == TAG_PHASE_RUNNING && !m_TagHitFramePending && ClientId == TagTargetId();
}

void CMiniGames::ChallengeTag()
{
	int aInvitees[TAG_MAX_PLAYERS - 1];
	int NumInvitees = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS && NumInvitees < TAG_MAX_PLAYERS - 1; ClientId++)
	{
		if(m_aTagSelected[ClientId] && GameClient()->m_aClients[ClientId].m_Active)
			aInvitees[NumInvitees++] = ClientId;
	}
	if(NumInvitees < 2)
		return;

	ResetTag();
	m_LocalId = GameClient()->m_Snap.m_LocalClientId;
	m_OpponentId = -1;
	m_TagAdminId = m_LocalId;
	m_TagLobbyId = secure_rand_below(999999) + 1;
	m_TagPhase = TAG_PHASE_LOBBY;
	m_aTagAccepted[m_LocalId] = true;
	m_aTagInvited[m_LocalId] = true;
	m_State = STATE_TAG_LOBBY;
	m_ViewActive = true;
	m_CursorActive = false;
	m_HasBoard = false;

	char aMessage[32];
	str_format(aMessage, sizeof(aMessage), "C %d %d", (int)GAME_TAG, m_TagLobbyId);
	for(int i = 0; i < NumInvitees; i++)
	{
		m_aTagInvited[aInvitees[i]] = true;
		SendTo(aInvitees[i], aMessage);
	}
}

bool CMiniGames::ParseTagRoster(const char *pText, int *pIds, int &NumIds, int &LobbyId) const
{
	char aToken[16];
	const char *pRest = str_next_token(pText, " ", aToken, sizeof(aToken));
	if(!str_toint(aToken, &LobbyId) || LobbyId <= 0 || !pRest)
		return false;

	pRest = str_next_token(pRest, " ", aToken, sizeof(aToken));
	if(!str_toint(aToken, &NumIds) || NumIds < 1 || NumIds > TAG_MAX_PLAYERS)
		return false;

	bool aSeen[MAX_CLIENTS] = {false};
	for(int i = 0; i < NumIds; i++)
	{
		pRest = str_next_token(pRest, " ", aToken, sizeof(aToken));
		int ClientId;
		if(!str_toint(aToken, &ClientId) || ClientId < 0 || ClientId >= MAX_CLIENTS || aSeen[ClientId])
			return false;
		aSeen[ClientId] = true;
		pIds[i] = ClientId;
		if(i + 1 < NumIds && !pRest)
			return false;
	}
	return true;
}

void CMiniGames::SendTagRoster(char Verb, int PriorityClientId)
{
	int aIds[TAG_MAX_PLAYERS];
	int NumIds = 0;
	if(Verb == 'S')
	{
		NumIds = m_TagNumPlayers;
		for(int i = 0; i < NumIds; i++)
			aIds[i] = m_aTagOrder[i];
	}
	else
	{
		if(m_TagAdminId >= 0 && m_aTagAccepted[m_TagAdminId])
			aIds[NumIds++] = m_TagAdminId;
		for(int ClientId = 0; ClientId < MAX_CLIENTS && NumIds < TAG_MAX_PLAYERS; ClientId++)
		{
			if(ClientId != m_TagAdminId && m_aTagAccepted[ClientId])
				aIds[NumIds++] = ClientId;
		}
	}

	char aMessage[192];
	str_format(aMessage, sizeof(aMessage), "%c %d %d", Verb, m_TagLobbyId, NumIds);
	for(int i = 0; i < NumIds; i++)
	{
		char aId[8];
		str_format(aId, sizeof(aId), " %d", aIds[i]);
		str_append(aMessage, aId, sizeof(aMessage));
	}
	if(PriorityClientId != m_TagAdminId && PriorityClientId >= 0 && PriorityClientId < MAX_CLIENTS && m_aTagAccepted[PriorityClientId])
		SendTo(PriorityClientId, aMessage, true, SEND_REPLACE_TAG_ROSTER);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId != m_TagAdminId && ClientId != PriorityClientId && m_aTagAccepted[ClientId])
			SendTo(ClientId, aMessage, false, SEND_REPLACE_TAG_ROSTER);
	}
}

void CMiniGames::StartTag()
{
	if(!IsTagAdmin() || m_TagStarting || NumTagAccepted() < 3)
		return;

	m_TagNumPlayers = 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS && m_TagNumPlayers < TAG_MAX_PLAYERS; ClientId++)
	{
		if(m_aTagAccepted[ClientId])
			m_aTagOrder[m_TagNumPlayers++] = ClientId;
	}
	for(int i = m_TagNumPlayers - 1; i > 0; i--)
	{
		const int Swap = secure_rand_below(i + 1);
		std::swap(m_aTagOrder[i], m_aTagOrder[Swap]);
	}
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId != m_TagAdminId && m_aTagInvited[ClientId] && !m_aTagAccepted[ClientId])
		{
			SendTo(ClientId, "Q");
			m_aTagInvited[ClientId] = false;
		}
	}

	for(bool &Ready : m_aTagReady)
		Ready = false;
	m_aTagReady[m_TagAdminId] = true;
	m_TagStarting = true;
	m_TagStartRetries = 0;
	m_TagStartRetryTime = LocalTime() + RETRY_INTERVAL;
	SendTagRoster('S');
}

void CMiniGames::BeginTagCountdown()
{
	m_State = STATE_PLAYING;
	m_TagPhase = TAG_PHASE_COUNTDOWN;
	m_TagRound = 0;
	m_TagRoundStartTick = Client()->GameTick(g_Config.m_ClDummy) + TAG_COUNTDOWN_SECONDS * Client()->GameTickSpeed();
	m_TagStarting = false;
	m_ViewActive = true;
	m_CursorActive = false;
	m_HasBoard = false;
	for(int &Time : m_aTagTimeCs)
		Time = -1;
	SetEmoteGameShowAll(true);
}

void CMiniGames::UpdateTagLobby()
{
	if(m_Game != GAME_TAG)
		return;

	if(m_State == STATE_PLAYING && m_TagPhase == TAG_PHASE_COUNTDOWN && Client()->GameTick(g_Config.m_ClDummy) >= m_TagRoundStartTick)
	{
		m_TagPhase = TAG_PHASE_RUNNING;
		m_TagRoundStartTick = Client()->GameTick(g_Config.m_ClDummy);
		return;
	}
	if(m_State == STATE_PLAYING && IsTagAdmin())
	{
		for(int i = 0; i < m_TagNumPlayers; i++)
		{
			const int ClientId = m_aTagOrder[i];
			if(GameClient()->m_aClients[ClientId].m_Active)
				continue;
			for(int OtherId = 0; OtherId < MAX_CLIENTS; OtherId++)
			{
				if(OtherId != m_TagAdminId && m_aTagAccepted[OtherId])
					SendTo(OtherId, "Q");
			}
			Finish(0, "A player left the Tag game.");
			ResetEmoteChannel();
			return;
		}
	}

	if(m_State != STATE_TAG_LOBBY || !IsTagAdmin())
		return;

	bool RosterChanged = false;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(ClientId == m_TagAdminId || !m_aTagInvited[ClientId] || GameClient()->m_aClients[ClientId].m_Active)
			continue;
		RosterChanged |= m_aTagAccepted[ClientId];
		m_aTagInvited[ClientId] = false;
		m_aTagAccepted[ClientId] = false;
		m_aTagReady[ClientId] = false;
	}
	if(RosterChanged)
	{
		if(m_TagStarting)
		{
			ResetEmoteChannel();
			m_TagStartFramePending = false;
			m_TagStarting = false;
			for(bool &Ready : m_aTagReady)
				Ready = false;
		}
		SendTagRoster('L');
	}

	if(!m_TagStarting)
		return;

	bool EveryoneReady = true;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(m_aTagAccepted[ClientId] && !m_aTagReady[ClientId])
		{
			EveryoneReady = false;
			break;
		}
	}
	if(EveryoneReady)
	{
		if(!m_TagStartFramePending)
		{
			m_TagStartFramePending = true;
			SendTagFrame(TAG_FRAME_START, 0, 0);
		}
		return;
	}

	if(LocalTime() < m_TagStartRetryTime)
		return;
	if(!m_vSendQueue.empty())
	{
		m_TagStartRetryTime = LocalTime() + SEND_INTERVAL;
		return;
	}
	m_TagStartRetries++;
	if(m_TagStartRetries > TAG_START_MAX_RETRIES)
	{
		m_TagStarting = false;
		for(bool &Ready : m_aTagReady)
			Ready = false;
		SendTagRoster('L');
		return;
	}
	SendTagRoster('S');
	m_TagStartRetryTime = LocalTime() + RETRY_INTERVAL;
}

void CMiniGames::UpdateTagHitDetection()
{
	if(m_Game != GAME_TAG || m_State != STATE_PLAYING)
		return;

	const int TargetId = TagTargetId();
	if(!(IsTagAdmin() && m_TagPhase == TAG_PHASE_RUNNING && !m_TagHitFramePending && TargetId >= 0 && GameClient()->m_Snap.m_aCharacters[TargetId].m_Active))
		return;

	const auto &Target = GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur;
	const vec2 TargetPos = vec2(Target.m_X, Target.m_Y);

	bool Hit = false;
	const int Num = Client()->SnapNumItems(IClient::SNAP_CURRENT);
	for(int Index = 0; Index < Num; Index++)
	{
		const IClient::CSnapItem Item = Client()->SnapGetItem(IClient::SNAP_CURRENT, Index);
		if(Item.m_Type != NETEVENTTYPE_HAMMERHIT)
			continue;
		const CNetEvent_HammerHit *pEvent = (const CNetEvent_HammerHit *)Item.m_pData;
		if(distance(vec2(pEvent->m_X, pEvent->m_Y), TargetPos) <= TAG_HAMMER_HIT_RADIUS)
		{
			Hit = true;
			break;
		}
	}
	if(!Hit)
		return;

	const int ElapsedTicks = std::max(0, Client()->GameTick(g_Config.m_ClDummy) - m_TagRoundStartTick);
	const int TimeCs = (ElapsedTicks * 100 + Client()->GameTickSpeed() / 2) / Client()->GameTickSpeed();
	FinishTagRound(TimeCs, true);
}

void CMiniGames::SendTagFrame(int Type, int Round, int TimeCs)
{
	int aDigits[TAG_FRAME_PAYLOAD];
	aDigits[0] = Type;
	aDigits[1] = (Round / EMOTE_RADIX) % EMOTE_RADIX;
	aDigits[2] = Round % EMOTE_RADIX;
	for(int i = TAG_FRAME_PAYLOAD - 1; i >= 3; i--)
	{
		aDigits[i] = TimeCs % EMOTE_RADIX;
		TimeCs /= EMOTE_RADIX;
	}
	SendFrame(EMOTE_OP_TAG, aDigits, TAG_FRAME_PAYLOAD);
}

void CMiniGames::FinishTagRound(int TimeCs, bool Broadcast)
{
	const int TargetId = TagTargetId();
	if(TargetId < 0 || m_TagPhase != TAG_PHASE_RUNNING)
		return;

	TimeCs = std::clamp(TimeCs, 0, 248831);
	if(Broadcast)
	{
		SendTagFrame(TAG_FRAME_HIT, m_TagRound, TimeCs);
		m_TagHitFramePending = true;
		m_TagPendingTimeCs = TimeCs;
		return;
	}
	m_aTagTimeCs[TargetId] = TimeCs;
	m_TagRound++;

	if(m_TagRound >= m_TagNumPlayers)
	{
		m_TagPhase = TAG_PHASE_RESULTS;
		m_State = STATE_OVER;
		m_ViewActive = true;
		m_CursorActive = false;
		m_FadeTime = 0.0f;
		int BestId = -1;
		for(int i = 0; i < m_TagNumPlayers; i++)
		{
			const int ClientId = m_aTagOrder[i];
			if(BestId < 0 || m_aTagTimeCs[ClientId] > m_aTagTimeCs[BestId])
				BestId = ClientId;
		}
		m_Result = BestId == GameClient()->m_Snap.m_LocalClientId ? 'W' : 'L';
		str_copy(m_aStatus, BestId >= 0 ? GameClient()->m_aClients[BestId].m_aName : "");
		return;
	}

	m_TagPhase = TAG_PHASE_COUNTDOWN;
	m_TagRoundStartTick = Client()->GameTick(g_Config.m_ClDummy) + TAG_COUNTDOWN_SECONDS * Client()->GameTickSpeed();
}

void CMiniGames::ProcessTagFrame()
{
	const int Type = m_aFrame[0];
	const int Round = m_aFrame[1] * EMOTE_RADIX + m_aFrame[2];
	int TimeCs = 0;
	for(int i = 3; i < TAG_FRAME_PAYLOAD; i++)
		TimeCs = TimeCs * EMOTE_RADIX + m_aFrame[i];

	if(Type == TAG_FRAME_START && m_State == STATE_TAG_LOBBY && !IsTagAdmin() && m_TagNumPlayers >= 3)
		BeginTagCountdown();
	else if(Type == TAG_FRAME_HIT && m_State == STATE_PLAYING && m_TagPhase == TAG_PHASE_RUNNING && Round == m_TagRound)
		FinishTagRound(TimeCs, false);
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
	m_HasBoard = true;
	m_State = STATE_PLAYING;
	SetEmoteGameShowAll(true);
	ClearRetry();
	ResetEmoteChannel();
}

void CMiniGames::QueueWhisper(int ClientId, const char *pMessage, bool Priority, int ReplaceKey)
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
		m_vSendQueue.erase(std::remove_if(m_vSendQueue.begin(), m_vSendQueue.end(), [ClientId, ReplaceKey](const CQueuedWhisper &Queued) {
			return Queued.m_ClientId == ClientId && Queued.m_ReplaceKey == ReplaceKey;
		}), m_vSendQueue.end());
	}
	CQueuedWhisper Queued{ClientId, ReplaceKey, aLine};
	if(Priority)
		m_vSendQueue.insert(m_vSendQueue.begin(), std::move(Queued));
	else
		m_vSendQueue.push_back(std::move(Queued));
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

	if(Now < m_NextSendTime)
		return;

	if(m_ChatScore + CHAT_SCORE_PENALTY > CHAT_SCORE_BUDGET)
		return;

	GameClient()->m_Chat.SendChat(0, m_vSendQueue.front().m_Command.c_str());
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

void CMiniGames::SetEmoteGameShowAll(bool Enable)
{
	if(m_ShowAllForEmoteGame == Enable)
		return;

	m_ShowAllForEmoteGame = Enable;
	if(Client()->State() == IClient::STATE_ONLINE)
		GameClient()->m_Chat.SendChat(0, Enable ? "/showall 1" : "/showall 0");
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
			const bool TagStartFailed = m_TagStartFramePending;
			const bool TagHitFailed = m_TagHitFramePending;
			ResetEmoteChannel();
			if(TagStartFailed)
			{
				m_TagStartFramePending = false;
				m_TagStarting = false;
				for(bool &Ready : m_aTagReady)
					Ready = false;
				SendTagRoster('L');
			}
			else if(TagHitFailed)
			{
				m_TagHitFramePending = false;
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
				{
					if(ClientId != m_TagAdminId && m_aTagAccepted[ClientId])
						SendTo(ClientId, "Q");
				}
				Finish(0, "Stopped, this server does not pass emoticons through.");
				m_ViewActive = false;
			}
			else if(Running)
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
	if(Protocol && m_Game == GAME_TAG && (m_vEmoteQueue.empty() || !m_vEmoteQueue.front().m_Protocol))
	{
		if(m_TagStartFramePending)
		{
			m_TagStartFramePending = false;
			BeginTagCountdown();
		}
		else if(m_TagHitFramePending)
		{
			m_TagHitFramePending = false;
			FinishTagRound(m_TagPendingTimeCs, false);
		}
	}
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
	if(m_FrameOp == EMOTE_OP_TAG)
	{
		ProcessTagFrame();
		return;
	}

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
	if(m_Game == GAME_TAG && ClientId == m_TagAdminId && (m_State == STATE_TAG_LOBBY || m_State == STATE_PLAYING || m_State == STATE_OVER))
		return HandleEmoteFrame(Emoticon);
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

bool CMiniGames::CheckGameEnd()
{
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
	if(m_State != STATE_CALLING && m_State != STATE_RINGING && m_State != STATE_INVITED && m_State != STATE_TAG_LOBBY)
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
		char aGame[16];
		const char *pGameRest = str_next_token(pRest, " ", aGame, sizeof(aGame));
		int Game;
		if(!str_toint(aGame, &Game) || Game < 0 || Game >= NUM_GAMES)
		{
			SendTo(ClientId, "D");
			break;
		}

		int TagLobbyId = 0;
		if(Game == GAME_TAG && (!pGameRest || !str_toint(pGameRest, &TagLobbyId) || TagLobbyId <= 0))
		{
			SendTo(ClientId, "D");
			break;
		}

		const bool Busy = m_State == STATE_CALLING || m_State == STATE_RINGING || m_State == STATE_INVITED || m_State == STATE_TAG_LOBBY || m_State == STATE_PLAYING;
		const bool Local = ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
		if(Game == GAME_TAG && m_Game == GAME_TAG && ClientId == m_TagAdminId && TagLobbyId == m_TagLobbyId && m_State == STATE_INVITED)
		{
			char aReady[32];
			str_format(aReady, sizeof(aReady), "R %d", m_TagLobbyId);
			SendTo(ClientId, aReady);
		}
		else if(FromOpponent && m_State == STATE_INVITED)
		{
			SendTo(ClientId, "R");
		}
		else if(!g_Config.m_ClMClientMiniGames || Busy || Local)
		{
			SendTo(ClientId, "D");
		}
		else
		{
			ClearRetry();
			ResetTag();
			m_Game = (EGame)Game;
			m_OpponentId = ClientId;
			m_LocalId = GameClient()->m_Snap.m_LocalClientId;
			m_MyMark = 'O';
			m_HasBoard = false;
			m_Result = 0;
			m_aStatus[0] = '\0';
			m_State = STATE_INVITED;
			m_AcceptDeadline = LocalTime() + ACCEPT_TIMEOUT;
			if(m_Game == GAME_TAG)
			{
				m_TagAdminId = ClientId;
				m_TagLobbyId = TagLobbyId;
				m_TagPhase = TAG_PHASE_LOBBY;
				m_aTagInvited[ClientId] = true;
				m_aTagInvited[m_LocalId] = true;
				m_aTagAccepted[ClientId] = true;
				m_ViewActive = true;
				m_CursorActive = false;
				char aReady[32];
				str_format(aReady, sizeof(aReady), "R %d", m_TagLobbyId);
				SendTo(ClientId, aReady);
			}
			else
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
		if(m_Game == GAME_TAG && IsTagAdmin() && m_State == STATE_TAG_LOBBY && !m_TagStarting)
		{
			int LobbyId;
			if(str_toint(pRest, &LobbyId) && LobbyId == m_TagLobbyId && m_aTagInvited[ClientId] && (m_aTagAccepted[ClientId] || NumTagAccepted() < TAG_MAX_PLAYERS))
			{
				m_aTagAccepted[ClientId] = true;
				SendTagRoster('L', ClientId);
			}
		}
		else if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING))
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
		if(m_Game == GAME_TAG && IsTagAdmin() && m_State == STATE_TAG_LOBBY && m_aTagInvited[ClientId])
		{
			const bool WasAccepted = m_aTagAccepted[ClientId];
			m_aTagInvited[ClientId] = false;
			m_aTagAccepted[ClientId] = false;
			m_aTagReady[ClientId] = false;
			if(WasAccepted && !m_TagStarting)
				SendTagRoster('L');
		}
		else if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING))
		{
			char aStatus[128];
			str_format(aStatus, sizeof(aStatus), "%s declined the challenge.", OpponentName());
			Finish(0, aStatus);
		}
		break;

	case 'L':
	case 'S':
		if(m_Game == GAME_TAG && ClientId == m_TagAdminId && (m_State == STATE_TAG_LOBBY || m_State == STATE_INVITED))
		{
			int aIds[TAG_MAX_PLAYERS];
			int NumIds = 0;
			int LobbyId = 0;
			if(!ParseTagRoster(pRest, aIds, NumIds, LobbyId) || LobbyId != m_TagLobbyId)
				break;
			bool HasLocal = false;
			for(int i = 0; i < NumIds; i++)
				HasLocal |= aIds[i] == m_LocalId;
			if(!HasLocal)
				break;

			for(bool &Accepted : m_aTagAccepted)
				Accepted = false;
			for(bool &Invited : m_aTagInvited)
				Invited = false;
			for(int i = 0; i < NumIds; i++)
			{
				m_aTagAccepted[aIds[i]] = true;
				m_aTagInvited[aIds[i]] = true;
			}
			m_State = STATE_TAG_LOBBY;
			m_TagPhase = TAG_PHASE_LOBBY;
			m_TagStarting = Verb == 'S';
			m_ViewActive = true;
			m_CursorActive = false;
			if(Verb == 'S')
			{
				m_TagNumPlayers = NumIds;
				for(int i = 0; i < NumIds; i++)
					m_aTagOrder[i] = aIds[i];
				char aAck[32];
				str_format(aAck, sizeof(aAck), "K %d", m_TagLobbyId);
				SendTo(m_TagAdminId, aAck);
			}
		}
		break;

	case 'K':
		if(m_Game == GAME_TAG && IsTagAdmin() && m_State == STATE_TAG_LOBBY && m_TagStarting)
		{
			int LobbyId;
			if(str_toint(pRest, &LobbyId) && LobbyId == m_TagLobbyId && m_aTagAccepted[ClientId])
				m_aTagReady[ClientId] = true;
		}
		else if(FromOpponent)
			ClearRetry();
		break;

	case 'Q':
		if(m_Game == GAME_TAG && m_TagAdminId >= 0)
		{
			if(ClientId == m_TagAdminId && !IsTagAdmin() && (m_State == STATE_INVITED || m_State == STATE_TAG_LOBBY || m_State == STATE_PLAYING))
			{
				Finish(0, "The Tag lobby was closed by its admin.");
				ResetEmoteChannel();
			}
			else if(IsTagAdmin() && m_aTagAccepted[ClientId])
			{
				m_aTagAccepted[ClientId] = false;
				m_aTagInvited[ClientId] = false;
				m_aTagReady[ClientId] = false;
				if(m_State == STATE_TAG_LOBBY)
				{
					if(m_TagStarting)
					{
						ResetEmoteChannel();
						m_TagStartFramePending = false;
						m_TagStarting = false;
						for(bool &Ready : m_aTagReady)
							Ready = false;
					}
					SendTagRoster('L');
				}
				else if(m_State == STATE_PLAYING)
				{
					for(int OtherId = 0; OtherId < MAX_CLIENTS; OtherId++)
					{
						if(OtherId != m_TagAdminId && m_aTagAccepted[OtherId])
							SendTo(OtherId, "Q");
					}
					Finish(0, "A player left the Tag game.");
					ResetEmoteChannel();
				}
			}
		}
		else if(FromOpponent && (m_State == STATE_CALLING || m_State == STATE_RINGING || m_State == STATE_INVITED || m_State == STATE_PLAYING))
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
	if(Game == GAME_TAG)
	{
		const vec2 Center = Grid.Center();
		const ColorRGBA Accent = CMenus::AccentColor();
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Accent.r, Accent.g, Accent.b, 0.2f * Alpha);
		Graphics()->DrawCircle(Center.x + Size * 0.2f, Center.y - Size * 0.05f, Size * 0.24f, 32);
		Graphics()->SetColor(Accent.r, Accent.g, Accent.b, 0.95f * Alpha);
		Graphics()->DrawCircle(Center.x + Size * 0.2f, Center.y - Size * 0.05f, Size * 0.14f, 32);
		Graphics()->SetColor(0.95f, 0.95f, 0.95f, 0.8f * Alpha);
		Graphics()->DrawCircle(Center.x - Size * 0.24f, Center.y - Size * 0.22f, Size * 0.09f, 24);
		Graphics()->DrawCircle(Center.x - Size * 0.27f, Center.y + Size * 0.2f, Size * 0.09f, 24);
		Graphics()->QuadsEnd();

		CUIRect Mark = {Center.x - Size * 0.02f, Center.y - Size * 0.2f, Size * 0.44f, Size * 0.3f};
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Alpha));
		Ui()->DoLabel(&Mark, "!", Size * 0.22f, TEXTALIGN_MC);
	}
	else if(Game == GAME_CHESS)
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
		pHint = m_Game == GAME_TAG ? Localize("Pick at least two players to invite to your Tag lobby.") : Localize("Pick the M-Client player you want to challenge.");
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
		{
			if(m_Game == GAME_TAG)
			{
				if(m_aTagSelected[ClientId])
					m_aTagSelected[ClientId] = false;
				else
				{
					int SelectedCount = 0;
					for(bool Selected : m_aTagSelected)
						SelectedCount += Selected ? 1 : 0;
					if(SelectedCount < TAG_MAX_PLAYERS - 1)
						m_aTagSelected[ClientId] = true;
				}
			}
			else
				m_SelectedId = m_SelectedId == ClientId ? -1 : ClientId;
		}

		const bool Selected = m_Game == GAME_TAG ? m_aTagSelected[ClientId] : m_SelectedId == ClientId;
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

	int SelectedCount = 0;
	for(bool Selected : m_aTagSelected)
		SelectedCount += Selected ? 1 : 0;
	const bool CanChallenge = m_Game == GAME_TAG ? SelectedCount >= 2 : m_SelectedId >= 0;
	ChallengeButton.Draw(CanChallenge ? (Ui()->HotItem() == &s_ChallengeButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&ChallengeButton, m_Game == GAME_TAG ? Localize("Create lobby") : Localize("Challenge"), 11.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_ChallengeButton, 0, &ChallengeButton, BUTTONFLAG_LEFT) && CanChallenge)
	{
		if(m_Game == GAME_TAG)
			ChallengeTag();
		else
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

void CMiniGames::RenderTagLobby(bool Interactive)
{
	CUIRect Window = OpenWindow(380.0f, 286.0f);
	CUIRect Title, Status, Players, ButtonRow;
	Window.HSplitTop(22.0f, &Title, &Window);
	Window.HSplitTop(18.0f, &Status, &Window);
	Window.HSplitTop(8.0f, nullptr, &Window);
	Window.HSplitBottom(24.0f, &Players, &ButtonRow);
	Players.HSplitBottom(8.0f, &Players, nullptr);

	Ui()->DoLabel(&Title, Localize("Tag lobby"), 16.0f, TEXTALIGN_ML);
	char aStatus[128];
	StatusText(aStatus, sizeof(aStatus));
	TextRender()->TextColor(ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f));
	Ui()->DoLabel(&Status, aStatus, 9.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 18.0f;
	static CScrollRegion s_TagLobbyScroll;
	s_TagLobbyScroll.Begin(&Players, &ScrollParams);
	int RowIndex = 0;
	for(int Pass = 0; Pass < 2; Pass++)
	{
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			if(!m_aTagInvited[ClientId] || (Pass == 0) != m_aTagAccepted[ClientId])
				continue;
			CUIRect Row;
			Players.HSplitTop(18.0f, &Row, &Players);
			s_TagLobbyScroll.AddRect(Row);
			if(RowIndex++ % 2 == 0)
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_ALL, 3.0f);

			CUIRect State, Name;
			Row.VSplitLeft(64.0f, &State, &Name);
			TextRender()->TextColor(m_aTagAccepted[ClientId] ? ColorRGBA(0.45f, 1.0f, 0.55f, 1.0f) : ColorRGBA(0.65f, 0.65f, 0.65f, 1.0f));
			Ui()->DoLabel(&State, m_aTagAccepted[ClientId] ? Localize("Ready") : Localize("Invited"), 8.0f, TEXTALIGN_ML);
			TextRender()->TextColor(ClientId == m_TagAdminId ? CMenus::AccentColor() : TextRender()->DefaultTextColor());
			char aName[MAX_NAME_LENGTH + 16];
			str_format(aName, sizeof(aName), ClientId == m_TagAdminId ? "%s (admin)" : "%s", GameClient()->m_aClients[ClientId].m_aName);
			Ui()->DoLabel(&Name, aName, 9.0f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
	s_TagLobbyScroll.End();
	if(!Interactive)
	{
		TextRender()->TextColor(ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel(&ButtonRow, Localize("Left click to use the mouse"), 8.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	CUIRect LeaveButton, StartButton;
	ButtonRow.VSplitRight(90.0f, &ButtonRow, &LeaveButton);
	ButtonRow.VSplitRight(8.0f, &ButtonRow, nullptr);
	ButtonRow.VSplitRight(90.0f, nullptr, &StartButton);
	static CButtonContainer s_TagLeaveButton;
	static CButtonContainer s_TagStartButton;
	LeaveButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_TagLeaveButton ? 0.25f : 0.12f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&LeaveButton, IsTagAdmin() ? Localize("Cancel") : Localize("Leave"), 10.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_TagLeaveButton, 0, &LeaveButton, BUTTONFLAG_LEFT))
	{
		Close();
		return;
	}

	if(!IsTagAdmin())
		return;
	const bool CanStart = NumTagAccepted() >= 3 && !m_TagStarting;
	StartButton.Draw(CanStart ? (Ui()->HotItem() == &s_TagStartButton ? CMenus::AccentColor() : CMenus::AccentColor().WithAlpha(0.7f)) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.1f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&StartButton, Localize("Start"), 10.0f, TEXTALIGN_MC);
	if(CanStart && Ui()->DoButtonLogic(&s_TagStartButton, 0, &StartButton, BUTTONFLAG_LEFT))
		StartTag();
}

void CMiniGames::RenderTagTimer()
{
	if(m_Game != GAME_TAG || m_State != STATE_PLAYING)
		return;
	const int TargetId = TagTargetId();
	if(TargetId < 0)
		return;

	const CUIRect Screen = *Ui()->Screen();
	const bool ShowRunner = m_TagPhase == TAG_PHASE_RUNNING || m_TagHitFramePending;
	const float Height = ShowRunner ? 58.0f : 40.0f;
	CUIRect Timer = {Screen.x + Screen.w / 2.0f - 100.0f, Screen.y + 28.0f, 200.0f, Height};
	Timer.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f), IGraphics::CORNER_ALL, 8.0f);
	CUIRect Value, Runner;
	Timer.Margin(6.0f, &Timer);
	if(ShowRunner)
		Timer.HSplitTop(32.0f, &Value, &Runner);
	else
		Value = Timer;
	char aValue[64];
	if(m_TagHitFramePending)
	{
		str_copy(aValue, Localize("Hit!"));
		TextRender()->TextColor(ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f));
	}
	else if(m_TagPhase == TAG_PHASE_COUNTDOWN)
	{
		const int TicksLeft = std::max(0, m_TagRoundStartTick - Client()->GameTick(g_Config.m_ClDummy));
		const int SecondsLeft = std::max(1, (TicksLeft + Client()->GameTickSpeed() - 1) / Client()->GameTickSpeed());
		str_format(aValue, sizeof(aValue), "%d", SecondsLeft);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else
	{
		const int ElapsedTicks = std::max(0, Client()->GameTick(g_Config.m_ClDummy) - m_TagRoundStartTick);
		str_format(aValue, sizeof(aValue), "%.2f s", ElapsedTicks / (float)Client()->GameTickSpeed());
		TextRender()->TextColor(ColorRGBA(1.0f, 0.3f, 0.3f, 1.0f));
	}
	Ui()->DoLabel(&Value, aValue, 26.0f, TEXTALIGN_MC);
	TextRender()->TextColor(ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
	if(ShowRunner)
		Ui()->DoLabel(&Runner, GameClient()->m_aClients[TargetId].m_aName, 10.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CMiniGames::RenderTagResults(bool Interactive)
{
	CUIRect Window = OpenWindow(380.0f, 320.0f);
	CUIRect Title, Winner, Rows, ButtonRow;
	Window.HSplitTop(24.0f, &Title, &Window);
	Window.HSplitTop(22.0f, &Winner, &Window);
	Window.HSplitTop(8.0f, nullptr, &Window);
	Window.HSplitBottom(24.0f, &Rows, &ButtonRow);
	Rows.HSplitBottom(8.0f, &Rows, nullptr);
	Ui()->DoLabel(&Title, Localize("Tag results"), 16.0f, TEXTALIGN_ML);
	char aWinner[128];
	str_format(aWinner, sizeof(aWinner), Localize("Winner: %s"), m_aStatus);
	TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.3f, 1.0f));
	Ui()->DoLabel(&Winner, aWinner, 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	int aRanked[TAG_MAX_PLAYERS];
	for(int i = 0; i < m_TagNumPlayers; i++)
		aRanked[i] = m_aTagOrder[i];
	std::stable_sort(aRanked, aRanked + m_TagNumPlayers, [&](int A, int B) { return m_aTagTimeCs[A] > m_aTagTimeCs[B]; });

	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 20.0f;
	static CScrollRegion s_TagResultsScroll;
	s_TagResultsScroll.Begin(&Rows, &ScrollParams);
	for(int Rank = 0; Rank < m_TagNumPlayers; Rank++)
	{
		CUIRect Row;
		Rows.HSplitTop(20.0f, &Row, &Rows);
		s_TagResultsScroll.AddRect(Row);
		if(Rank == 0)
			Row.Draw(ColorRGBA(1.0f, 0.75f, 0.15f, 0.13f), IGraphics::CORNER_ALL, 3.0f);
		else if(Rank % 2 == 1)
			Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_ALL, 3.0f);
		CUIRect Place, Time, Name;
		Row.VSplitLeft(38.0f, &Place, &Name);
		Name.VSplitRight(70.0f, &Name, &Time);
		char aPlace[16];
		char aTime[32];
		str_format(aPlace, sizeof(aPlace), "#%d", Rank + 1);
		str_format(aTime, sizeof(aTime), "%.2f s", m_aTagTimeCs[aRanked[Rank]] / 100.0f);
		TextRender()->TextColor(Rank == 0 ? ColorRGBA(1.0f, 0.85f, 0.3f, 1.0f) : ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f));
		Ui()->DoLabel(&Place, aPlace, 9.0f, TEXTALIGN_ML);
		TextRender()->TextColor(aRanked[Rank] == GameClient()->m_Snap.m_LocalClientId ? CMenus::AccentColor() : TextRender()->DefaultTextColor());
		Ui()->DoLabel(&Name, GameClient()->m_aClients[aRanked[Rank]].m_aName, 9.0f, TEXTALIGN_ML);
		TextRender()->TextColor(ColorRGBA(0.85f, 0.85f, 0.85f, 1.0f));
		Ui()->DoLabel(&Time, aTime, 9.0f, TEXTALIGN_MR);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	s_TagResultsScroll.End();
	if(!Interactive)
	{
		TextRender()->TextColor(ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f));
		Ui()->DoLabel(&ButtonRow, Localize("Left click to use the mouse"), 8.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return;
	}

	CUIRect CloseButton;
	ButtonRow.VSplitRight(90.0f, nullptr, &CloseButton);
	static CButtonContainer s_TagResultsClose;
	CloseButton.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, Ui()->HotItem() == &s_TagResultsClose ? 0.25f : 0.12f), IGraphics::CORNER_ALL, 5.0f);
	Ui()->DoLabel(&CloseButton, Localize("Close"), 10.0f, TEXTALIGN_MC);
	if(Ui()->DoButtonLogic(&s_TagResultsClose, 0, &CloseButton, BUTTONFLAG_LEFT))
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
	if(m_Game == GAME_TAG && m_State == STATE_TAG_LOBBY)
	{
		RenderTagLobby(Interactive);
		return;
	}
	if(m_Game == GAME_TAG && m_TagPhase == TAG_PHASE_RESULTS)
	{
		RenderTagResults(Interactive);
		return;
	}

	const bool ShowBoard = m_HasBoard && (m_State == STATE_PLAYING || m_State == STATE_OVER);

	const CUIRect Screen = *Ui()->Screen();
	CUIRect Panel;
	const bool Chess = m_Game == GAME_CHESS;
	const bool Battle = m_Game == GAME_BATTLESHIP;
	Panel.w = ShowBoard ? (Chess ? 280.0f : Battle ? 330.0f :
							 210.0f) :
			      260.0f;
	Panel.h = ShowBoard ? (Chess ? 320.0f : Battle ? 240.0f :
							 250.0f) :
			      84.0f;
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
			if(m_Game == GAME_TAG)
			{
				m_aTagAccepted[m_LocalId] = true;
				m_State = STATE_TAG_LOBBY;
				m_TagPhase = TAG_PHASE_LOBBY;
				char aAccept[32];
				str_format(aAccept, sizeof(aAccept), "A %d", m_TagLobbyId);
				SendTo(m_TagAdminId, aAccept);
			}
			else
			{
				StartGame(m_OpponentId, false);
				SendProtocol("A", MAX_RETRIES);
			}
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

void CMiniGames::OnUpdate()
{
	FlushSendQueue();
	UpdateTagLobby();
	UpdateTagHitDetection();
	if(m_State == STATE_OVER && m_vEmoteQueue.empty() && !m_EmoteWaiting && !m_MovePending)
		SetEmoteGameShowAll(false);
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
		if(m_Game == GAME_TAG && m_TagAdminId >= 0)
		{
			if(m_LocalId == m_TagAdminId)
			{
				for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
				{
					if(ClientId != m_TagAdminId && m_aTagInvited[ClientId])
						SendTo(ClientId, "Q");
				}
			}
			else
				SendTo(m_TagAdminId, "Q");
		}
		Finish(0, "Aborted, you switched to another tee.");
		ResetEmoteChannel();
	}
	else if(m_Game == GAME_TAG && m_State != STATE_SELECT && m_State != STATE_OVER && m_TagAdminId >= 0 && !GameClient()->m_aClients[m_TagAdminId].m_Active)
	{
		Finish(0, "The Tag lobby admin left the server.");
		ResetEmoteChannel();
	}
	else if(m_Game != GAME_TAG && m_State != STATE_SELECT && m_State != STATE_OVER && (m_OpponentId < 0 || !GameClient()->m_aClients[m_OpponentId].m_Active))
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

	if(m_State == STATE_OVER && !m_HasBoard && m_ViewActive && !m_KeyBlocked && !(m_Game == GAME_TAG && m_TagPhase == TAG_PHASE_RESULTS))
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
	if(m_State == STATE_OVER && !(m_Game == GAME_TAG && m_TagPhase == TAG_PHASE_RESULTS))
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

	m_ShowKeyHint = m_State == STATE_INVITED || m_State == STATE_PLAYING || m_State == STATE_TAG_LOBBY || (m_State == STATE_OVER && m_HasBoard);

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

	if(!(m_Game == GAME_TAG && (m_State == STATE_PLAYING || m_TagPhase == TAG_PHASE_RESULTS)))
		RenderStatusBar(Alpha);
	RenderTagTimer();
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
