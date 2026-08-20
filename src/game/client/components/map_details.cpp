#include "map_details.h"

#include <base/str.h>

#include <game/client/gameclient.h>

static int ParseStars(const char *pDescription)
{
	for(const char *pStart = pDescription; *pStart != '\0'; pStart++)
	{
		int Stars = 0;
		int Count = 0;
		const char *pRest = pStart;
		while(Count < MAX_MAP_STARS)
		{
			const char *pFilled = str_startswith(pRest, "★");
			const char *pEmpty = pFilled == nullptr ? str_startswith(pRest, "✰") : nullptr;
			if(pFilled == nullptr && pEmpty == nullptr)
				break;
			Stars += pFilled == nullptr ? 0 : 1;
			Count++;
			pRest = pFilled == nullptr ? pEmpty : pFilled;
		}
		if(Count == MAX_MAP_STARS)
			return Stars;
	}
	return -1;
}

static int ParseMedianTime(const char *pDescription)
{
	const char *pEnd = str_find(pDescription, " median");
	if(pEnd == nullptr)
		return -1;

	const char *pStart = pEnd;
	while(pStart > pDescription && pStart[-1] != ' ')
		pStart--;

	int Seconds = 0;
	int Part = 0;
	int Digits = 0;
	for(const char *pChar = pStart; pChar != pEnd; pChar++)
	{
		if(*pChar == ':')
		{
			if(Digits == 0)
				return -1;
			Seconds = Seconds * 60 + Part;
			Part = 0;
			Digits = 0;
		}
		else if(*pChar >= '0' && *pChar <= '9')
		{
			Part = Part * 10 + (*pChar - '0');
			Digits++;
		}
		else
		{
			return -1;
		}
		if(Digits > 2 || Seconds > 24 * 60 * 60)
			return -1;
	}
	return Digits == 0 ? -1 : Seconds * 60 + Part;
}

void CMapDetails::Reset()
{
	m_Stars = -1;
	m_MedianTimeSeconds = -1;
}

void CMapDetails::OnMapLoad()
{
	Reset();
}

void CMapDetails::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
		Reset();
}

void CMapDetails::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_MAPINFO)
		return;

	const CNetMsg_Sv_MapInfo *pMsg = static_cast<CNetMsg_Sv_MapInfo *>(pRawMsg);
	m_Stars = ParseStars(pMsg->m_pDescription);
	m_MedianTimeSeconds = ParseMedianTime(pMsg->m_pDescription);
}
