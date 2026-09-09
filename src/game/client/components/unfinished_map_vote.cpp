#include "unfinished_map_vote.h"

#include <base/secure.h>
#include <base/str.h>

#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <map>
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

static std::string VoteDescriptionInfo(const CVoteOptionClient *pOption)
{
	if(pOption == nullptr || str_find(pOption->m_aDescription, "⚑") == nullptr)
		return "";

	int Length = str_length(pOption->m_aDescription);
	while(Length > 0 && pOption->m_aDescription[Length - 1] == ' ')
		Length--;
	return std::string(pOption->m_aDescription, Length);
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

static bool NormalizedDescriptionMatchesMap(const char *pDescription, const char *pMapName)
{
	char aDescription[VOTE_DESC_LENGTH];
	char aMapName[MAX_MAP_LENGTH];
	NormalizeVoteDescription(aDescription, sizeof(aDescription), pDescription);
	NormalizeVoteDescription(aMapName, sizeof(aMapName), pMapName);
	return DescriptionMatchesMap(aDescription, aMapName);
}

bool CUnfinishedMapVote::VoteDescriptionContains(const char *pDescription, const char *pNeedle)
{
	char aDescription[VOTE_DESC_LENGTH];
	char aNeedle[VOTE_DESC_LENGTH];
	NormalizeVoteDescription(aDescription, sizeof(aDescription), pDescription);
	NormalizeVoteDescription(aNeedle, sizeof(aNeedle), pNeedle);
	return str_find(aDescription, aNeedle) != nullptr;
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
	m_RemainingDirty = true;
}

void CUnfinishedMapVote::SetAllPlayersSelected(bool Selected)
{
	if(Selected)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Snap.m_apPlayerInfos[i] && GameClient()->m_aClients[i].m_aRealName[0])
				m_SelectedPlayers.emplace(GameClient()->m_aClients[i].m_aRealName);
		}
	}
	else
		m_SelectedPlayers.clear();
	m_RemainingDirty = true;
}

bool CUnfinishedMapVote::AreAllPlayersSelected() const
{
	bool HasPlayers = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_aClients[i].m_aRealName[0])
			continue;
		HasPlayers = true;
		if(!IsPlayerSelected(GameClient()->m_aClients[i].m_aRealName))
			return false;
	}
	return HasPlayers;
}

void CUnfinishedMapVote::EnsureLocalPlayerSelected()
{
	if(m_LocalPlayerAutoSelected)
		return;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0)
		return;
	const char *pName = GameClient()->m_aClients[LocalId].m_aRealName;
	if(!pName[0])
		return;
	m_SelectedPlayers.emplace(pName);
	m_LocalPlayerAutoSelected = true;
	m_RemainingDirty = true;
}

std::vector<std::string> CUnfinishedMapVote::SelectedPlayerNames() const
{
	std::vector<std::string> vNames;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		const char *pName = GameClient()->m_aClients[i].m_aRealName;
		if(!pName[0] || !IsPlayerSelected(pName))
			continue;
		if(std::find(vNames.begin(), vNames.end(), pName) == vNames.end())
			vNames.emplace_back(pName);
	}
	return vNames;
}

bool CUnfinishedMapVote::CanStart()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;
	if(m_VotePending)
	{
		GameClient()->Echo("Unfinished map vote: already looking up maps, please wait.");
		return false;
	}

	str_copy(m_aCurrentMap, Client()->ServerInfo().m_aMap);
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
		const char *pName = GameClient()->m_aClients[i].m_aRealName;
		if(!pName[0])
			continue;
		if(std::find(m_vPlayerNames.begin(), m_vPlayerNames.end(), pName) == m_vPlayerNames.end())
			m_vPlayerNames.emplace_back(pName);
	}

	Launch(pReason);
}

void CUnfinishedMapVote::StartSelected(const char *pReason)
{
	if(!CanStart())
		return;

	m_vPlayerNames = SelectedPlayerNames();
	if(m_vPlayerNames.empty())
	{
		GameClient()->Echo("Unfinished map vote: no players selected, click the tees above the vote list to select them.");
		return;
	}

	Launch(pReason);
}

