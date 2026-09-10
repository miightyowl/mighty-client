#include "unfinished_map_vote.h"

#include <base/secure.h>
#include <base/str.h>

#include <engine/http.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <map>
#include <set>

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
	{
		m_SelectedPlayers.clear();
	}
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
	m_VotePending = true;
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
			const json_value *pMapper = json_object_get(pMap, "mapper");
			const json_value *pDifficulty = json_object_get(pMap, "difficulty");
			const json_value *pThumbnail = json_object_get(pMap, "thumbnail");
			if(pName->type != json_string || pType->type != json_string || pMapper->type != json_string || pDifficulty->type != json_integer || pThumbnail->type != json_string)
				continue;

			SMapRelease Release;
			Release.m_Name = json_string_get(pName);
			Release.m_Type = json_string_get(pType);
			Release.m_Mapper = json_string_get(pMapper);
			Release.m_Difficulty = json_int_get(pDifficulty);
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
			m_vMapReleases.push_back(std::move(Release));
		}
		std::stable_sort(m_vMapReleases.begin(), m_vMapReleases.end(), [](const SMapRelease &Left, const SMapRelease &Right) {
			const int CaseInsensitive = str_comp_nocase(Left.m_Name.c_str(), Right.m_Name.c_str());
			return CaseInsensitive != 0 ? CaseInsensitive < 0 : str_comp(Left.m_Name.c_str(), Right.m_Name.c_str()) < 0;
		});
		m_MapReleaseIndices.clear();
		for(int Index = 0; Index < (int)m_vMapReleases.size(); Index++)
			m_MapReleaseIndices[m_vMapReleases[Index].m_Name] = Index;
		m_RemainingDirty = true;
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
	for(const SMapRelease &Release : m_vMapReleases)
	{
		if(str_comp_nocase(Release.m_Name.c_str(), pMapName) == 0)
			return &Release;
	}
	return nullptr;
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

const CUnfinishedMapVote::SMapDetails *CUnfinishedMapVote::RequestMapDetails(const SMapRelease *pRelease)
{
	if(!pRelease)
		return nullptr;
	const auto Details = m_MapDetails.find(pRelease->m_Name);
	if(Details != m_MapDetails.end())
		return &Details->second;
	if(m_FailedMapDetails.contains(pRelease->m_Name) || m_MapDetailsRequests.contains(pRelease->m_Name))
		return nullptr;

	if(m_MapDetailsRequests.size() >= 2)
		return nullptr;
	char aEscaped[2 * MAX_MAP_LENGTH + 64];
	EscapeUrl(aEscaped, pRelease->m_Name.c_str());
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/maps/?json=%s", aEscaped);
	m_MapDetailsRequests[pRelease->m_Name] = RunRequest(aUrl, 64 * 1024);
	return nullptr;
}

void CUnfinishedMapVote::PollMapDetails()
{
	for(auto Request = m_MapDetailsRequests.begin(); Request != m_MapDetailsRequests.end();)
	{
		if(!Request->second->Done())
		{
			++Request;
			continue;
		}

		bool Success = false;
		json_value *pJson = Request->second->State() == EHttpState::DONE ? Request->second->ResultJson() : nullptr;
		if(pJson && pJson->type == json_object)
		{
			SMapDetails Details;
			const json_value *pFinishes = json_object_get(pJson, "finishes");
			const json_value *pAverageTime = json_object_get(pJson, "average_time");
			if(pFinishes->type == json_integer)
				Details.m_Finishes = json_int_get(pFinishes);
			if(pAverageTime->type == json_double)
				Details.m_AverageTime = pAverageTime->u.dbl;
			else if(pAverageTime->type == json_integer)
				Details.m_AverageTime = json_int_get(pAverageTime);
			m_MapDetails[Request->first] = Details;
			Success = true;
		}
		if(pJson)
			json_value_free(pJson);
		if(!Success)
			m_FailedMapDetails.emplace(Request->first);
		Request = m_MapDetailsRequests.erase(Request);
		break;
	}
}

