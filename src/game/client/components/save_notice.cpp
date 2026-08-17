#include "save_notice.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <game/client/components/chat.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

static const char *CsvReadField(const char *pLine, char *pBuffer, int BufferSize)
{
	int Length = 0;
	const auto Append = [&](char Char) {
		if(Length + 1 < BufferSize)
		{
			pBuffer[Length++] = Char;
		}
	};

	if(*pLine == '"')
	{
		++pLine;
		while(*pLine != '\0')
		{
			if(*pLine == '"')
			{
				if(pLine[1] != '"')
				{
					++pLine;
					break;
				}
				Append('"');
				pLine += 2;
				continue;
			}
			Append(*pLine);
			++pLine;
		}
		while(*pLine != '\0' && *pLine != ',')
		{
			++pLine;
		}
	}
	else
	{
		while(*pLine != '\0' && *pLine != ',')
		{
			Append(*pLine);
			++pLine;
		}
	}

	pBuffer[Length] = '\0';
	return *pLine == ',' ? pLine + 1 : nullptr;
}

void CSaveNotice::OnConsoleInit()
{
	Console()->Register("show_saves", "", CFGFLAG_CLIENT, ConShowSaves, this, "Show your save codes for the current map in chat");
}

void CSaveNotice::ConShowSaves(IConsole::IResult *pResult, void *pUserData)
{
	CSaveNotice *pThis = static_cast<CSaveNotice *>(pUserData);
	if(!pThis->GameClient()->Map()->IsLoaded())
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "save_notice", "no map is loaded");
		return;
	}
	pThis->ShowSaves(true);
}

void CSaveNotice::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
	{
		m_Pending = false;
	}
}

void CSaveNotice::OnMapLoad()
{
	m_Pending = true;
}

void CSaveNotice::OnRender()
{
	if(!m_Pending || Client()->State() != IClient::STATE_ONLINE)
	{
		return;
	}
	m_Pending = false;

	ShowSaves(false);
}

void CSaveNotice::CollectSaves(const char *pMap, std::vector<SSave> &vSaves) const
{
	CLineReader LineReader;
	if(!LineReader.OpenFile(Storage()->OpenFile(SAVES_FILE, IOFLAG_READ, IStorage::TYPE_SAVE)))
	{
		return;
	}

	while(const char *pLine = LineReader.Get())
	{
		char aTimestamp[32];
		char aPlayers[512];
		char aMap[MAX_MAP_LENGTH];
		char aCode[128];

		const char *pNext = CsvReadField(pLine, aTimestamp, sizeof(aTimestamp));
		if(!pNext)
			continue;
		pNext = CsvReadField(pNext, aPlayers, sizeof(aPlayers));
		if(!pNext)
			continue;
		pNext = CsvReadField(pNext, aMap, sizeof(aMap));
		if(!pNext)
			continue;
		CsvReadField(pNext, aCode, sizeof(aCode));

		if(aCode[0] == '\0' || (pMap != nullptr && str_comp_nocase(aMap, pMap) != 0))
		{
			continue;
		}
		if(str_comp(aTimestamp, "Time") == 0 && str_comp(aCode, "Code") == 0)
		{
			continue;
		}

		const auto Duplicate = std::find_if(vSaves.begin(), vSaves.end(), [&](const SSave &Save) {
			return str_comp(Save.m_Code.c_str(), aCode) == 0 && str_comp(Save.m_Players.c_str(), aPlayers) == 0 && str_comp(Save.m_Map.c_str(), aMap) == 0;
		});
		if(Duplicate != vSaves.end())
		{
			vSaves.erase(Duplicate);
		}
		vSaves.push_back({aTimestamp, aPlayers, aMap, aCode});
	}
}

void CSaveNotice::AllSaves(std::vector<SSave> &vSaves) const
{
	CollectSaves(nullptr, vSaves);
}

void CSaveNotice::ShowSaves(bool Verbose)
{
	const char *pMap = GameClient()->Map()->BaseName();
	if(pMap[0] == '\0')
	{
		return;
	}

	std::vector<SSave> vSaves;
	CollectSaves(pMap, vSaves);

	if(Verbose)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, SAVES_FILE, aPath, sizeof(aPath));
		char aInfo[IO_MAX_PATH_LENGTH + 128];
		str_format(aInfo, sizeof(aInfo), "map '%s': found %d save(s) in '%s'", pMap, (int)vSaves.size(), aPath);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "save_notice", aInfo);
	}

	if(vSaves.empty())
	{
		return;
	}

	char aBuf[1024];

	if(g_Config.m_ClMClientSaveNoticeHideCode)
	{
		if(vSaves.size() == 1)
		{
			str_copy(aBuf, Localize("You have a save on this map"));
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), Localize("You have %d saves on this map"), (int)vSaves.size());
		}
		GameClient()->m_Chat.Echo(aBuf);
		return;
	}

	if(vSaves.size() == 1)
	{
		str_copy(aBuf, Localize("You have a save on this map:"));
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), Localize("You have %d saves on this map:"), (int)vSaves.size());
	}
	GameClient()->m_Chat.Echo(aBuf);

	for(const SSave &Save : vSaves)
	{
		if(Save.m_Players.empty())
		{
			str_format(aBuf, sizeof(aBuf), "%s: /load %s", Save.m_Timestamp.c_str(), Save.m_Code.c_str());
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s (%s): /load %s", Save.m_Timestamp.c_str(), Save.m_Players.c_str(), Save.m_Code.c_str());
		}
		GameClient()->m_Chat.Echo(aBuf);
	}
}
