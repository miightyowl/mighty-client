#include "mclient_detect.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cstdlib>

namespace
{
	const int EMOTE_RADIX = 12;
	const int BEACON_OP = 15;
	const int BEACON_MAGIC = 5;
	const unsigned COLOR_VALUE_MASK = 0x00ffffffu;
	const unsigned COLOR_BEACON_BODY = 0x4du;
	const unsigned COLOR_BEACON_FEET = 0x43u;
	const char *PET_WHISPER_PREFIX = "MCPET1 ";

	const float MIN_EMOTE_GAP = 0.0f;
	const float MAX_EMOTE_GAP = 3.5f;
	const float EMOTE_GAP_GROWTH = 1.8f;
	const float EMOTE_BACKOFF_START = 0.6f;
	const float EMOTE_GAP_DECAY = 0.5f;

	const float ECHO_TIMEOUT = 2.0f;
	const int MAX_ECHO_RETRIES = 4;

	const float ANNOUNCE_TIMEOUT = 10.0f;
	const float ANNOUNCE_COOLDOWN = 30.0f;
	const float ANNOUNCE_LISTEN = 12.0f;
	const float COLOR_BEACON_CLEAR_TIME = 1.0f;
	const float BEACON_ROUND = 10.0f;
	const float REPLY_DELAY = 0.3f;
	const float REPLY_JITTER = 1.5f;

	const float PET_COOLDOWN = 2.0f;
	const float PET_RETRY = 5.0f;
	const float FRAME_TIMEOUT = 15.0f;

	const char PET_ALPHABET[] = "abcdefghijklmnopqrstuvwxyz0123456789_-ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const int PET_NARROW_SIZE = 38;
	const int PET_WIDE_SIZE = sizeof(PET_ALPHABET) - 1;

	int FrameCheck(int Kind, const int *pPayload, int PayloadLen)
	{
		int Sum = BEACON_OP + BEACON_MAGIC + Kind;
		for(int i = 0; i < PayloadLen; i++)
			Sum += pPayload[i];
		return Sum % EMOTE_RADIX;
	}

	int AlphabetIndex(char Char)
	{
		for(int i = 0; i < PET_WIDE_SIZE; i++)
		{
			if(PET_ALPHABET[i] == Char)
				return i;
		}
		return -1;
	}

	float Jitter(float Amount)
	{
		return Amount * (float)(rand() % 1001) / 1000.0f;
	}

	char HexDigit(unsigned Value)
	{
		return Value < 10 ? '0' + Value : 'a' + Value - 10;
	}

	int HexValue(char Digit)
	{
		if(Digit >= '0' && Digit <= '9')
			return Digit - '0';
		if(Digit >= 'a' && Digit <= 'f')
			return Digit - 'a' + 10;
		if(Digit >= 'A' && Digit <= 'F')
			return Digit - 'A' + 10;
		return -1;
	}

	void HexEncode(const char *pText, char *pOutput, int OutputSize)
	{
		int Out = 0;
		for(int i = 0; pText[i] != '\0' && Out + 2 < OutputSize; i++)
		{
			const unsigned Char = static_cast<unsigned char>(pText[i]);
			pOutput[Out++] = HexDigit(Char >> 4);
			pOutput[Out++] = HexDigit(Char & 0xf);
		}
		pOutput[Out] = '\0';
	}

	bool HexDecode(const char *pText, char *pOutput, int OutputSize)
	{
		const int Length = str_length(pText);
		if(Length % 2 != 0 || Length / 2 >= OutputSize)
			return false;
		for(int i = 0; i < Length; i += 2)
		{
			const int High = HexValue(pText[i]);
			const int Low = HexValue(pText[i + 1]);
			if(High < 0 || Low < 0 || (High == 0 && Low == 0))
				return false;
			pOutput[i / 2] = static_cast<char>((High << 4) | Low);
		}
		pOutput[Length / 2] = '\0';
		return true;
	}
}

bool CMClientDetect::Enabled() const
{
	return g_Config.m_ClMClientUserDetection != 0;
}

bool CMClientDetect::IsMClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(IsLocal(ClientId))
		return true;
	return m_aPeers[ClientId].m_Detected;
}

int CMClientDetect::NumDetected() const
{
	int Num = 0;
	for(const CPeer &Peer : m_aPeers)
	{
		if(Peer.m_Detected)
			Num++;
	}
	return Num;
}

bool CMClientDetect::IsLocal(int ClientId) const
{
	return ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1];
}

bool CMClientDetect::CanEmote() const
{
	return Client()->State() == IClient::STATE_ONLINE && GameClient()->m_Snap.m_pLocalCharacter != nullptr;
}

