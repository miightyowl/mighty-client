#ifndef GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H
#define GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H

#include <engine/console.h>
#include <engine/map.h>

#include <game/client/component.h>
#include <game/voting.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class IHttpRequest;
struct _json_value;

class CUnfinishedMapVote : public CComponent
{
public:
	struct SRemainingMap
	{
		int m_OptionIndex;
		std::string m_Description;
		std::string m_Info;
	};

private:
	char m_aReason[VOTE_REASON_LENGTH] = "";
	char m_aCurrentMap[MAX_MAP_LENGTH] = "";
	char m_aServerType[32] = "";
	bool m_ServerTypeFailed = false;
	bool m_VotePending = false;

	std::shared_ptr<IHttpRequest> m_pMapInfoRequest;

	struct SPlayerStats
	{
		std::set<std::string> m_TypeMaps;
		std::set<std::string> m_FinishedMaps;
	};
	std::map<std::string, SPlayerStats> m_PlayerStats;
	std::map<std::string, std::shared_ptr<IHttpRequest>> m_PlayerRequests;
	std::set<std::string> m_FailedPlayers;

	std::vector<std::string> m_vPlayerNames;

	std::set<std::string> m_SelectedPlayers;
	bool m_LocalPlayerAutoSelected = false;

	std::vector<SRemainingMap> m_vRemainingMaps;
	std::vector<std::string> m_vRemainingNames;
	bool m_RemainingDirty = true;
	bool m_RemainingLoading = false;
	int m_RemainingNumOptions = -1;

	bool CanStart();
	void Launch(const char *pReason);
	std::vector<std::string> SelectedPlayerNames() const;
	bool DetermineServerTypeFromVoteList();
	void UpdateServerType();
	std::shared_ptr<IHttpRequest> RunRequest(const char *pUrl);
	void RequestPlayer(const char *pName);
	bool PlayerReady(const char *pName) const;
	void PollPlayerRequests();
	void ParsePlayerStats(const char *pName, const _json_value *pJson);
	void UpdateVote();
	void Analyze();
	void RecomputeRemainingMaps(const std::vector<std::string> &vNames);
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
	bool IsActive() const { return m_VotePending; }

	bool IsPlayerSelected(const char *pName) const { return m_SelectedPlayers.contains(pName); }
	void TogglePlayerSelection(const char *pName);
	void EnsureLocalPlayerSelected();

	void UpdateRemainingMaps();
	const std::vector<SRemainingMap> &RemainingMaps() const { return m_vRemainingMaps; }
	bool RemainingMapsLoading() const { return m_RemainingLoading; }
	bool RemainingMapsKnown() const { return !m_RemainingLoading && !m_RemainingDirty && !m_vRemainingNames.empty() && m_aServerType[0] != '\0'; }
};

#endif
