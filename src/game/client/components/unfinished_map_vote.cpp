#include "unfinished_map_vote.h"

#include <base/secure.h>
#include <base/str.h>

#include <engine/serverbrowser.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <set>

static bool IsWordChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool DescriptionMatchesMap(const char *pDescription, const char *pMapName)
{
	const int MapNameLength = str_length(pMapName);
	if(MapNameLength == 0)
		return false;
	for(const char *pMatch = str_find_nocase(pDescription, pMapName); pMatch; pMatch = str_find_nocase(pMatch + 1, pMapName))
	{
		const bool StartsAtBoundary = pMatch == pDescription || !IsWordChar(pMatch[-1]);
		const bool EndsAtBoundary = !IsWordChar(pMatch[MapNameLength]);
		if(StartsAtBoundary && EndsAtBoundary)
			return true;
	}
	return false;
}

static int VoteDescriptionStars(const char *pDescription)
{
	const char *pMatch = str_find(pDescription, "/5 ★");
	if(pMatch == nullptr || pMatch == pDescription || pMatch[-1] < '0' || pMatch[-1] > '5')
		return -1;
	return pMatch[-1] - '0';
}

static char SmallCapToAscii(int Codepoint)
{
	switch(Codepoint)
	{
	case 0x1D00: return 'a';
	case 0x0299: return 'b';
	case 0x1D04: return 'c';
	case 0x1D05: return 'd';
	case 0x1D07: return 'e';
	case 0xA730: return 'f';
	case 0x0262: return 'g';
	case 0x029C: return 'h';
	case 0x026A: return 'i';
	case 0x1D0A: return 'j';
	case 0x1D0B: return 'k';
	case 0x029F: return 'l';
	case 0x1D0D: return 'm';
	case 0x0274: return 'n';
	case 0x1D0F: return 'o';
	case 0x1D18: return 'p';
	case 0x0280: return 'r';
	case 0xA731: return 's';
	case 0x1D1B: return 't';
	case 0x1D1C: return 'u';
	case 0x1D20: return 'v';
	case 0x1D21: return 'w';
	case 0x028F: return 'y';
	case 0x1D22: return 'z';
	default: return '\0';
	}
}

static void NormalizeVoteDescription(char *pDst, int DstSize, const char *pSrc)
{
	int Length = 0;
	while(*pSrc && Length < DstSize - 1)
	{
		const int Codepoint = str_utf8_decode(&pSrc);
		char Char;
		if(Codepoint >= 'A' && Codepoint <= 'Z')
			Char = Codepoint - 'A' + 'a';
		else if(Codepoint >= 0x20 && Codepoint < 0x7F)
			Char = Codepoint;
		else
		{
			Char = SmallCapToAscii(Codepoint);
			if(!Char)
				continue;
		}
		pDst[Length++] = Char;
	}
	pDst[Length] = '\0';
}

void CUnfinishedMapVote::OnConsoleInit()
{
	Console()->Register("vote_random_unfinished_by_all", "?r[reason]", CFGFLAG_CLIENT, ConVoteRandomUnfinishedByAll, this, "Call a vote for a random map of the current server type that no player on the server has finished (uses ddnet.org stats, reason 1-5 picks maps with that star rating)");
	Console()->Register("vote_random_unfinished_by_selected", "?r[reason]", CFGFLAG_CLIENT, ConVoteRandomUnfinishedBySelected, this, "Call a vote for a random map of the current server type that none of the players selected in the vote menu has finished (uses ddnet.org stats, reason 1-5 picks maps with that star rating)");
}