bool CMClientDetect::ChannelFree() const
{
	// a mini game that wants to move must not have our frame in front of it
	return m_vEmoteQueue.empty() && !GameClient()->m_MiniGames.EmoteChannelBusy();
}

void CMClientDetect::ClearPeer(int ClientId)
{
	m_aPeers[ClientId] = CPeer{};
}

void CMClientDetect::ClearPeers()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		ClearPeer(ClientId);
}

void CMClientDetect::OnReset()
{
	ClearPeers();

	m_vEmoteQueue.clear();
	m_ProtocolLeft = 0;
	m_QueuedKind = -1;
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	// what the last server allowed says nothing about the next one
	m_NextEmoteTime = 0.0f;
	m_EmoteGap = MIN_EMOTE_GAP;
	m_EmoteRetries = 0;

	m_AnnouncePending = false;
	m_AnnounceDeadline = 0.0f;
	m_AnnounceCooldown = 0.0f;
	m_AnnounceListenTime = 0.0f;
	m_ColorBeaconClearPending = false;
	m_ColorBeaconClearTime = 0.0f;
	m_RefreshPending = false;
	m_RefreshColorDetected = false;
	m_RefreshLegacySent = false;
	m_RefreshFallbackTime = 0.0f;
	m_ColorBeaconActive = false;
	m_ColorBeaconObserved = false;
	m_ColorBeaconConn = IClient::CONN_MAIN;
	m_ColorBeaconSentTick = 0;
	m_ColorRestoreAttemptTick = 0;
	m_ColorBeaconSendTime = 0.0f;
	m_OriginalColorBody = 0;
	m_OriginalColorFeet = 0;
	m_MarkerColorBody = 0;
	m_MarkerColorFeet = 0;

	m_ReplyPending = false;
	m_ReplyWithEmotes = false;
	m_ReplyTime = 0.0f;

	m_PetOnSent = false;
	m_aPetSkinSent[0] = '\0';
	m_PetUseCustomColorSent = false;
	m_PetColorBodySent = 0;
	m_PetColorFeetSent = 0;
	m_PetLocalId = -1;
	m_PetCooldown = 0.0f;
	m_QueuedPetOn = false;
	m_aQueuedPetSkin[0] = '\0';

	m_aLocalName[0] = '\0';
	m_BeaconHeardUntil = 0.0f;
}

void CMClientDetect::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
	{
		OnReset();
		return;
	}

	if(OldState != IClient::STATE_ONLINE && Enabled())
		QueueColorAnnounce();
}

void CMClientDetect::ForgetLeftPeers()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
		CPeer &Peer = m_aPeers[ClientId];
		if(!Client.m_Active)
			ClearPeer(ClientId);
		else if(Peer.m_aName[0] != '\0' && str_comp(Client.m_aRealName, Peer.m_aName) != 0)
			str_copy(Peer.m_aName, Client.m_aRealName);
	}
}

void CMClientDetect::ForgetOnRename()
{
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const char *pName = LocalId < 0 ? "" : GameClient()->m_aClients[LocalId].m_aRealName;
	if(str_comp(pName, m_aLocalName) == 0)
		return;

	str_copy(m_aLocalName, pName);
	m_BeaconHeardUntil = 0.0f;
	for(CPeer &Peer : m_aPeers)
		Peer.m_Answered = false;
}

bool CMClientDetect::AnswerOwed() const
{
	return std::any_of(std::begin(m_aPeers), std::end(m_aPeers), [](const CPeer &Peer) { return Peer.m_WantsAnswer; });
}

void CMClientDetect::OnBeaconSent()
{
	if(m_QueuedKind != KIND_PET)
	{
		m_BeaconHeardUntil = LocalTime() + BEACON_ROUND;
		for(CPeer &Peer : m_aPeers)
		{
			if(!Peer.m_Detected)
				continue;
			Peer.m_WantsAnswer = false;
			Peer.m_Answered = true;
		}
		return;
	}

	m_PetOnSent = m_QueuedPetOn;
	str_copy(m_aPetSkinSent, m_aQueuedPetSkin);
	MarkPetTold();
	m_PetCooldown = LocalTime() + PET_COOLDOWN;
}

