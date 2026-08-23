#ifndef GAME_CLIENT_COMPONENTS_FINISH_RENAME_H
#define GAME_CLIENT_COMPONENTS_FINISH_RENAME_H

#include <base/vmath.h>

#include <engine/map.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class IHttpRequest;
struct _json_value;

class CFinishRename : public CComponent
{
	enum EStatus
	{
		STATUS_PENDING,
		STATUS_FINISHED,
		STATUS_UNFINISHED,
		STATUS_FAILED,
	};
	struct SLookup
	{
		std::shared_ptr<IHttpRequest> m_pRequest;
		bool m_Done = false;
		bool m_Failed = false;
		std::set<std::string> m_FinishedMaps;
	};
	std::map<std::string, SLookup> m_Lookups;

	char m_aMap[MAX_MAP_LENGTH] = "";
	bool m_MapScanned = false;
	std::vector<vec2> m_vFinishTilePositions;
	bool m_Armed = true;

	bool m_Decided = false;
	char m_aTargetName[MAX_NAME_LENGTH] = "";
	std::string m_DecisionInputs;

	char m_aOriginalName[MAX_NAME_LENGTH] = "";
	int m_RenamedDummy = -1;

	const char *ActiveName() const;
	std::vector<std::string> AlternativeNames() const;
	void ResetMapState();
	void ScanFinishTiles();
	void EnsureLookup(const char *pName);
	void UpdateLookups();
	EStatus LookupStatus(const char *pName) const;
	void ParseFinishedMaps(const _json_value *pJson, std::set<std::string> *pFinishedMaps) const;
	float DistanceToFinish(vec2 Pos) const;
	bool PathIsClear(vec2 From, vec2 To) const;
	bool FinishReachable(vec2 Pos, float Range) const;
	void ComputeDecision();
	void Rename(const char *pName);
	void RestoreName(bool SendUpdate);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
};

#endif