void CUnfinishedMapVote::Launch(const char *pReason)
{
	str_copy(m_aReason, pReason);
	m_FailedPlayers.clear();
	m_ServerTypeFailed = false;
	m_VotePending = true;
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
		const char *pDescription = str_skip_whitespaces_const(pOption->m_aDescription);
		if(!str_startswith(pDescription, "☒") && !str_startswith(pDescription, "☑"))
			continue;
		char aNormalized[VOTE_DESC_LENGTH];
		NormalizeVoteDescription(aNormalized, sizeof(aNormalized), pDescription);
		if(!str_find(aNormalized, "maps"))
			continue;
		for(const auto &Category : s_aCategories)
		{
			if(str_find(aNormalized, Category.m_pNormalized))
			{
				if(str_comp(m_aServerType, Category.m_pType) != 0)
				{
					str_copy(m_aServerType, Category.m_pType);
					m_PlayerStats.clear();
					m_FailedPlayers.clear();
					m_vRemainingMaps.clear();
					m_RemainingDirty = true;
				}
				m_ServerTypeFailed = false;
				if(m_pMapInfoRequest)
				{
					m_pMapInfoRequest->Abort();
					m_pMapInfoRequest = nullptr;
				}
				return true;
			}
		}
	}
	return false;
}

void CUnfinishedMapVote::UpdateServerType()
{
	if(DetermineServerTypeFromVoteList())
		return;

	if(m_aServerType[0] != '\0' || m_ServerTypeFailed)
		return;

	if(!m_aCurrentMap[0])
	{
		str_copy(m_aCurrentMap, Client()->ServerInfo().m_aMap);
		if(!m_aCurrentMap[0])
			return;
	}

	if(!m_pMapInfoRequest)
	{
		char aEscaped[2 * MAX_MAP_LENGTH + 64];
		EscapeUrl(aEscaped, m_aCurrentMap);
		char aUrl[512];
		str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
		m_pMapInfoRequest = RunRequest(aUrl);
		return;
	}

	if(!m_pMapInfoRequest->Done())
		return;

	if(m_pMapInfoRequest->State() == EHttpState::DONE)
	{
		json_value *pJson = m_pMapInfoRequest->ResultJson();
		if(pJson)
		{
			const json_value *pType = json_object_get(pJson, "type");
			if(pType->type == json_string)
				str_copy(m_aServerType, json_string_get(pType));
			json_value_free(pJson);
		}
	}
	m_pMapInfoRequest = nullptr;
	m_ServerTypeFailed = m_aServerType[0] == '\0';
	m_RemainingDirty = true;
}

std::shared_ptr<IHttpRequest> CUnfinishedMapVote::RunRequest(const char *pUrl, int64_t MaxResponseSize)
{
	std::shared_ptr<IHttpRequest> pRequest = HttpGet(pUrl);
	pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	pRequest->MaxResponseSize(MaxResponseSize);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(pRequest);
	return pRequest;
}

void CUnfinishedMapVote::UpdateMapReleases()
{
	PollMapReleases();
	if(m_pMapReleasesRequest || !m_vMapReleases.empty() || m_MapReleasesFailed)
		return;
	m_pMapReleasesRequest = RunRequest("https://ddnet.org/releases/maps.json", 2 * 1024 * 1024);
}

void CUnfinishedMapVote::PollMapReleases()
{
	if(!m_pMapReleasesRequest || !m_pMapReleasesRequest->Done())
		return;

	json_value *pJson = m_pMapReleasesRequest->State() == EHttpState::DONE ? m_pMapReleasesRequest->ResultJson() : nullptr;
	if(pJson && pJson->type == json_array)
	{
		m_vMapReleases.reserve(json_array_length(pJson));
		for(int MapIndex = 0; MapIndex < json_array_length(pJson); MapIndex++)
		{
			const json_value *pMap = json_array_get(pJson, MapIndex);
			if(!pMap || pMap->type != json_object)
				continue;
			const json_value *pName = json_object_get(pMap, "name");
			const json_value *pType = json_object_get(pMap, "type");
			const json_value *pThumbnail = json_object_get(pMap, "thumbnail");
			if(pName->type != json_string || pType->type != json_string || pThumbnail->type != json_string)
				continue;

			SMapRelease Release;
			Release.m_Name = json_string_get(pName);
			Release.m_Type = json_string_get(pType);
			Release.m_ThumbnailUrl = json_string_get(pThumbnail);
			const json_value *pTiles = json_object_get(pMap, "tiles");
			if(pTiles->type == json_array)
			{
				int NumShownTags = 0;
				for(int TileIndex = 0; TileIndex < json_array_length(pTiles); TileIndex++)
				{
					const json_value *pTile = json_array_get(pTiles, TileIndex);
					if(!pTile || pTile->type != json_string)
						continue;
					const char *pTag = json_string_get(pTile);
					Release.m_vTags.emplace_back(pTag);
					if(NumShownTags < 5)
					{
						if(!Release.m_Tags.empty())
							Release.m_Tags.append(" · ");
						Release.m_Tags.append(pTag);
						NumShownTags++;
					}
				}
			}
			m_MapReleaseIndices[Release.m_Name] = m_vMapReleases.size();
			m_vMapReleases.push_back(std::move(Release));
		}
	}
	if(pJson)
		json_value_free(pJson);
	m_pMapReleasesRequest = nullptr;
	m_MapReleasesFailed = m_vMapReleases.empty();
}