void CUnfinishedMapVote::ConVoteRandomUnfinishedByAll(IConsole::IResult *pResult, void *pUserData)
{
	CUnfinishedMapVote *pSelf = (CUnfinishedMapVote *)pUserData;
	pSelf->Start(pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
}

void CUnfinishedMapVote::ConVoteRandomUnfinishedBySelected(IConsole::IResult *pResult, void *pUserData)
{
	CUnfinishedMapVote *pSelf = (CUnfinishedMapVote *)pUserData;
	pSelf->StartSelected(pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
}

void CUnfinishedMapVote::TogglePlayerSelection(const char *pName)
{
	if(!pName[0])
		return;
	auto [Iterator, Inserted] = m_SelectedPlayers.emplace(pName);
	if(!Inserted)
		m_SelectedPlayers.erase(Iterator);
}

void CUnfinishedMapVote::EnsureLocalPlayerSelected()
{
	if(m_LocalPlayerAutoSelected)
		return;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;
	const char *pName = GameClient()->m_aClients[LocalId].m_aName;
	if(!pName[0])
		return;
	m_SelectedPlayers.emplace(pName);
	m_LocalPlayerAutoSelected = true;
}

bool CUnfinishedMapVote::CanStart()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	if(m_State != STATE_IDLE)
	{
		GameClient()->Echo("Unfinished map vote: already looking up maps, please wait.");
		return false;
	}

	CServerInfo ServerInfo;
	Client()->GetServerInfo(&ServerInfo);
	str_copy(m_aCurrentMap, ServerInfo.m_aMap);
	if(!m_aCurrentMap[0])
	{
		GameClient()->Echo("Unfinished map vote: couldn't determine the current map.");
		return false;
	}
	return true;
}

void CUnfinishedMapVote::Start(const char *pReason)
{
	if(!CanStart())
		return;

	m_vPlayerNames.clear();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		const char *pName = GameClient()->m_aClients[i].m_aName;
		if(!pName[0])
			continue;
		if(std::find(m_vPlayerNames.begin(), m_vPlayerNames.end(), pName) == m_vPlayerNames.end())
			m_vPlayerNames.emplace_back(pName);
	}

	m_SelectedPlayersOnly = false;
	Launch(pReason);
}

void CUnfinishedMapVote::StartSelected(const char *pReason)
{
	if(!CanStart())
		return;

	m_vPlayerNames.clear();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		const char *pName = GameClient()->m_aClients[i].m_aName;
		if(!pName[0] || !IsPlayerSelected(pName))
			continue;
		if(std::find(m_vPlayerNames.begin(), m_vPlayerNames.end(), pName) == m_vPlayerNames.end())
			m_vPlayerNames.emplace_back(pName);
	}
	if(m_vPlayerNames.empty())
	{
		GameClient()->Echo("Unfinished map vote: no players selected, click the tees above the vote list to select them.");
		return;
	}

	m_SelectedPlayersOnly = true;
	Launch(pReason);
}

void CUnfinishedMapVote::Launch(const char *pReason)
{
	str_copy(m_aReason, pReason);

	if(DetermineServerTypeFromVoteList())
	{
		StartPlayerRequests();
		return;
	}

	char aEscaped[2 * MAX_MAP_LENGTH + 64];
	EscapeUrl(aEscaped, m_aCurrentMap);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
	m_pMapInfoRequest = RunRequest(aUrl);
	m_State = STATE_MAP_INFO;
}

bool CUnfinishedMapVote::DetermineServerTypeFromVoteList()
{
	static const struct
	{
		const char *m_pNormalized;
		const char *m_pType;
	} s_aCategories[] = {
		{"novice", "Novice"},
		{"moderate", "Moderate"},
		{"brutal", "Brutal"},
		{"insane", "Insane"},
		{"dummy", "Dummy"},
		{"ddmax", "DDmaX"},
		{"oldschool", "Oldschool"},
		{"solo", "Solo"},
		{"race", "Race"},
		{"fun", "Fun"},
	};

	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext)
	{
		if(!str_startswith(pOption->m_aDescription, "☒"))
			continue;
		char aNormalized[VOTE_DESC_LENGTH];
		NormalizeVoteDescription(aNormalized, sizeof(aNormalized), pOption->m_aDescription);
		if(!str_find(aNormalized, "maps"))
			continue;
		for(const auto &Category : s_aCategories)
		{
			if(str_find(aNormalized, Category.m_pNormalized))
			{
				str_copy(m_aServerType, Category.m_pType);
				return true;
			}
		}
	}
	return false;
}