void CMClientDetect::Announce()
{
	if(!Enabled() || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(m_AnnouncePending || m_ColorBeaconClearPending || LocalTime() < m_AnnounceCooldown)
		return;

	QueueColorAnnounce();
}

void CMClientDetect::QueueColorAnnounce()
{
	m_AnnouncePending = false;
	m_ColorBeaconClearPending = true;
	m_ColorBeaconClearTime = 0.0f;
	m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
}

void CMClientDetect::Refresh()
{
	if(!Enabled() || Client()->State() != IClient::STATE_ONLINE)
		return;

	m_RefreshPending = true;
	m_RefreshColorDetected = false;
	m_RefreshLegacySent = false;
	if(Client()->IsSixup())
	{
		m_ColorBeaconClearPending = false;
		m_AnnouncePending = true;
		m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
		m_RefreshFallbackTime = LocalTime();
		return;
	}

	QueueColorAnnounce();
	m_RefreshFallbackTime = m_AnnounceDeadline;
}

void CMClientDetect::UpdateAnnounce()
{
	if(m_ColorBeaconClearPending)
	{
		if(LocalTime() > m_AnnounceDeadline || Client()->IsSixup())
		{
			m_ColorBeaconClearPending = false;
			m_ColorBeaconClearTime = 0.0f;
			return;
		}
		if(m_ColorBeaconActive)
			return;

		const int Conn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
		if(GameClient()->m_aLocalIds[Conn] < 0)
			return;
		if(m_ColorBeaconClearTime == 0.0f)
		{
			m_ColorBeaconClearTime = LocalTime() + COLOR_BEACON_CLEAR_TIME;
			return;
		}
		if(LocalTime() < m_ColorBeaconClearTime)
			return;

		m_ColorBeaconClearPending = false;
		m_ColorBeaconClearTime = 0.0f;
		m_AnnouncePending = true;
		m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
		if(m_RefreshPending)
			m_RefreshFallbackTime = m_AnnounceDeadline;
	}

	if(!m_AnnouncePending)
		return;

	if(LocalTime() > m_AnnounceDeadline)
	{
		m_AnnouncePending = false;
		return;
	}

	if(Client()->IsSixup() || m_ColorBeaconActive)
		return;
	const int Conn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	if(GameClient()->m_aLocalIds[Conn] < 0 || GameClient()->m_aNextChangeInfo[Conn] > Client()->GameTick(Conn))
		return;

	SendColorBeacon();
	m_AnnouncePending = false;
	m_AnnounceCooldown = LocalTime() + ANNOUNCE_COOLDOWN;
	m_AnnounceListenTime = LocalTime() + ANNOUNCE_LISTEN;
	if(m_RefreshPending)
		m_RefreshFallbackTime = m_AnnounceListenTime;
}

bool CMClientDetect::Refreshing() const
{
	return m_RefreshPending;
}

void CMClientDetect::UpdateRefresh()
{
	if(!m_RefreshPending)
		return;

	if(m_RefreshColorDetected)
	{
		m_RefreshPending = false;
		m_RefreshLegacySent = false;
		return;
	}
	if(m_RefreshLegacySent)
	{
		if(LocalTime() >= m_RefreshFallbackTime)
		{
			m_RefreshPending = false;
			m_RefreshLegacySent = false;
		}
		return;
	}

	if(LocalTime() < m_RefreshFallbackTime || m_ColorBeaconActive)
		return;

	m_AnnouncePending = false;
	if(!CanEmote() || !ChannelFree())
		return;

	SendBeacon(KIND_ANNOUNCE);
	m_RefreshLegacySent = true;
	m_RefreshFallbackTime = LocalTime() + ANNOUNCE_LISTEN;
	m_AnnounceCooldown = LocalTime() + ANNOUNCE_COOLDOWN;
	m_AnnounceListenTime = LocalTime() + ANNOUNCE_LISTEN;
}

void CMClientDetect::UpdateReply()
{
	if(!m_ReplyPending)
		return;

	if(!AnswerOwed())
	{
		m_ReplyPending = false;
		m_ReplyWithEmotes = false;
		return;
	}

	if(LocalTime() < m_ReplyTime || m_ColorBeaconActive)
		return;
	if(m_ReplyWithEmotes)
	{
		if(!CanEmote() || !ChannelFree())
			return;
		SendBeacon(KIND_REPLY);
		m_ReplyPending = false;
		m_ReplyWithEmotes = false;
		return;
	}

	m_AnnouncePending = true;
	m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
	m_ReplyPending = false;
}

void CMClientDetect::DetectColorBeacons()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active || IsLocal(ClientId))
			continue;

		CPeer &Peer = m_aPeers[ClientId];
		const unsigned ColorBody = static_cast<unsigned>(Client.m_ColorBody);
		const unsigned ColorFeet = static_cast<unsigned>(Client.m_ColorFeet);
		const bool MarkerVisible = (ColorBody >> 24) == COLOR_BEACON_BODY && (ColorFeet >> 24) == COLOR_BEACON_FEET;
		const bool NewColorBeacon = MarkerVisible && (!Peer.m_ColorBeaconVisible || LocalTime() >= Peer.m_LastColorBeaconReplyTime + BEACON_ROUND);
		Peer.m_ColorBeaconVisible = MarkerVisible;
		if(NewColorBeacon)
			Peer.m_LastColorBeaconReplyTime = LocalTime();
		if(!MarkerVisible)
			continue;

		Peer.m_Detected = true;
		str_copy(Peer.m_aName, Client.m_aRealName);
		if(m_RefreshPending)
			m_RefreshColorDetected = true;
		if(!NewColorBeacon || LocalTime() < m_AnnounceListenTime)
			continue;

		Peer.m_WantsAnswer = true;
		if(!m_ReplyPending)
		{
			m_ReplyPending = true;
			m_ReplyWithEmotes = false;
			m_ReplyTime = LocalTime() + REPLY_DELAY + Jitter(REPLY_JITTER);
		}
	}
}

