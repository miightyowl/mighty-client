#ifndef GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H
#define GAME_CLIENT_COMPONENTS_UNFINISHED_MAP_VOTE_H

#include <engine/console.h>
#include <engine/graphics.h>
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
	struct SMapRelease
	{
		std::string m_Name;
		std::string m_Type;
		std::string m_Mapper;
		std::string m_ThumbnailUrl;
		std::string m_Tags;
		std::vector<std::string> m_vTags;
		int m_Difficulty = 0;
	};

	struct SMapDetails
	{
		int m_Finishes = -1;
		double m_AverageTime = -1.0;
	};

private:
	char m_aReason[VOTE_REASON_LENGTH] = "";
	char m_aCurrentMap[MAX_MAP_LENGTH] = "";
	bool m_VotePending = false;

	std::shared_ptr<IHttpRequest> m_pMapReleasesRequest;
	bool m_MapReleasesFailed = false;
	std::vector<SMapRelease> m_vMapReleases;
	std::map<std::string, int> m_MapReleaseIndices;

	struct SMapPreview
	{
		std::shared_ptr<IHttpRequest> m_pRequest;
		IGraphics::CTextureHandle m_Texture;
		bool m_Failed = false;
	};
	std::map<std::string, SMapPreview> m_MapPreviews;
	std::map<std::string, SMapDetails> m_MapDetails;
	std::map<std::string, std::shared_ptr<IHttpRequest>> m_MapDetailsRequests;
	std::set<std::string> m_FailedMapDetails;

	struct SPlayerStats
	{
		std::set<std::string> m_FinishedMaps;
	};
	std::map<std::string, SPlayerStats> m_PlayerStats;
	std::map<std::string, std::shared_ptr<IHttpRequest>> m_PlayerRequests;
	std::set<std::string> m_FailedPlayers;

	std::vector<std::string> m_vPlayerNames;

	std::set<std::string> m_SelectedPlayers;
	bool m_LocalPlayerAutoSelected = false;

	std::set<std::string> m_RemainingMapNames;
	std::vector<std::string> m_vRemainingNames;
	bool m_RemainingDirty = true;
	bool m_RemainingLoading = false;

	bool CanStart();
	void Launch(const char *pReason);
	std::vector<std::string> SelectedPlayerNames() const;
	std::shared_ptr<IHttpRequest> RunRequest(const char *pUrl, int64_t MaxResponseSize = 16 * 1024 * 1024);
	void PollMapReleases();
	void PollMapPreviews();
	void PollMapDetails();
	void RequestPlayer(const char *pName);
	bool PlayerReady(const char *pName) const;
	void PollPlayerRequests();
	void ParsePlayerStats(const char *pName, const _json_value *pJson);
	void UpdateVote();
	void RecomputeRemainingMaps(const std::vector<std::string> &vNames);
	void Stop(const char *pErrorMessage);

	static void ConVoteRandomUnfinishedByAll(IConsole::IResult *pResult, void *pUserData);
	static void ConVoteRandomUnfinishedBySelected(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;

	void Start(const char *pReason);
	void StartSelected(const char *pReason);
	bool IsActive() const { return m_VotePending; }

	bool IsPlayerSelected(const char *pName) const { return m_SelectedPlayers.contains(pName); }
	void TogglePlayerSelection(const char *pName);
	void SetAllPlayersSelected(bool Selected);
	bool AreAllPlayersSelected() const;
	void EnsureLocalPlayerSelected();
	void UpdateMapReleases();
	const std::vector<SMapRelease> &MapReleases() const { return m_vMapReleases; }
	bool MapReleasesLoading() const { return m_pMapReleasesRequest != nullptr; }
	bool MapReleasesFailed() const { return m_MapReleasesFailed; }
	const SMapRelease *FindMapRelease(const char *pMapName);
	IGraphics::CTextureHandle RequestMapPreview(const SMapRelease *pRelease);
	const SMapDetails *RequestMapDetails(const SMapRelease *pRelease);

	void UpdateRemainingMaps();
	bool IsMapUnfinished(const char *pMapName) const { return m_RemainingMapNames.contains(pMapName); }
	bool RemainingMapsLoading() const { return m_RemainingLoading; }
	bool RemainingMapsKnown() const { return !m_RemainingLoading && !m_RemainingDirty && !m_vRemainingNames.empty(); }
};

#endif
