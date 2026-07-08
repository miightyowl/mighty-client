#ifndef GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H
#define GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H

#include <engine/console.h>
#include <engine/map.h>

#include <game/client/component.h>
#include <game/voting.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

class CHttpRequest;

class CUnfinishedMapVote : public CComponent
{
	enum EState
	{
		STATE_IDLE,
		STATE_MAP_INFO,
		STATE_PLAYERS,
	};
	EState m_State = STATE_IDLE;

	char m_aReason[VOTE_REASON_LENGTH] = "";
	char m_aCurrentMap[MAX_MAP_LENGTH] = "";
	char m_aServerType[32] = "";
	bool m_SelectedPlayersOnly = false;

	std::shared_ptr<CHttpRequest> m_pMapInfoRequest;

	struct SPlayerRequest
	{
		std::string m_Name;
		std::shared_ptr<CHttpRequest> m_pRequest;
	};
	std::vector<SPlayerRequest> m_vPlayerRequests;
	std::vector<std::string> m_vPlayerNames;

	std::set<std::string> m_SelectedPlayers;
	bool m_LocalPlayerAutoSelected = false;

	bool CanStart();
	void Launch(const char *pReason);
	bool DetermineServerTypeFromVoteList();
	void StartPlayerRequests();
	std::shared_ptr<CHttpRequest> RunRequest(const char *pUrl);
	void OnMapInfoDone();
	void OnPlayersDone();
	void Stop(const char *pErrorMessage);

	static void ConVoteRandomUnfinishedByAll(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteRandomUnfinishedBySelected(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	void Start(const char *pReason);
	void StartSelected(const char *pReason);
	bool IsActive() const { return m_State != STATE_IDLE; }

	bool IsPlayerSelected(const char *pName) const { return m_SelectedPlayers.count(pName) != 0; }
	void TogglePlayerSelection(const char *pName);
	void EnsureLocalPlayerSelected();
};

#endif