void CMClientDetect::SendInfoColors(int Conn, int ColorBody, int ColorFeet) const
{
	const bool Dummy = Conn == IClient::CONN_DUMMY;
	CNetMsg_Cl_ChangeInfo Msg;
	Msg.m_pName = Dummy ? Client()->DummyName() : Client()->PlayerName();
	Msg.m_pClan = Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;
	Msg.m_Country = Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	Msg.m_pSkin = Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	Msg.m_UseCustomColor = Dummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor;
	Msg.m_ColorBody = ColorBody;
	Msg.m_ColorFeet = ColorFeet;
	CMsgPacker Packer(&Msg);
	Msg.Pack(&Packer);
	Client()->SendMsg(Conn, &Packer, MSGFLAG_VITAL);
}

void CMClientDetect::SendColorBeacon()
{
	m_ColorBeaconConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const bool Dummy = m_ColorBeaconConn == IClient::CONN_DUMMY;
	m_OriginalColorBody = Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody;
	m_OriginalColorFeet = Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet;
	m_MarkerColorBody = static_cast<int>((static_cast<unsigned>(m_OriginalColorBody) & COLOR_VALUE_MASK) | (COLOR_BEACON_BODY << 24));
	m_MarkerColorFeet = static_cast<int>((static_cast<unsigned>(m_OriginalColorFeet) & COLOR_VALUE_MASK) | (COLOR_BEACON_FEET << 24));
	SendInfoColors(m_ColorBeaconConn, m_MarkerColorBody, m_MarkerColorFeet);
	m_ColorBeaconSentTick = Client()->GameTick(m_ColorBeaconConn);
	m_ColorRestoreAttemptTick = 0;
	m_ColorBeaconSendTime = LocalTime();
	m_ColorBeaconActive = true;
	m_ColorBeaconObserved = false;
	for(CPeer &Peer : m_aPeers)
	{
		if(!Peer.m_WantsAnswer)
			continue;
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}
}

void CMClientDetect::RestoreColorBeacon()
{
	if(!m_ColorBeaconActive)
		return;

	const int LocalId = GameClient()->m_aLocalIds[m_ColorBeaconConn];
	if(LocalId < 0)
	{
		m_ColorBeaconActive = false;
		return;
	}

	const CGameClient::CClientData &LocalClient = GameClient()->m_aClients[LocalId];
	const bool MarkerVisible = LocalClient.m_ColorBody == m_MarkerColorBody && LocalClient.m_ColorFeet == m_MarkerColorFeet;
	if(!m_ColorBeaconObserved)
	{
		if(MarkerVisible)
			m_ColorBeaconObserved = true;
		else if(LocalTime() >= m_ColorBeaconSendTime + ECHO_TIMEOUT)
		{
			m_ColorBeaconActive = false;
			if(Enabled() && LocalTime() < m_AnnounceDeadline)
				m_AnnouncePending = true;
		}
		return;
	}

	if(!MarkerVisible)
	{
		m_ColorBeaconActive = false;
		return;
	}

	const int CurrentTick = Client()->GameTick(m_ColorBeaconConn);
	const int RestoreTick = std::max({m_ColorBeaconSentTick + Client()->GameTickSpeed(), m_ColorRestoreAttemptTick + Client()->GameTickSpeed(), GameClient()->m_aNextChangeInfo[m_ColorBeaconConn]});
	if(CurrentTick < RestoreTick)
		return;

	SendInfoColors(m_ColorBeaconConn, m_OriginalColorBody, m_OriginalColorFeet);
	m_ColorRestoreAttemptTick = CurrentTick;
}

void CMClientDetect::LocalPet(bool *pOn, char *pSkin, int SkinSize, bool *pUseCustomColor, int *pColorBody, int *pColorFeet) const
{
	const bool Dummy = g_Config.m_ClDummy != 0;
	*pOn = Dummy ? g_Config.m_ClMClientDummyPetTee != 0 : g_Config.m_ClMClientPetTee != 0;
	const char *pConfiguredSkin = Dummy ? g_Config.m_ClMClientDummyPetTeeSkin : g_Config.m_ClMClientPetTeeSkin;
	str_copy(pSkin, g_Config.m_ClMClientForceSkin ? "maodie" : pConfiguredSkin, SkinSize);
	*pUseCustomColor = Dummy ? g_Config.m_ClMClientDummyPetTeeUseCustomColor != 0 : g_Config.m_ClMClientPetTeeUseCustomColor != 0;
	*pColorBody = Dummy ? g_Config.m_ClMClientDummyPetTeeColorBody : g_Config.m_ClMClientPetTeeColorBody;
	*pColorFeet = Dummy ? g_Config.m_ClMClientDummyPetTeeColorFeet : g_Config.m_ClMClientPetTeeColorFeet;
}

