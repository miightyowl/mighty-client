/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "motd.h"

#include <base/color.h>
#include <base/log.h>
#include <base/log_color.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/components/important_alert.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <chrono>

namespace
{
	bool IsDigit(char c) { return c >= '0' && c <= '9'; }

	void StripTimeFractions(const char *pSrc, char *pDst, size_t DstSize)
	{
		size_t k = 0;
		for(size_t i = 0; pSrc[i] != '\0' && k + 1 < DstSize; i++)
		{
			const bool AfterTime = k >= 4 && IsDigit(pDst[k - 1]) && IsDigit(pDst[k - 2]) && pDst[k - 3] == ':' && IsDigit(pDst[k - 4]);
			if(pSrc[i] == '.' && AfterTime && IsDigit(pSrc[i + 1]))
			{
				while(IsDigit(pSrc[i + 1]))
					i++;
				continue;
			}
			pDst[k++] = pSrc[i];
		}
		pDst[k] = '\0';
	}

	int64_t ParseMedianTimeMillis(const char *pMotd)
	{
		const char *pLabel = str_find_nocase(pMotd, "Median Time:");
		if(pLabel == nullptr)
			return -1;
		const char *p = pLabel + str_length("Median Time:");
		while(*p == ' ')
			p++;

		int64_t Seconds = 0;
		int Part = 0;
		int Digits = 0;
		int Colons = 0;
		for(; IsDigit(*p) || *p == ':'; p++)
		{
			if(*p == ':')
			{
				if(Digits == 0 || ++Colons > 2)
					return -1;
				Seconds = Seconds * 60 + Part;
				Part = 0;
				Digits = 0;
			}
			else
			{
				Part = Part * 10 + (*p - '0');
				if(++Digits > 2 && Colons > 0)
					return -1;
			}
		}
		if(Digits == 0 || Colons == 0)
			return -1;
		Seconds = Seconds * 60 + Part;

		int Millis = 0;
		if(*p == '.' && IsDigit(p[1]))
		{
			p++;
			int Scale = 100;
			for(; IsDigit(*p); p++)
			{
				Millis += (*p - '0') * Scale;
				Scale /= 10;
			}
		}
		return Seconds * 1000 + Millis;
	}
}

CMotd::CMotd()
{
	m_aServerMotd[0] = '\0';
	m_aServerMotdNoMillis[0] = '\0';
	m_ServerMotdTime = 0;
	m_ServerMotdUpdateTime = 0;
}

void CMotd::Clear()
{
	m_ServerMotdTime = 0;
	m_TouchRect.reset();
	Graphics()->DeleteQuadContainer(m_RectQuadContainer);
	TextRender()->DeleteTextContainer(m_TextContainerIndex);
}

bool CMotd::IsActive() const
{
	return time() < m_ServerMotdTime;
}

void CMotd::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE || OldState == IClient::STATE_OFFLINE)
		Clear();
}

void CMotd::OnWindowResize()
{
	Graphics()->DeleteQuadContainer(m_RectQuadContainer);
	TextRender()->DeleteTextContainer(m_TextContainerIndex);
}

void CMotd::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!IsActive())
		return;

	if(GameClient()->m_ImportantAlert.IsActive())
	{
		Clear();
		return;
	}

	const int MaxLines = 24;
	const float FontSize = 32.0f; // also the size of the margin and rect rounding
	const float ScreenHeight = 40.0f * FontSize; // multiple of the font size to get perfect alignment
	const float ScreenWidth = ScreenHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(ScreenWidth, ScreenHeight);

	const float RectHeight = (MaxLines + 2) * FontSize;
	const float RectWidth = 630.0f + 2.0f * FontSize;
	const float RectX = ScreenWidth / 2.0f - RectWidth / 2.0f;
	const float RectY = 160.0f;
	m_TouchRect = CUIRect{RectX / ScreenWidth, RectY / ScreenHeight, RectWidth / ScreenWidth, RectHeight / ScreenHeight};

	if(m_RectQuadContainer == -1)
	{
		Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.5f);
		m_RectQuadContainer = Graphics()->CreateRectQuadContainer(RectX, RectY, RectWidth, RectHeight, FontSize, IGraphics::CORNER_ALL);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(m_RectQuadContainer != -1)
	{
		Graphics()->TextureClear();
		Graphics()->RenderQuadContainer(m_RectQuadContainer, -1);
	}

	if(m_ShownHideMillis != g_Config.m_ClMClientHideMillis)
	{
		TextRender()->DeleteTextContainer(m_TextContainerIndex);
		m_ShownHideMillis = g_Config.m_ClMClientHideMillis;
	}

	if(!m_TextContainerIndex.Valid())
	{
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(RectX + FontSize, RectY + FontSize));
		// TODO: Set TEXTFLAG_ELLIPSIS_AT_END when https://github.com/ddnet/ddnet/issues/11419 is fixed
		// Cursor.m_Flags |= TEXTFLAG_ELLIPSIS_AT_END;
		Cursor.m_FontSize = FontSize;
		Cursor.m_LineWidth = RectWidth - 2.0f * FontSize;
		Cursor.m_MaxLines = MaxLines;
		TextRender()->CreateTextContainer(m_TextContainerIndex, &Cursor, DisplayMotd());
	}

	if(m_TextContainerIndex.Valid())
		TextRender()->RenderTextContainer(m_TextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
}

