
#include "finish_rename.h"

#include <base/str.h>

#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>
#include <game/client/race.h>
#include <game/mapitems.h>

const char *CFinishRename::ActiveName() const
{
	return g_Config.m_ClDummy ? Client()->DummyName() : Client()->PlayerName();
}

std::vector<std::string> CFinishRename::AlternativeNames() const
{
	std::vector<std::string> vNames;
	const char *pList = g_Config.m_ClFinishRenameNames;
	while(*pList && vNames.size() < 16)
	{
		const char *pComma = str_find(pList, ",");
		std::string Name = pComma ? std::string(pList, pComma - pList) : std::string(pList);
		pList = pComma ? pComma + 1 : pList + Name.length();

		const size_t Begin = Name.find_first_not_of(' ');
		const size_t End = Name.find_last_not_of(' ');
		if(Begin == std::string::npos)
			continue;
		Name = Name.substr(Begin, End - Begin + 1);
		if(Name.length() >= MAX_NAME_LENGTH)
			Name.resize(MAX_NAME_LENGTH - 1);
		vNames.push_back(Name);
	}
	return vNames;
}

void CFinishRename::ResetMapState()
{
	RestoreName(false);
	for(auto &[Name, Lookup] : m_Lookups)
	{
		if(Lookup.m_pRequest)
			Lookup.m_pRequest->Abort();
	}
	m_Lookups.clear();
	m_aMap[0] = '\0';
	m_MapScanned = false;
	m_vFinishTilePositions.clear();
	m_Armed = true;
	m_Decided = false;
	m_aTargetName[0] = '\0';
	m_DecisionInputs.clear();
}

void CFinishRename::OnMapLoad()
{
	ResetMapState();
}

void CFinishRename::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		ResetMapState();
}

void CFinishRename::ScanFinishTiles()
{
	m_MapScanned = true;
	const int MapSize = Collision()->GetWidth() * Collision()->GetHeight();
	for(int Index = 0; Index < MapSize; Index++)
	{
		if(Collision()->GetTileIndex(Index) == TILE_FINISH || Collision()->GetFrontTileIndex(Index) == TILE_FINISH)
			m_vFinishTilePositions.push_back(Collision()->GetPos(Index));
	}
}

void CFinishRename::EnsureLookup(const char *pName)
{
	if(!pName[0] || m_Lookups.contains(pName))
		return;

	char aEscaped[2 * MAX_NAME_LENGTH + 64];
	EscapeUrl(aEscaped, pName);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);

	SLookup &Lookup = m_Lookups[pName];
	Lookup.m_pRequest = HttpGet(aUrl);
	Lookup.m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	Lookup.m_pRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(Lookup.m_pRequest);
}

void CFinishRename::UpdateLookups()
{
	for(auto &[Name, Lookup] : m_Lookups)
	{
		if(!Lookup.m_pRequest || !Lookup.m_pRequest->Done())
			continue;
		if(Lookup.m_pRequest->State() != EHttpState::DONE)
		{
			Lookup.m_Status = STATUS_FAILED;
		}
		else
		{
			json_value *pJson = Lookup.m_pRequest->ResultJson();
			Lookup.m_Status = pJson ? StatusFromJson(pJson) : STATUS_FAILED;
			if(pJson)
				json_value_free(pJson);
		}
		Lookup.m_pRequest = nullptr;
	}
}

CFinishRename::EStatus CFinishRename::StatusFromJson(const _json_value *pJson) const
{
	const json_value *pTypes = json_object_get(pJson, "types");
	if(pTypes->type != json_object)
		return STATUS_UNFINISHED;
	for(unsigned TypeIndex = 0; TypeIndex < pTypes->u.object.length; TypeIndex++)
	{
		const json_value *pMap = json_object_get(json_object_get(pTypes->u.object.values[TypeIndex].value, "maps"), m_aMap);
		if(pMap->type != json_object)
			continue;
		const json_value *pFinishes = json_object_get(pMap, "finishes");
		return pFinishes->type == json_integer && json_int_get(pFinishes) > 0 ? STATUS_FINISHED : STATUS_UNFINISHED;
	}
	return STATUS_UNFINISHED;
}

CFinishRename::EStatus CFinishRename::LookupStatus(const char *pName) const
{
	const auto Lookup = m_Lookups.find(pName);
	return Lookup == m_Lookups.end() ? STATUS_PENDING : Lookup->second.m_Status;
}

float CFinishRename::DistanceToFinish(vec2 Pos) const
{
	float Nearest = -1.0f;
	for(const vec2 &TilePos : m_vFinishTilePositions)
	{
		const float Distance = distance(Pos, TilePos);
		if(Nearest < 0.0f || Distance < Nearest)
			Nearest = Distance;
	}
	return Nearest;
}