bool CMClientDetect::PetAudience() const
{
	return std::any_of(std::begin(m_aPeers), std::end(m_aPeers), [](const CPeer &Peer) { return Peer.m_Detected && !Peer.m_PetTold; });
}

void CMClientDetect::MarkPetTold()
{
	for(CPeer &Peer : m_aPeers)
		Peer.m_PetTold = Peer.m_Detected;
}

void CMClientDetect::ForgetPetTold()
{
	for(CPeer &Peer : m_aPeers)
		Peer.m_PetTold = false;
}

void CMClientDetect::UpdatePet()
{
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId != m_PetLocalId)
	{
		m_PetLocalId = LocalId;
		ForgetPetTold();
	}

	bool On;
	char aSkin[MAX_SKIN_LENGTH];
	bool UseCustomColor;
	int ColorBody, ColorFeet;
	LocalPet(&On, aSkin, sizeof(aSkin), &UseCustomColor, &ColorBody, &ColorFeet);

	if(On != m_PetOnSent || str_comp(aSkin, m_aPetSkinSent) != 0 ||
		UseCustomColor != m_PetUseCustomColorSent || ColorBody != m_PetColorBodySent || ColorFeet != m_PetColorFeetSent)
	{
		ForgetPetTold();
		m_PetOnSent = On;
		str_copy(m_aPetSkinSent, aSkin);
		m_PetUseCustomColorSent = UseCustomColor;
		m_PetColorBodySent = ColorBody;
		m_PetColorFeetSent = ColorFeet;
	}

	if(!PetAudience())
		return;
	if(!GameClient()->m_Chat.ServerHasCommand("w"))
		return;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		CPeer &Peer = m_aPeers[ClientId];
		if(!Peer.m_Detected || Peer.m_PetTold)
			continue;
		SendPetWhisper(ClientId, On, aSkin, UseCustomColor, ColorBody, ColorFeet);
		Peer.m_PetTold = true;
	}
}

void CMClientDetect::SendPetWhisper(int ClientId, bool On, const char *pSkin, bool UseCustomColor, int ColorBody, int ColorFeet)
{
	char aEncodedSkin[2 * MAX_SKIN_LENGTH];
	if(!On)
		str_copy(aEncodedSkin, "!");
	else
		HexEncode(pSkin, aEncodedSkin, sizeof(aEncodedSkin));
	if(On && aEncodedSkin[0] == '\0')
		str_copy(aEncodedSkin, "-");
	char aMessage[192];
	if(!On)
		str_format(aMessage, sizeof(aMessage), "%s%s", PET_WHISPER_PREFIX, aEncodedSkin);
	else if(UseCustomColor)
		str_format(aMessage, sizeof(aMessage), "%s%s 1 %08x %08x", PET_WHISPER_PREFIX, aEncodedSkin, (unsigned)ColorBody, (unsigned)ColorFeet);
	else
		str_format(aMessage, sizeof(aMessage), "%s%s 0", PET_WHISPER_PREFIX, aEncodedSkin);
	GameClient()->m_MiniGames.QueueWhisper(ClientId, aMessage);
}