void CMotd::OnMessage(int MsgType, void *pRawMsg)
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
		return;

	if(MsgType == NETMSGTYPE_SV_MOTD)
	{
		const CNetMsg_Sv_Motd *pMsg = static_cast<CNetMsg_Sv_Motd *>(pRawMsg);

		// copy it manually to process all \n
		const char *pMsgStr = pMsg->m_pMessage;
		const size_t MotdLen = str_length(pMsgStr) + 1;
		const char *pLast = m_aServerMotd; // for console printing
		const LOG_COLOR LogColor = color_cast<LOG_COLOR>(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClMessageHighlightColor)));
		for(size_t i = 0, k = 0; i < MotdLen && k < sizeof(m_aServerMotd); i++, k++)
		{
			// handle incoming "\\n"
			if(pMsgStr[i] == '\\' && pMsgStr[i + 1] == 'n')
			{
				m_aServerMotd[k] = '\n';
				i++; // skip the 'n'
			}
			else
			{
				m_aServerMotd[k] = pMsgStr[i];
			}

			// print the line to the console when receiving the newline character
			if(g_Config.m_ClPrintMotd && m_aServerMotd[k] == '\n')
			{
				m_aServerMotd[k] = '\0';
				log_info_color(LogColor, "motd", "%s", pLast);
				m_aServerMotd[k] = '\n';
				pLast = m_aServerMotd + k + 1;
			}
		}
		m_aServerMotd[sizeof(m_aServerMotd) - 1] = '\0';
		StripTimeFractions(m_aServerMotd, m_aServerMotdNoMillis, sizeof(m_aServerMotdNoMillis));
		m_MedianTimeMillis = ParseMedianTimeMillis(m_aServerMotd);
		if(g_Config.m_ClPrintMotd && *pLast != '\0')
		{
			log_info_color(LogColor, "motd", "%s", pLast);
		}

		if(!IsActive())
			m_ShownSince = time_get_nanoseconds();
		m_ServerMotdUpdateTime = time();
		if(m_aServerMotd[0] && g_Config.m_ClMotdTime)
			m_ServerMotdTime = m_ServerMotdUpdateTime + time_freq() * g_Config.m_ClMotdTime;
		else
			m_ServerMotdTime = 0;
		TextRender()->DeleteTextContainer(m_TextContainerIndex);
	}
}

bool CMotd::OnInput(const IInput::CEvent &Event)
{
	if(IsActive() && Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		Clear();
		return true;
	}
	return false;
}

bool CMotd::OnTouchState(std::vector<IInput::CTouchFingerState> &vTouchFingerStates)
{
	// Remove the finger that dismissed the MOTD so it does not activate other components until it is released.
	if(m_DismissTouchFinger.has_value())
	{
		const auto DismissFingerState = std::find_if(vTouchFingerStates.begin(), vTouchFingerStates.end(), [&](const IInput::CTouchFingerState &State) {
			return State.m_Finger == *m_DismissTouchFinger;
		});
		if(DismissFingerState == vTouchFingerStates.end())
			m_DismissTouchFinger.reset();
		else
			vTouchFingerStates.erase(DismissFingerState);
	}

	if(!IsActive())
		return false;
	if(GameClient()->m_Chat.IsActive() ||
		GameClient()->m_GameConsole.IsActive() ||
		GameClient()->m_Menus.IsActive() ||
		GameClient()->m_Emoticon.IsActive() ||
		GameClient()->m_Spectator.IsActive())
	{
		return false;
	}

	if(!m_TouchRect.has_value())
		return false;
	// Only fingers pressed down after the MOTD appeared and outside of its rect dismiss it.
	const auto DismissFinger = std::find_if(vTouchFingerStates.begin(), vTouchFingerStates.end(), [&](const IInput::CTouchFingerState &State) {
		return State.m_PressTime > m_ShownSince && !m_TouchRect->Inside(State.m_Position);
	});
	if(DismissFinger == vTouchFingerStates.end())
		return false;
	Clear();
	m_DismissTouchFinger = DismissFinger->m_Finger;
	vTouchFingerStates.erase(DismissFinger);
	return false;
}