std::shared_ptr<CHttpRequest> CUnfinishedMapVote::RunRequest(const char *pUrl)
{
	std::shared_ptr<CHttpRequest> pRequest = HttpGet(pUrl);
	pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	pRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(pRequest);
	return pRequest;
}

void CUnfinishedMapVote::OnRender()
{
	if(m_State == STATE_MAP_INFO)
	{
		if(m_pMapInfoRequest->Done())
			OnMapInfoDone();
	}
	else if(m_State == STATE_PLAYERS)
	{
		if(std::all_of(m_vPlayerRequests.begin(), m_vPlayerRequests.end(), [](const SPlayerRequest &Request) { return Request.m_pRequest->Done(); }))
			OnPlayersDone();
	}
}

void CUnfinishedMapVote::OnMapInfoDone()
{
	if(m_pMapInfoRequest->State() != EHttpState::DONE)
	{
		Stop("Unfinished map vote: couldn't reach ddnet.org.");
		return;
	}

	json_value *pJson = m_pMapInfoRequest->ResultJson();
	m_aServerType[0] = '\0';
	if(pJson)
	{
		const json_value *pType = json_object_get(pJson, "type");
		if(pType->type == json_string)
			str_copy(m_aServerType, json_string_get(pType));
		json_value_free(pJson);
	}
	m_pMapInfoRequest = nullptr;

	if(!m_aServerType[0])
	{
		Stop("Unfinished map vote: the current map is unknown to ddnet.org, couldn't determine the server type.");
		return;
	}

	StartPlayerRequests();
}

void CUnfinishedMapVote::StartPlayerRequests()
{
	m_vPlayerRequests.clear();
	for(const std::string &Name : m_vPlayerNames)
	{
		char aEscaped[2 * MAX_NAME_LENGTH + 64];
		EscapeUrl(aEscaped, Name.c_str());
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);
		m_vPlayerRequests.push_back({Name, RunRequest(aUrl)});
	}
	m_State = STATE_PLAYERS;
}