void CMClientDetect::SendPetBeacon(bool On, const char *pSkin)
{
	int aIndices[MAX_PET_NAME];
	int NameLen = 0;
	int Head = PET_HEAD_NONE;
	bool Wide = false;
	if(On)
	{
		Head = PET_HEAD_UNKNOWN;
		NameLen = str_length(pSkin);
		if(NameLen > 0 && NameLen <= MAX_PET_NAME)
		{
			int i = 0;
			for(; i < NameLen; i++)
			{
				aIndices[i] = AlphabetIndex(pSkin[i]);
				if(aIndices[i] < 0)
					break;
				if(aIndices[i] >= PET_NARROW_SIZE)
					Wide = true;
			}
			if(i == NameLen)
				Head = Wide ? PET_HEAD_WIDE + NameLen : NameLen;
		}
	}

	int aPayload[MAX_PAYLOAD];
	int PayloadLen = 0;
	aPayload[PayloadLen++] = Head / EMOTE_RADIX;
	aPayload[PayloadLen++] = Head % EMOTE_RADIX;
	if(Head != PET_HEAD_NONE && Head != PET_HEAD_UNKNOWN)
	{
		const int Base = Wide ? PET_WIDE_SIZE : PET_NARROW_SIZE;
		const int Digits = Wide ? 4 : 3;
		for(int i = 0; i < NameLen; i += 2)
		{
			int Value = aIndices[i] * Base + (i + 1 < NameLen ? aIndices[i + 1] : 0);
			for(int d = Digits - 1; d >= 0; d--)
			{
				aPayload[PayloadLen + d] = Value % EMOTE_RADIX;
				Value /= EMOTE_RADIX;
			}
			PayloadLen += Digits;
		}
	}

	m_vEmoteQueue.push_back(BEACON_OP);
	m_vEmoteQueue.push_back(BEACON_MAGIC);
	m_vEmoteQueue.push_back(KIND_PET);
	for(int i = 0; i < PayloadLen; i++)
		m_vEmoteQueue.push_back(aPayload[i]);
	m_vEmoteQueue.push_back(FrameCheck(KIND_PET, aPayload, PayloadLen));

	m_ProtocolLeft = (int)m_vEmoteQueue.size();
	m_QueuedKind = KIND_PET;
	m_QueuedPetOn = On;
	str_copy(m_aQueuedPetSkin, pSkin);
}

void CMClientDetect::SendBeacon(int Kind)
{
	m_vEmoteQueue.push_back(BEACON_OP);
	m_vEmoteQueue.push_back(BEACON_MAGIC);
	m_vEmoteQueue.push_back(Kind);
	m_vEmoteQueue.push_back(FrameCheck(Kind, nullptr, 0));
	m_ProtocolLeft = (int)m_vEmoteQueue.size();
	m_QueuedKind = Kind;
}

void CMClientDetect::AbortBeacon()
{
	m_vEmoteQueue.clear();
	m_ProtocolLeft = 0;
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;

	if(m_QueuedKind == KIND_PET)
	{
		m_PetCooldown = LocalTime() + PET_RETRY;
	}
	m_QueuedKind = -1;
}

bool CMClientDetect::QueueManualEmote(int Emoticon)
{
	if(m_vEmoteQueue.empty())
		return false;
	if(Emoticon < 0 || Emoticon >= NUM_EMOTICONS)
		return false;

	m_vEmoteQueue.push_back(Emoticon);
	return true;
}

void CMClientDetect::YieldEmoteChannel()
{
	if(m_vEmoteQueue.empty())
		return;
	AbortBeacon();
}

void CMClientDetect::FlushEmoteQueue()
{
	if(m_vEmoteQueue.empty())
		return;

	if(!CanEmote())
	{
		AbortBeacon();
		return;
	}

	if(!m_EmoteWaiting && GameClient()->m_MiniGames.EmoteChannelBusy())
		return;

	if(m_EmoteWaiting && LocalTime() >= m_EmoteTime + ECHO_TIMEOUT)
	{
		m_EmoteRetries++;
		if(m_EmoteRetries > MAX_ECHO_RETRIES)
		{
			const int Kind = m_QueuedKind;
			AbortBeacon();
			// a peer that asked for an answer while this frame was running still deserves one
			if(Kind != KIND_PET)
				m_ReplyPending = false;
			return;
		}

		m_EmoteGap = std::min(std::max(m_EmoteGap * EMOTE_GAP_GROWTH, EMOTE_BACKOFF_START), MAX_EMOTE_GAP);
		m_EmoteWaiting = false;
		m_NextEmoteTime = LocalTime() + m_EmoteGap;
	}

	if(m_EmoteWaiting || LocalTime() < m_NextEmoteTime)
		return;

	CNetMsg_Cl_Emoticon Msg;
	Msg.m_Emoticon = m_vEmoteQueue.front();
	Client()->SendPackMsgActive(&Msg, MSGFLAG_VITAL);
	m_EmoteWaiting = true;
	m_EmoteTime = LocalTime();
	m_NextEmoteTime = LocalTime() + m_EmoteGap;
}

bool CMClientDetect::HandleEcho(int Emoticon)
{
	if(!m_EmoteWaiting || m_vEmoteQueue.empty() || m_vEmoteQueue.front() != Emoticon)
		return false;

	m_vEmoteQueue.erase(m_vEmoteQueue.begin());
	if(m_ProtocolLeft > 0)
	{
		m_ProtocolLeft--;
		if(m_ProtocolLeft == 0)
		{
			OnBeaconSent();
			m_QueuedKind = -1;
		}
	}
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;
	m_EmoteGap = std::max(m_EmoteGap * EMOTE_GAP_DECAY, MIN_EMOTE_GAP);
	m_NextEmoteTime = std::min(m_NextEmoteTime, LocalTime() + m_EmoteGap);
	return true;
}

