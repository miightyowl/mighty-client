#ifndef GAME_CLIENT_COMPONENTS_MINIGAMES_H
#define GAME_CLIENT_COMPONENTS_MINIGAMES_H

#include "chess_board.h"

#include <engine/console.h>
#include <engine/input.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <string>
#include <vector>

class CMiniGames : public CComponent
{
public:
	enum EGame
	{
		GAME_TICTACTOE,
		GAME_CHESS,
		GAME_BATTLESHIP,
		NUM_GAMES,
	};

	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool IsActive() const { return m_State != STATE_IDLE; }

	bool OnWhisper(int ClientId, int Team, const char *pMessage);

	void OnChatMessage(int ClientId, const char *pMessage);

	void OnEmoticon(int ClientId, int Emoticon);

	bool QueueManualEmote(int Emoticon);

private:
	enum EState
	{
		STATE_IDLE,
		STATE_GAMES, // picking a game
		STATE_SELECT,
		STATE_CALLING,
		STATE_RINGING,
		STATE_INVITED,
		STATE_PLAYING,
		STATE_OVER,
	};

	EState m_State = STATE_IDLE;
	EGame m_Game = GAME_TICTACTOE;
	int m_OpponentId = -1;
	int m_LocalId = -1;
	int m_SelectedId = -1;

	enum ENote
	{
		NOTE_NONE,
		NOTE_MISS,
		NOTE_HIT,
		NOTE_SUNK,
		NOTE_FOE_MISS,
		NOTE_FOE_HIT,
		NOTE_FOE_SUNK,
	};

	char m_aBoard[10] = ".........";
	CChessBoard m_Chess;

	char m_aMyWaters[100] = {0};
	char m_aFoeWaters[100] = {0};
	bool m_BattleMyTurn = false;
	int m_BattleShot = -1;
	ENote m_BattleNote = NOTE_NONE;
	float m_BattleNoteTime = 0.0f;
	int m_ChessSelected = -1;
	int m_MoveCount = 0;
	char m_MyMark = 'X';
	int m_aWinLine[3] = {-1, -1, -1};
	char m_Result = 0;
	char m_aStatus[128] = "";
	bool m_HasBoard = false;
	float m_FadeTime = 0.0f;

	bool m_ViewActive = false;
	bool m_CursorActive = false;
	bool m_IgnoreClick = false;
	bool m_KeyBlocked = false;
	bool m_ShowKeyHint = false;
	float m_AcceptDeadline = 0.0f;

	std::vector<std::string> m_vSendQueue;
	float m_NextSendTime = 0.0f;
	float m_ChatScore = 0.0f;
	float m_ChatScoreTime = 0.0f;

	std::string m_RetryMessage;
	float m_RetryTime = 0.0f;
	int m_RetryCount = 0;
	int m_RetryMax = 0;

	std::vector<int> m_vEmoteQueue;
	bool m_EmoteWaiting = false;
	float m_EmoteTime = 0.0f;
	int m_EmoteRetries = 0;

	int m_FrameOp = -1;
	int m_FrameExpect = 0;
	int m_FrameLen = 0;
	int m_aFrame[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	int m_aMoveFrame[5] = {0, 0, 0, 0, 0};
	int m_MoveFrameLen = 0;
	bool m_MovePending = false;
	float m_MoveRetryTime = 0.0f;
	int m_MoveRetryCount = 0;

	const char *GameName() const;
	bool AmWhite() const { return m_MyMark == 'X'; }
	bool WhisperSupported() const;
	bool WindowVisible() const;
	bool MyTurn() const;
	bool NeedsAttention() const;
	bool ViewShown() const;
	int ViewKeyState() const;
	const char *OpponentName() const;
	void StatusText(char *pBuf, size_t Size) const;

	void Toggle();
	void HideView();
	void OpenGames();
	void OpenSelect();
	void Close();
	void Finish(char Result, const char *pStatus);
	void Challenge(int ClientId);
	void StartGame(int OpponentId, bool Challenger);

	void SendTo(int ClientId, const char *pMessage);
	void SendProtocol(const char *pMessage, int MaxRetries);
	void FlushSendQueue();
	void UpdateRetry();
	void ClearRetry();

	int BoardHash() const;
	void SendFrame(int Op, const int *pDigits, int NumDigits);
	void SendMoveAck(int Cell);
	void FlushEmoteQueue();
	void UpdateMoveRetry();
	void ClearMoveRetry();
	void ResetEmoteChannel();
	void HandleEmoteEcho(int Emoticon);
	void HandleEmoteFrame(int Emoticon);
	void ProcessFrame();
	void PlayCell(int Cell);
	void ChessClick(int Square);
	void PlaceShips();
	void Shoot(int Cell);
	void ApplyShotResult(int Cell, int Result);
	int ShotResult(int Cell) const;
	bool ShipSunk(int Cell) const;
	bool PlayChessMove(int From, int To);
	bool CheckGameEnd();

	int CollectPlayers(int *pIds) const;

	void RenderGameMenu();
	void RenderGameIcon(int Game, CUIRect Area, float Alpha);
	void RenderSelectModal();
	void RenderStatusBar(float Alpha);
	void RenderView(bool Interactive, float Alpha);
	void RenderBoard(CUIRect Area, bool Interactive, float Alpha);
	void RenderTicTacToeBoard(CUIRect Grid, bool Interactive, float Alpha);
	void RenderChessBoard(CUIRect Grid, bool Interactive, float Alpha);
	void RenderBattleshipBoard(CUIRect Area, bool Interactive, float Alpha);
	void RenderWaterGrid(CUIRect Area, bool Own, bool Interactive, float Alpha);
	CUIRect OpenWindow(float Width, float Height) const;

	static void ConMiniGames(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyMiniGames(IConsole::IResult *pResult, void *pUserData);
};

#endif