const CUnfinishedMapVote::SMapRelease *CUnfinishedMapVote::FindMapRelease(const char *pMapName)
{
	if(!pMapName || !pMapName[0])
		return nullptr;
	const auto It = m_MapReleaseIndices.find(pMapName);
	if(It != m_MapReleaseIndices.end())
		return &m_vMapReleases[It->second];
	for(int Index = 0; Index < (int)m_vMapReleases.size(); Index++)
	{
		if(str_comp_nocase(m_vMapReleases[Index].m_Name.c_str(), pMapName) == 0)
			return &m_vMapReleases[Index];
	}
	return nullptr;
}

const CUnfinishedMapVote::SMapRelease *CUnfinishedMapVote::FindMapReleaseForVote(const char *pDescription)
{
	if(m_vMapReleases.empty())
		return nullptr;
	const auto Cached = m_VoteDescriptionReleaseCache.find(pDescription);
	if(Cached != m_VoteDescriptionReleaseCache.end())
		return Cached->second >= 0 ? &m_vMapReleases[Cached->second] : nullptr;

	int BestIndex = -1;
	for(int Index = 0; Index < (int)m_vMapReleases.size(); Index++)
	{
		if((DescriptionMatchesMap(pDescription, m_vMapReleases[Index].m_Name.c_str()) || NormalizedDescriptionMatchesMap(pDescription, m_vMapReleases[Index].m_Name.c_str())) &&
			(BestIndex < 0 || m_vMapReleases[Index].m_Name.length() > m_vMapReleases[BestIndex].m_Name.length()))
			BestIndex = Index;
	}
	m_VoteDescriptionReleaseCache[pDescription] = BestIndex;
	return BestIndex >= 0 ? &m_vMapReleases[BestIndex] : nullptr;
}

IGraphics::CTextureHandle CUnfinishedMapVote::RequestMapPreview(const SMapRelease *pRelease)
{
	if(!pRelease || pRelease->m_ThumbnailUrl.empty())
		return {};
	SMapPreview &Preview = m_MapPreviews[pRelease->m_Name];
	if(Preview.m_Texture.IsValid() || Preview.m_pRequest || Preview.m_Failed)
		return Preview.m_Texture;

	int ActiveRequests = 0;
	for(const auto &[Name, OtherPreview] : m_MapPreviews)
		ActiveRequests += OtherPreview.m_pRequest != nullptr;
	if(ActiveRequests >= 2)
		return {};

	Preview.m_pRequest = RunRequest(pRelease->m_ThumbnailUrl.c_str(), 4 * 1024 * 1024);
	return {};
}

void CUnfinishedMapVote::PollMapPreviews()
{
	// Decode at most one image per frame to avoid a visible hitch when several downloads finish together.
	for(auto &[Name, Preview] : m_MapPreviews)
	{
		if(!Preview.m_pRequest || !Preview.m_pRequest->Done())
			continue;
		if(Preview.m_pRequest->State() == EHttpState::DONE && Preview.m_pRequest->StatusCode() < 400)
		{
			unsigned char *pData;
			size_t DataSize;
			Preview.m_pRequest->Result(&pData, &DataSize);
			CImageInfo ImageInfo;
			if(Graphics()->LoadPng(ImageInfo, pData, DataSize, Name.c_str()))
				Preview.m_Texture = Graphics()->LoadTextureRawMove(ImageInfo, 0, Name.c_str());
		}
		Preview.m_Failed = !Preview.m_Texture.IsValid();
		Preview.m_pRequest = nullptr;
		break;
	}
}