void CUnfinishedMapVote::OnPlayersDone()
{
	std::set<std::string> AllTypeMaps;
	std::set<std::string> FinishedByAnyone;
	std::vector<std::string> vFullFinishers;

	for(const SPlayerRequest &Request : m_vPlayerRequests)
	{
		if(Request.m_pRequest->State() != EHttpState::DONE)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: failed to fetch ddnet.org stats of '%s'.", Request.m_Name.c_str());
			Stop(aBuf);
			return;
		}

		json_value *pJson = Request.m_pRequest->ResultJson();
		if(!pJson)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: invalid response from ddnet.org for '%s'.", Request.m_Name.c_str());
			Stop(aBuf);
			return;
		}

		const json_value *pTypes = json_object_get(pJson, "types");
		bool HasMapList = false;
		int NumUnfinishedByPlayer = 0;
		if(pTypes->type == json_object)
		{
			for(unsigned TypeIndex = 0; TypeIndex < pTypes->u.object.length; TypeIndex++)
			{
				if(!str_startswith(pTypes->u.object.values[TypeIndex].name, m_aServerType))
					continue;
				const json_value *pMaps = json_object_get(pTypes->u.object.values[TypeIndex].value, "maps");
				if(pMaps->type != json_object)
					continue;
				HasMapList = true;
				for(unsigned MapIndex = 0; MapIndex < pMaps->u.object.length; MapIndex++)
				{
					const char *pMapName = pMaps->u.object.values[MapIndex].name;
					const json_value *pFinishes = json_object_get(pMaps->u.object.values[MapIndex].value, "finishes");
					AllTypeMaps.emplace(pMapName);
					if(pFinishes->type == json_integer && json_int_get(pFinishes) > 0)
						FinishedByAnyone.emplace(pMapName);
					else if(str_comp(pMapName, m_aCurrentMap) != 0)
						NumUnfinishedByPlayer++;
				}
			}
		}
		if(HasMapList && NumUnfinishedByPlayer == 0)
			vFullFinishers.push_back(Request.m_Name);
		json_value_free(pJson);
	}

	int NumUnfinished = 0;
	for(const std::string &Map : AllTypeMaps)
		if(FinishedByAnyone.count(Map) == 0 && str_comp(Map.c_str(), m_aCurrentMap) != 0)
			NumUnfinished++;

	int StarsFilter = -1;
	if(m_aReason[0] >= '1' && m_aReason[0] <= '5' && m_aReason[1] == '\0')
		StarsFilter = m_aReason[0] - '0';

	std::vector<std::pair<std::string, int>> vCandidates;
	std::set<std::string> TakenMaps;
	int NumWrongStars = 0;
	int OptionIndex = 0;
	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, OptionIndex++)
	{
		const std::string *pBestMap = nullptr;
		for(const std::string &Map : AllTypeMaps)
		{
			if((!pBestMap || Map.length() > pBestMap->length()) && DescriptionMatchesMap(pOption->m_aDescription, Map.c_str()))
				pBestMap = &Map;
		}
		if(pBestMap == nullptr || FinishedByAnyone.count(*pBestMap) != 0 || str_comp(pBestMap->c_str(), m_aCurrentMap) == 0)
			continue;
		if(StarsFilter != -1 && VoteDescriptionStars(pOption->m_aDescription) != StarsFilter)
		{
			NumWrongStars++;
			continue;
		}
		if(!TakenMaps.emplace(*pBestMap).second)
			continue;
		vCandidates.emplace_back(*pBestMap, OptionIndex);
	}

	if(vCandidates.empty())
	{
		char aBuf[512] = "";
		if(NumUnfinished == 0)
		{
			if(!vFullFinishers.empty())
			{
				char aNames[256] = "";
				for(size_t Finisher = 0; Finisher < vFullFinishers.size(); Finisher++)
				{
					if(Finisher > 0)
						str_append(aNames, Finisher == vFullFinishers.size() - 1 ? " and " : ", ");
					str_append(aNames, "'");
					str_append(aNames, vFullFinishers[Finisher].c_str());
					str_append(aNames, "'");
				}
				str_format(aBuf, sizeof(aBuf), "Unfinished map vote: every %s map has been finished by %s on this server.", m_aServerType, aNames);
			}
		}
		else if(NumWrongStars > 0)
		{
			const int NumPlayers = m_vPlayerRequests.size();
			char aWho[64];
			if(m_SelectedPlayersOnly)
				str_format(aWho, sizeof(aWho), "the %d selected player%s", NumPlayers, NumPlayers == 1 ? "" : "s");
			else
				str_format(aWho, sizeof(aWho), "all %d player%s on the server", NumPlayers, NumPlayers == 1 ? "" : "s");
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: %d %s maps are unfinished by %s, but none of them has %d★.", NumWrongStars, m_aServerType, aWho, StarsFilter);
		}
		Stop(aBuf[0] ? aBuf : nullptr);
		return;
	}

	const auto &Pick = vCandidates[secure_rand_below((int)vCandidates.size())];
	GameClient()->m_Voting.CallvoteOption(Pick.second, m_aReason);
	Stop(nullptr);
}

void CUnfinishedMapVote::Stop(const char *pErrorMessage)
{
	if(m_pMapInfoRequest)
	{
		m_pMapInfoRequest->Abort();
		m_pMapInfoRequest = nullptr;
	}
	for(SPlayerRequest &Request : m_vPlayerRequests)
		Request.m_pRequest->Abort();
	m_vPlayerRequests.clear();
	m_vPlayerNames.clear();
	m_State = STATE_IDLE;
	if(pErrorMessage)
		GameClient()->Echo(pErrorMessage);
}

void CUnfinishedMapVote::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
	{
		Stop(nullptr);
		m_SelectedPlayers.clear();
		m_LocalPlayerAutoSelected = false;
	}
}
