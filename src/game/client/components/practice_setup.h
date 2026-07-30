#ifndef GAME_CLIENT_COMPONENTS_PRACTICE_SETUP_H
#define GAME_CLIENT_COMPONENTS_PRACTICE_SETUP_H

#include <engine/console.h>
#include <engine/input.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <string>
#include <vector>

class CPracticeSetup : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;

	bool IsActive() const { return m_State != STATE_IDLE; }

private:
	enum EState
	{
		STATE_IDLE,
		STATE_SELECT,
		STATE_CONFIRM_KILL,
		STATE_CONFIRM_REDO,
	};

	EState m_State = STATE_IDLE;
	std::vector<int> m_vSelectedIds;

	bool m_TeamReady = false;
	int m_PracticeTeam = -1;

	std::vector<std::string> m_vPendingCommands;
	float m_NextCommandTime = 0.0f;
	bool m_WaitingForDummy = false;

	void Toggle();
	void Close();
	void Start(bool KillExistingDummy);
	void QueueTeleportAndInvites();
	void RunPendingCommands();
	int PickEmptyTeam() const;
	int CollectPlayers(int *pIds) const;
	bool IsSelected(int ClientId) const;
	void ToggleSelected(int ClientId);
	const char *MainTeeName() const;

	void RenderSelectModal();
	void RenderConfirmModal();

	static void ConPracticeSetup(IConsole::IResult *pResult, void *pUserData);
};

#endif