void CUnfinishedMapVote::RequestPlayer(const char *pName)
{
	// the stats say which maps of this type exist, so the type has to be known first
	if(!pName[0] || !m_aServerType[0])
		return;
	if(m_PlayerStats.contains(pName) || m_FailedPlayers.contains(pName) || m_PlayerRequests.contains(pName))
		return;

	char aEscaped[2 * MAX_NAME_LENGTH + 64];
	EscapeUrl(aEscaped, pName);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);
	m_PlayerRequests[pName] = RunRequest(aUrl);
}

bool CUnfinishedMapVote::PlayerReady(const char *pName) const
{
	return m_PlayerStats.contains(pName) || m_FailedPlayers.contains(pName);
}

void CUnfinishedMapVote::PollPlayerRequests()
{
	for(auto Request = m_PlayerRequests.begin(); Request != m_PlayerRequests.end();)
	{
		if(!Request->second->Done())
		{
			++Request;
			continue;
		}

		json_value *pJson = Request->second->State() == EHttpState::DONE ? Request->second->ResultJson() : nullptr;
		if(pJson)
		{
			ParsePlayerStats(Request->first.c_str(), pJson);
			json_value_free(pJson);
		}
		else
		{
			m_FailedPlayers.emplace(Request->first);
		}
		m_RemainingDirty = true;
		Request = m_PlayerRequests.erase(Request);
	}
}

void CUnfinishedMapVote::ParsePlayerStats(const char *pName, const _json_value *pJson)
{
	SPlayerStats &Stats = m_PlayerStats[pName];
	const json_value *pTypes = json_object_get(pJson, "types");
	if(pTypes->type != json_object)
		return;

	for(unsigned TypeIndex = 0; TypeIndex < pTypes->u.object.length; TypeIndex++)
	{
		if(!str_startswith(pTypes->u.object.values[TypeIndex].name, m_aServerType))
			continue;
		const json_value *pMaps = json_object_get(pTypes->u.object.values[TypeIndex].value, "maps");
		if(pMaps->type != json_object)
			continue;
		for(unsigned MapIndex = 0; MapIndex < pMaps->u.object.length; MapIndex++)
		{
			const char *pMapName = pMaps->u.object.values[MapIndex].name;
			Stats.m_TypeMaps.emplace(pMapName);
			const json_value *pFinishes = json_object_get(pMaps->u.object.values[MapIndex].value, "finishes");
			if(pFinishes->type == json_integer && json_int_get(pFinishes) > 0)
				Stats.m_FinishedMaps.emplace(pMapName);
		}
	}
}

void CUnfinishedMapVote::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	PollMapReleases();
	PollMapPreviews();
	PollPlayerRequests();
	if(m_VotePending)
		UpdateVote();
}

void CUnfinishedMapVote::UpdateVote()
{
	UpdateServerType();
	if(m_ServerTypeFailed)
	{
		Stop("Unfinished map vote: the current map is unknown to ddnet.org, couldn't determine the server type.");
		return;
	}
	if(!m_aServerType[0])
		return;

	for(const std::string &Name : m_vPlayerNames)
		RequestPlayer(Name.c_str());

	for(const std::string &Name : m_vPlayerNames)
	{
		if(m_FailedPlayers.contains(Name))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: failed to fetch ddnet.org stats of '%s'.", Name.c_str());
			Stop(aBuf);
			return;
		}
		if(!PlayerReady(Name.c_str()))
			return;
	}

	Analyze();
}