bool CMClientDetect::Decode(int ClientId, int Emoticon)
{
	CPeer &Peer = m_aPeers[ClientId];

	if(Peer.m_Step != 0 && LocalTime() > Peer.m_SymbolTime + FRAME_TIMEOUT)
		Peer.m_Step = 0;
	Peer.m_SymbolTime = LocalTime();

	if(Emoticon == BEACON_OP)
	{
		Peer.m_Step = 1;
		return false;
	}

	switch(Peer.m_Step)
	{
	case 1:
		if(Emoticon != BEACON_MAGIC)
		{
			Peer.m_Step = 0;
			return false;
		}
		Peer.m_Step = 2;
		if(g_Config.m_ClMClientHideProtocolEmotes)
			GameClient()->m_aClients[ClientId].m_EmoticonStartTick = -1;
		return true;
	case 2:
		if(Emoticon < 0 || Emoticon >= NUM_KINDS)
		{
			Peer.m_Step = 0;
			return true;
		}
		Peer.m_Kind = Emoticon;
		Peer.m_PayloadLen = 0;
		Peer.m_PayloadNeed = Emoticon == KIND_PET ? 2 : 0;
		Peer.m_Step = Peer.m_PayloadNeed > 0 ? 3 : 4;
		return true;
	case 3:
		if(Peer.m_PayloadLen >= MAX_PAYLOAD)
		{
			Peer.m_Step = 0;
			return true;
		}
		Peer.m_aPayload[Peer.m_PayloadLen++] = Emoticon;
		if(Peer.m_Kind == KIND_PET && Peer.m_PayloadLen == 2)
		{
			const int Head = Peer.m_aPayload[0] * EMOTE_RADIX + Peer.m_aPayload[1];
			if(Head >= 1 && Head <= MAX_PET_NAME)
				Peer.m_PayloadNeed = 2 + 3 * ((Head + 1) / 2);
			else if(Head > PET_HEAD_WIDE && Head <= PET_HEAD_WIDE + MAX_PET_NAME)
				Peer.m_PayloadNeed = 2 + 4 * ((Head - PET_HEAD_WIDE + 1) / 2);
			else if(Head != PET_HEAD_NONE && Head != PET_HEAD_UNKNOWN)
			{
				Peer.m_Step = 0;
				return true;
			}
		}
		if(Peer.m_PayloadLen >= Peer.m_PayloadNeed)
			Peer.m_Step = 4;
		return true;
	case 4:
		if(Emoticon == FrameCheck(Peer.m_Kind, Peer.m_aPayload, Peer.m_PayloadLen))
		{
			if(Peer.m_Kind == KIND_PET)
				OnPetBeacon(ClientId);
			else
				OnBeacon(ClientId, Peer.m_Kind);
		}
		Peer.m_Step = 0;
		return true;
	default:
		Peer.m_Step = 0;
		return false;
	}
}

void CMClientDetect::OnBeacon(int ClientId, int Kind)
{
	CPeer &Peer = m_aPeers[ClientId];
	Peer.m_Detected = true;
	str_copy(Peer.m_aName, GameClient()->m_aClients[ClientId].m_aRealName);
	m_RefreshPending = false;
	m_RefreshLegacySent = false;

	if(LocalTime() < m_BeaconHeardUntil)
	{
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}

	if(Kind != KIND_ANNOUNCE)
		return;

	if(!g_Config.m_ClMClientMiniGames)
		return;

	Peer.m_WantsAnswer = true;
	m_ReplyWithEmotes = true;

	if(m_ReplyPending)
		return;

	m_ReplyPending = true;
	m_ReplyTime = LocalTime() + REPLY_DELAY + Jitter(REPLY_JITTER);
}

void CMClientDetect::OnPetBeacon(int ClientId)
{
	CPeer &Peer = m_aPeers[ClientId];
	Peer.m_Detected = true;
	str_copy(Peer.m_aName, GameClient()->m_aClients[ClientId].m_aRealName);
	m_RefreshPending = false;
	m_RefreshLegacySent = false;

	if(LocalTime() < m_BeaconHeardUntil)
	{
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}

	const int Head = Peer.m_aPayload[0] * EMOTE_RADIX + Peer.m_aPayload[1];
	Peer.m_PetOn = Head != PET_HEAD_NONE;
	Peer.m_aPetSkin[0] = '\0';
	Peer.m_PetUseCustomColor = false;
	Peer.m_PetColorBody = 0;
	Peer.m_PetColorFeet = 0;

	const bool Wide = Head > PET_HEAD_WIDE;
	const int NameLen = Wide ? Head - PET_HEAD_WIDE : Head;
	if(NameLen < 1 || NameLen > MAX_PET_NAME)
		return;

	const int Base = Wide ? PET_WIDE_SIZE : PET_NARROW_SIZE;
	const int Digits = Wide ? 4 : 3;
	char aSkin[MAX_SKIN_LENGTH] = "";
	int Pos = 2;
	for(int i = 0; i < NameLen; i += 2)
	{
		int Value = 0;
		for(int d = 0; d < Digits; d++)
			Value = Value * EMOTE_RADIX + Peer.m_aPayload[Pos + d];
		Pos += Digits;
		if(Value >= Base * Base)
			return;
		aSkin[i] = PET_ALPHABET[Value / Base];
		if(i + 1 < NameLen)
			aSkin[i + 1] = PET_ALPHABET[Value % Base];
	}
	str_copy(Peer.m_aPetSkin, aSkin);
}