void CFinishRename::OnRender()
{
	if(!g_Config.m_ClFinishRename || Client()->State() != IClient::STATE_ONLINE)
		return;

	if(m_aMap[0] == '\0')
	{
		CServerInfo ServerInfo;
		Client()->GetServerInfo(&ServerInfo);
		if(!ServerInfo.m_aMap[0])
			return;
		str_copy(m_aMap, ServerInfo.m_aMap);
	}
	EnsureLookup(ActiveName());
	for(const std::string &Name : AlternativeNames())
		EnsureLookup(Name.c_str());
	UpdateLookups();

	std::string Inputs = std::string(ActiveName()) + "\n" + g_Config.m_ClFinishRenameNames;
	if(Inputs != m_DecisionInputs)
	{
		m_DecisionInputs = Inputs;
		m_Decided = false;
	}
	if(!m_Decided)
		ComputeDecision();

	if(m_RenamedDummy >= 0)
		return;

	if(!m_MapScanned)
		ScanFinishTiles();
	if(m_vFinishTilePositions.empty())
		return;

	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return;
	const float TriggerDistance = g_Config.m_ClFinishRenameDistance * 32.0f;
	const float Distance = DistanceToFinish(GameClient()->m_LocalCharacterPos);
	if(Distance > 1.5f * TriggerDistance)
	{
		m_Armed = true;
		return;
	}
	if(Distance > TriggerDistance || !m_Armed)
		return;

	m_Armed = false;
	if(m_Decided && m_aTargetName[0] != '\0')
		Rename(m_aTargetName);
}

void CFinishRename::ComputeDecision()
{
	const EStatus CurrentStatus = LookupStatus(ActiveName());
	if(CurrentStatus == STATUS_PENDING)
		return;

	m_aTargetName[0] = '\0';
	if(CurrentStatus == STATUS_FINISHED)
	{
		for(const std::string &Name : AlternativeNames())
		{
			if(str_comp(Name.c_str(), ActiveName()) == 0)
				continue;
			const EStatus Status = LookupStatus(Name.c_str());
			if(Status == STATUS_PENDING)
				return;
			if(Status == STATUS_UNFINISHED)
			{
				str_copy(m_aTargetName, Name.c_str());
				break;
			}
		}
	}
	m_Decided = true;
}

void CFinishRename::Rename(const char *pName)
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Finish rename: now playing as '%s'.", pName);
	str_copy(m_aOriginalName, ActiveName());
	m_RenamedDummy = g_Config.m_ClDummy;
	if(g_Config.m_ClDummy)
	{
		str_copy(g_Config.m_ClDummyName, pName);
		GameClient()->SendDummyInfo(false);
	}
	else
	{
		str_copy(g_Config.m_PlayerName, pName);
		GameClient()->SendInfo(false);
	}
	GameClient()->Echo(aBuf);
}

void CFinishRename::RestoreName(bool SendUpdate)
{
	if(m_RenamedDummy < 0)
		return;
	if(m_RenamedDummy == 1)
	{
		str_copy(g_Config.m_ClDummyName, m_aOriginalName);
		if(SendUpdate)
			GameClient()->SendDummyInfo(false);
	}
	else
	{
		str_copy(g_Config.m_PlayerName, m_aOriginalName);
		if(SendUpdate)
			GameClient()->SendInfo(false);
	}
	if(SendUpdate)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Finish rename: switching back to '%s'.", m_aOriginalName);
		GameClient()->Echo(aBuf);
	}
	m_aOriginalName[0] = '\0';
	m_RenamedDummy = -1;
	m_Decided = false;
}

void CFinishRename::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	int FinishedClientId = -1;
	if(MsgType == NETMSGTYPE_SV_RACEFINISH)
	{
		const CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
		for(const int LocalId : GameClient()->m_aLocalIds)
			if(LocalId >= 0 && pMsg->m_ClientId == LocalId)
				FinishedClientId = LocalId;
	}
	else if(MsgType == NETMSGTYPE_SV_CHAT)
	{
		const CNetMsg_Sv_Chat *pMsg = (CNetMsg_Sv_Chat *)pRawMsg;
		char aName[MAX_NAME_LENGTH];
		if(pMsg->m_ClientId == -1 && CRaceHelper::TimeFromFinishMessage(pMsg->m_pMessage, aName, sizeof(aName)) > 0)
		{
			for(const int LocalId : GameClient()->m_aLocalIds)
				if(LocalId >= 0 && str_comp(aName, GameClient()->m_aClients[LocalId].m_aName) == 0)
					FinishedClientId = LocalId;
		}
	}
	if(FinishedClientId < 0)
		return;

	const char *pFinishedName = GameClient()->m_aClients[FinishedClientId].m_aName;
	const auto Lookup = m_Lookups.find(pFinishedName);
	if(Lookup != m_Lookups.end())
		Lookup->second.m_Status = STATUS_FINISHED;

	if(m_RenamedDummy >= 0 && GameClient()->m_aLocalIds[m_RenamedDummy] == FinishedClientId)
		RestoreName(true);
}