void CUnfinishedMapVote::Analyze()
{
	std::set<std::string> AllTypeMaps;
	std::map<std::string, std::vector<std::string>> MapFinishers;
	for(const std::string &Name : m_vPlayerNames)
	{
		const auto Stats = m_PlayerStats.find(Name);
		if(Stats == m_PlayerStats.end())
			continue;
		AllTypeMaps.insert(Stats->second.m_TypeMaps.begin(), Stats->second.m_TypeMaps.end());
		for(const std::string &Map : Stats->second.m_FinishedMaps)
			MapFinishers[Map].push_back(Name);
	}

	int StarsFilter = -1;
	if(m_aReason[0] >= '1' && m_aReason[0] <= '5' && m_aReason[1] == '\0')
		StarsFilter = m_aReason[0] - '0';

	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const char *pLocalName = LocalId >= 0 ? GameClient()->m_aClients[LocalId].m_aRealName : "";
	const bool LocalSelected = pLocalName[0] && std::find(m_vPlayerNames.begin(), m_vPlayerNames.end(), pLocalName) != m_vPlayerNames.end();

	struct SVotableMap
	{
		const std::string *m_pMap;
		int m_OptionIndex;
		int m_Unfinished;
		bool m_LocalUnfinished;
	};
	std::vector<SVotableMap> vVotable;
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
		if(pBestMap == nullptr || str_comp(pBestMap->c_str(), m_aCurrentMap) == 0)
			continue;
		if(StarsFilter != -1 && VoteDescriptionStars(pOption->m_aDescription) != StarsFilter)
		{
			NumWrongStars++;
			continue;
		}
		if(!TakenMaps.emplace(*pBestMap).second)
			continue;

		const auto FinisherIt = MapFinishers.find(*pBestMap);
		const int FinishedCount = FinisherIt == MapFinishers.end() ? 0 : (int)FinisherIt->second.size();
		const bool LocalUnfinished = !pLocalName[0] || FinisherIt == MapFinishers.end() ||
					     std::find(FinisherIt->second.begin(), FinisherIt->second.end(), pLocalName) == FinisherIt->second.end();
		vVotable.push_back({pBestMap, OptionIndex, (int)m_vPlayerNames.size() - FinishedCount, LocalUnfinished});
	}

	if(vVotable.empty())
	{
		char aBuf[256];
		if(NumWrongStars > 0)
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: no %s map in the vote list has %d★.", m_aServerType, StarsFilter);
		else
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: couldn't find any %s maps in the vote list.", m_aServerType);
		Stop(aBuf);
		return;
	}

	const int PoolSize = (int)m_vPlayerNames.size();

	// every selected player has unfinished
	std::vector<int> vCommon;
	for(int i = 0; i < (int)vVotable.size(); i++)
		if(vVotable[i].m_Unfinished == PoolSize)
			vCommon.push_back(i);
	if(!vCommon.empty())
	{
		const SVotableMap &Pick = vVotable[vCommon[secure_rand_below((int)vCommon.size())]];
		GameClient()->m_Voting.CallvoteOption(Pick.m_OptionIndex, m_aReason);
		Stop(nullptr);
		return;
	}

	// no map is unfinished by everyone
	std::map<std::string, int> FinishedVotableCount;
	for(const SVotableMap &Votable : vVotable)
	{
		const auto FinisherIt = MapFinishers.find(*Votable.m_pMap);
		if(FinisherIt == MapFinishers.end())
			continue;
		for(const std::string &Finisher : FinisherIt->second)
			FinishedVotableCount[Finisher]++;
	}
	std::vector<std::string> vFullFinishers;
	for(const std::string &Name : m_vPlayerNames)
	{
		const auto CountIt = FinishedVotableCount.find(Name);
		if(CountIt != FinishedVotableCount.end() && CountIt->second == (int)vVotable.size())
			vFullFinishers.push_back(Name);
	}
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
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "Unfinished map vote: %s finished every votable %s map, exclude %s to vote an unfinished one.", aNames, m_aServerType, vFullFinishers.size() == 1 ? "that player" : "those players");
		Stop(aBuf);
		return;
	}

	// nobody shares an unfinished map, vote the one the most selected players still have unfinished
	int BestUnfinished = -1;
	std::vector<int> vBestMaps;
	for(int i = 0; i < (int)vVotable.size(); i++)
	{
		if(LocalSelected && !vVotable[i].m_LocalUnfinished)
			continue;
		if(vVotable[i].m_Unfinished > BestUnfinished)
		{
			BestUnfinished = vVotable[i].m_Unfinished;
			vBestMaps.assign(1, i);
		}
		else if(vVotable[i].m_Unfinished == BestUnfinished)
			vBestMaps.push_back(i);
	}
	if(vBestMaps.empty())
	{
		Stop("Unfinished map vote: couldn't find a suitable map to vote.");
		return;
	}

	const SVotableMap &Pick = vVotable[vBestMaps[secure_rand_below((int)vBestMaps.size())]];
	GameClient()->m_Voting.CallvoteOption(Pick.m_OptionIndex, m_aReason);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Unfinished map vote: no map is unfinished by everyone, voting '%s' which %d of %d players haven't finished.", Pick.m_pMap->c_str(), BestUnfinished, PoolSize);
	Stop(aBuf);
}

