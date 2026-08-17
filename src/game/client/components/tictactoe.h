#ifndef GAME_CLIENT_COMPONENTS_TICTACTOE_H
#define GAME_CLIENT_COMPONENTS_TICTACTOE_H

#include <engine/console.h>
#include <engine/input.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <string>
#include <vector>

class CTicTacToe : public CComponent
{
public:
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
		STATE_SELECT,
		STATE_CALLING,
		STATE_RINGING,
		STATE_INVITED,
		STATE_PLAYING,
		STATE_OVER,
	};

	EState m_State = STATE_IDLE;
	int m_OpponentId = -1;
	int m_LocalId = -1;
	int m_SelectedId = -1;

	char m_aBoard[10] = ".........";
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
	int m_aFrame[4] = {0, 0, 0, 0};

	int m_aMoveFrame[2] = {0, 0};
	bool m_MovePending = false;
	float m_MoveRetryTime = 0.0f;
	int m_MoveRetryCount = 0;

	bool WhisperSupported() const;
	bool WindowVisible() const;
	bool MyTurn() const;
	bool ViewShown() const;
	int ViewKeyState() const;
	const char *OpponentName() const;
	void StatusText(char *pBuf, size_t Size) const;

	void Toggle();
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
	void SendMoveAck();
	void FlushEmoteQueue();
	void UpdateMoveRetry();
	void ClearMoveRetry();
	void ResetEmoteChannel();
	void HandleEmoteEcho(int Emoticon);
	void HandleEmoteFrame(int Emoticon);
	void ProcessFrame();
	void PlayCell(int Cell);
	bool CheckGameEnd();

	int CollectPlayers(int *pIds) const;

	void RenderSelectModal();
	void RenderStatusBar(float Alpha);
	void RenderView(bool Interactive, float Alpha);
	void RenderBoard(CUIRect Area, bool Interactive, float Alpha);
	CUIRect OpenWindow(float Width, float Height) const;

	static void ConTicTacToe(IConsole::IResult *pResult, void *pUserData);
	static void ConKeyTicTacToe(IConsole::IResult *pResult, void *pUserData);
};

#endif