CMClientDetect::SPetAppearance CMClientDetect::PetAppearance(int ClientId) const
{
	SPetAppearance Appearance;
	if(!Enabled() || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return Appearance;

	const CPeer &Peer = m_aPeers[ClientId];
	if(!Peer.m_Detected || !Peer.m_PetOn)
		return Appearance;
	Appearance.m_pSkin = Peer.m_aPetSkin[0] == '\0' ? "default" : Peer.m_aPetSkin;
	Appearance.m_UseCustomColor = Peer.m_PetUseCustomColor;
	Appearance.m_ColorBody = Peer.m_PetColorBody;
	Appearance.m_ColorFeet = Peer.m_PetColorFeet;
	return Appearance;
}

bool CMClientDetect::OnWhisper(int ClientId, int Team, const char *pMessage)
{
	const char *pArgs = str_startswith(pMessage, PET_WHISPER_PREFIX);
	if(!pArgs)
		return false;
	if(Team == TEAM_WHISPER_SEND)
		return true;
	if(Team != TEAM_WHISPER_RECV || ClientId < 0 || ClientId >= MAX_CLIENTS || !Enabled())
		return true;

	CPeer &Peer = m_aPeers[ClientId];

	char aEncodedSkin[2 * MAX_SKIN_LENGTH] = "";
	int UseCustomColor = 0;
	char aColorBody[16] = "";
	char aColorFeet[16] = "";
	const int Tokens = sscanf(pArgs, "%46s %d %15s %15s", aEncodedSkin, &UseCustomColor, aColorBody, aColorFeet);
	if(Tokens < 1)
		return true;

	char aSkin[MAX_SKIN_LENGTH];
	const bool On = str_comp(aEncodedSkin, "!") != 0;
	if(!On || str_comp(aEncodedSkin, "-") == 0)
		aSkin[0] = '\0';
	else if(!HexDecode(aEncodedSkin, aSkin, sizeof(aSkin)))
		return true;

	Peer.m_Detected = true;
	str_copy(Peer.m_aName, GameClient()->m_aClients[ClientId].m_aRealName);
	m_RefreshPending = false;
	m_RefreshLegacySent = false;

	Peer.m_PetOn = On;
	str_copy(Peer.m_aPetSkin, aSkin);
	Peer.m_PetUseCustomColor = false;
	Peer.m_PetColorBody = 0;
	Peer.m_PetColorFeet = 0;
	if(On && Tokens >= 4 && UseCustomColor)
	{
		unsigned ColorBody = 0, ColorFeet = 0;
		if(sscanf(aColorBody, "%x", &ColorBody) == 1 && sscanf(aColorFeet, "%x", &ColorFeet) == 1)
		{
			Peer.m_PetUseCustomColor = true;
			Peer.m_PetColorBody = (int)ColorBody;
			Peer.m_PetColorFeet = (int)ColorFeet;
		}
	}
	return true;
}

bool CMClientDetect::OnEmoticon(int ClientId, int Emoticon)
{
	if(!Enabled() || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(Emoticon < 0 || Emoticon >= NUM_EMOTICONS)
		return false;

	if(IsLocal(ClientId))
		return HandleEcho(Emoticon);
	return Decode(ClientId, Emoticon);
}

void CMClientDetect::OnUpdate()
{
	if(!Enabled())
	{
		if(m_ColorBeaconActive && Client()->State() == IClient::STATE_ONLINE)
		{
			RestoreColorBeacon();
			if(m_ColorBeaconActive)
				return;
		}
		if(m_AnnouncePending || m_ColorBeaconClearPending || m_RefreshPending || m_ReplyPending || !m_vEmoteQueue.empty() || NumDetected() > 0)
			OnReset();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	ForgetLeftPeers();
	ForgetOnRename();
	DetectColorBeacons();
	UpdateAnnounce();
	UpdateRefresh();
	UpdateReply();
	RestoreColorBeacon();
	UpdatePet();
	FlushEmoteQueue();
}