void CUnfinishedMapVote::UpdateRemainingMaps()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	const std::vector<std::string> vNames = SelectedPlayerNames();
	if(!vNames.empty())
		UpdateServerType();

	if(vNames != m_vRemainingNames)
	{
		m_vRemainingNames = vNames;
		m_RemainingDirty = true;
	}
	if(m_RemainingNumOptions != GameClient()->m_Voting.NumOptions())
	{
		m_RemainingNumOptions = GameClient()->m_Voting.NumOptions();
		m_RemainingDirty = true;
	}

	m_RemainingLoading = !vNames.empty() && !m_aServerType[0] && !m_ServerTypeFailed;
	for(const std::string &Name : vNames)
	{
		RequestPlayer(Name.c_str());
		if(!PlayerReady(Name.c_str()))
			m_RemainingLoading = true;
	}

	if(m_RemainingDirty && !m_RemainingLoading)
		RecomputeRemainingMaps(vNames);
}

void CUnfinishedMapVote::RecomputeRemainingMaps(const std::vector<std::string> &vNames)
{
	m_RemainingDirty = false;
	m_vRemainingMaps.clear();
	if(vNames.empty() || !m_aServerType[0])
		return;

	const std::set<std::string> *pTypeMaps = nullptr;
	for(const std::string &Name : vNames)
	{
		const auto Stats = m_PlayerStats.find(Name);
		if(Stats != m_PlayerStats.end() && !Stats->second.m_TypeMaps.empty())
		{
			pTypeMaps = &Stats->second.m_TypeMaps;
			break;
		}
	}
	if(pTypeMaps == nullptr)
		return;

	std::set<std::string> TakenMaps;
	int OptionIndex = 0;
	for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, OptionIndex++)
	{
		const std::string *pBestMap = nullptr;
		for(const std::string &Map : *pTypeMaps)
		{
			if((!pBestMap || Map.length() > pBestMap->length()) && DescriptionMatchesMap(pOption->m_aDescription, Map.c_str()))
				pBestMap = &Map;
		}
		if(pBestMap == nullptr || str_comp(pBestMap->c_str(), m_aCurrentMap) == 0)
			continue;
		if(!TakenMaps.emplace(*pBestMap).second)
			continue;

		bool Unfinished = true;
		for(const std::string &Name : vNames)
		{
			const auto Stats = m_PlayerStats.find(Name);
			if(Stats != m_PlayerStats.end() && Stats->second.m_FinishedMaps.contains(*pBestMap))
			{
				Unfinished = false;
				break;
			}
		}
		if(Unfinished)
			m_vRemainingMaps.push_back({OptionIndex, *pBestMap, pOption->m_aDescription, VoteDescriptionInfo(pOption->m_pNext)});
	}
}

void CUnfinishedMapVote::OnShutdown()
{
	if(m_pMapInfoRequest)
		m_pMapInfoRequest->Abort();
	if(m_pMapReleasesRequest)
		m_pMapReleasesRequest->Abort();
	for(auto &[Name, pRequest] : m_PlayerRequests)
		pRequest->Abort();
	for(auto &[Name, Preview] : m_MapPreviews)
	{
		if(Preview.m_pRequest)
			Preview.m_pRequest->Abort();
		if(Preview.m_Texture.IsValid())
			Graphics()->UnloadTexture(&Preview.m_Texture);
	}
}

void CUnfinishedMapVote::Stop(const char *pErrorMessage)
{
	m_vPlayerNames.clear();
	m_VotePending = false;
	if(pErrorMessage)
		GameClient()->Echo(pErrorMessage);
}

void CUnfinishedMapVote::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
		return;

	Stop(nullptr);
	if(m_pMapInfoRequest)
	{
		m_pMapInfoRequest->Abort();
		m_pMapInfoRequest = nullptr;
	}
	for(auto &[Name, pRequest] : m_PlayerRequests)
		pRequest->Abort();
	m_PlayerRequests.clear();
	// another server means another map type
	m_PlayerStats.clear();
	m_FailedPlayers.clear();
	m_aServerType[0] = '\0';
	m_ServerTypeFailed = false;
	m_aCurrentMap[0] = '\0';
	m_SelectedPlayers.clear();
	m_LocalPlayerAutoSelected = false;
	m_vRemainingMaps.clear();
	m_vRemainingNames.clear();
	m_RemainingDirty = true;
	m_RemainingLoading = false;
	m_RemainingNumOptions = -1;
}