void CUnfinishedMapVote::RequestPlayer(const char *pName)
{
	if(!pName[0])
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
		const json_value *pMaps = json_object_get(pTypes->u.object.values[TypeIndex].value, "maps");
		if(pMaps->type != json_object)
			continue;
		for(unsigned MapIndex = 0; MapIndex < pMaps->u.object.length; MapIndex++)
		{
			const char *pMapName = pMaps->u.object.values[MapIndex].name;
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
	PollMapDetails();
	PollPlayerRequests();
	if(m_VotePending)
		UpdateVote();
}

void CUnfinishedMapVote::UpdateVote()
{
	UpdateMapReleases();
	if(m_vMapReleases.empty())
	{
		if(m_MapReleasesFailed)
			Stop("Unfinished map vote: failed to fetch the map catalog from ddnet.org.");
		return;
	}

	const SMapRelease *pCurrentMap = FindMapRelease(m_aCurrentMap);
	if(pCurrentMap == nullptr)
	{
		Stop("Unfinished map vote: the current map is unknown to ddnet.org.");
		return;
	}

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

	int StarsFilter = -1;
	if(m_aReason[0] >= '1' && m_aReason[0] <= '5' && m_aReason[1] == '\0')
		StarsFilter = m_aReason[0] - '0';

	std::vector<const SMapRelease *> vCandidates;
	for(const SMapRelease &Release : m_vMapReleases)
	{
		if(str_comp_nocase(Release.m_Type.c_str(), pCurrentMap->m_Type.c_str()) != 0 ||
			str_comp_nocase(Release.m_Name.c_str(), m_aCurrentMap) == 0 ||
			(StarsFilter >= 0 && Release.m_Difficulty != StarsFilter))
			continue;

		const bool Unfinished = std::all_of(m_vPlayerNames.begin(), m_vPlayerNames.end(), [&](const std::string &Name) {
			const auto Stats = m_PlayerStats.find(Name);
			return Stats == m_PlayerStats.end() || !Stats->second.m_FinishedMaps.contains(Release.m_Name);
		});
		if(Unfinished)
			vCandidates.push_back(&Release);
	}

	if(vCandidates.empty())
	{
		char aBuf[256];
		if(StarsFilter >= 0)
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: no unfinished %s map has %d★.", pCurrentMap->m_Type.c_str(), StarsFilter);
		else
			str_format(aBuf, sizeof(aBuf), "Unfinished map vote: no unfinished %s map matches all selected players.", pCurrentMap->m_Type.c_str());
		Stop(aBuf);
		return;
	}

	const SMapRelease *pPick = vCandidates[secure_rand_below((int)vCandidates.size())];
	char aCommand[MAX_MAP_LENGTH + 8];
	str_format(aCommand, sizeof(aCommand), "/map %s", pPick->m_Name.c_str());
	GameClient()->m_Chat.SendChat(0, aCommand);
	Stop(nullptr);
}

void CUnfinishedMapVote::UpdateRemainingMaps()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	UpdateMapReleases();
	const std::vector<std::string> vNames = SelectedPlayerNames();
	if(vNames != m_vRemainingNames)
	{
		m_vRemainingNames = vNames;
		m_RemainingDirty = true;
	}

	m_RemainingLoading = !vNames.empty() && m_vMapReleases.empty() && !m_MapReleasesFailed;
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
	m_RemainingMapNames.clear();
	if(vNames.empty() || m_vMapReleases.empty())
		return;

	for(const SMapRelease &Release : m_vMapReleases)
	{
		bool Unfinished = true;
		for(const std::string &Name : vNames)
		{
			const auto Stats = m_PlayerStats.find(Name);
			if(Stats != m_PlayerStats.end() && Stats->second.m_FinishedMaps.contains(Release.m_Name))
			{
				Unfinished = false;
				break;
			}
		}
		if(Unfinished)
		{
			m_RemainingMapNames.emplace(Release.m_Name);
		}
	}
}

void CUnfinishedMapVote::OnShutdown()
{
	if(m_pMapReleasesRequest)
		m_pMapReleasesRequest->Abort();
	for(auto &[Name, pRequest] : m_PlayerRequests)
		pRequest->Abort();
	for(auto &[Name, pRequest] : m_MapDetailsRequests)
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
	for(auto &[Name, pRequest] : m_PlayerRequests)
		pRequest->Abort();
	m_PlayerRequests.clear();
	m_PlayerStats.clear();
	m_FailedPlayers.clear();
	m_aCurrentMap[0] = '\0';
	m_SelectedPlayers.clear();
	m_LocalPlayerAutoSelected = false;
	m_RemainingMapNames.clear();
	m_vRemainingNames.clear();
	m_RemainingDirty = true;
	m_RemainingLoading = false;
}
