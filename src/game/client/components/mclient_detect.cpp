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

	const float MIN_EMOTE_GAP = 0.0f;
	const float MAX_EMOTE_GAP = 3.5f;
	const float EMOTE_GAP_GROWTH = 1.8f;
	const float EMOTE_BACKOFF_START = 0.6f;

	const float ECHO_TIMEOUT = 2.0f;
	const int MAX_ECHO_RETRIES = 4;

	const float ANNOUNCE_TIMEOUT = 10.0f;
	const float ANNOUNCE_COOLDOWN = 30.0f;
	const float ANNOUNCE_LISTEN = 8.0f;
	const float BEACON_ROUND = 10.0f;
	const float REPLY_DELAY = 0.3f;
	const float REPLY_JITTER = 1.5f;

	const float PET_COOLDOWN = 8.0f;
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

	m_ReplyPending = false;
	m_ReplyTime = 0.0f;

	m_PetOnSent = false;
	m_aPetSkinSent[0] = '\0';
	m_PetKnown = false;
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
		OnReset();
}

void CMClientDetect::ForgetLeftPeers()
{
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		const CGameClient::CClientData &Client = GameClient()->m_aClients[ClientId];
		if(!Client.m_Active)
			ClearPeer(ClientId);
		else if(m_aPeers[ClientId].m_aName[0] != '\0' && str_comp(Client.m_aRealName, m_aPeers[ClientId].m_aName) != 0)
			ClearPeer(ClientId);
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
	{
		Peer.m_Answering = false;
		Peer.m_Answered = false;
	}
}

bool CMClientDetect::AnswerOwed() const
{
	return std::any_of(std::begin(m_aPeers), std::end(m_aPeers), [](const CPeer &Peer) { return Peer.m_WantsAnswer; });
}

void CMClientDetect::MarkAnswering()
{
	for(CPeer &Peer : m_aPeers)
	{
		if(!Peer.m_WantsAnswer)
			continue;
		Peer.m_WantsAnswer = false;
		Peer.m_Answering = true;
	}
}

void CMClientDetect::OnBeaconSent()
{
	if(m_QueuedKind == KIND_PET)
	{
		m_PetOnSent = m_QueuedPetOn;
		str_copy(m_aPetSkinSent, m_aQueuedPetSkin);
		m_PetKnown = true;
		m_PetCooldown = LocalTime() + PET_COOLDOWN;
		return;
	}

	m_BeaconHeardUntil = LocalTime() + BEACON_ROUND;
	m_PetKnown = false;
	for(CPeer &Peer : m_aPeers)
	{
		Peer.m_Answering = false;
		if(!Peer.m_Detected)
			continue;
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}
}

void CMClientDetect::OnBeaconLost()
{
	for(CPeer &Peer : m_aPeers)
	{
		if(!Peer.m_Answering)
			continue;
		Peer.m_Answering = false;
		Peer.m_WantsAnswer = true;
	}
}

void CMClientDetect::Announce()
{
	if(!Enabled() || Client()->State() != IClient::STATE_ONLINE)
		return;
	if(m_AnnouncePending || LocalTime() < m_AnnounceCooldown)
		return;

	m_AnnouncePending = true;
	m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
}

void CMClientDetect::UpdateAnnounce()
{
	if(!m_AnnouncePending)
		return;

	if(LocalTime() > m_AnnounceDeadline)
	{
		m_AnnouncePending = false;
		return;
	}

	if(!CanEmote() || !ChannelFree())
		return;

	SendBeacon(KIND_ANNOUNCE);
	m_AnnouncePending = false;
	m_AnnounceCooldown = LocalTime() + ANNOUNCE_COOLDOWN;
	m_AnnounceListenTime = LocalTime() + ANNOUNCE_LISTEN;
}

bool CMClientDetect::Announcing() const
{
	return m_AnnouncePending || LocalTime() < m_AnnounceListenTime;
}

void CMClientDetect::UpdateReply()
{
	if(!m_ReplyPending)
		return;

	if(!g_Config.m_ClMClientMiniGames)
	{
		for(CPeer &Peer : m_aPeers)
			Peer.m_WantsAnswer = false;
		m_ReplyPending = false;
		return;
	}

	if(!AnswerOwed())
	{
		m_ReplyPending = false;
		return;
	}

	if(!CanEmote())
		return;

	if(LocalTime() < m_ReplyTime || !ChannelFree())
		return;

	SendBeacon(KIND_REPLY);
	MarkAnswering();
	m_ReplyPending = false;
}

void CMClientDetect::LocalPet(bool *pOn, char *pSkin, int SkinSize) const
{
	*pOn = g_Config.m_ClMClientPetTee != 0;
	str_copy(pSkin, g_Config.m_ClMClientForceSkin ? "maodie" : g_Config.m_ClMClientPetTeeSkin, SkinSize);
}

void CMClientDetect::UpdatePet()
{
	if(m_AnnouncePending || m_ReplyPending)
		return;

	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId != m_PetLocalId)
	{
		m_PetLocalId = LocalId;
		m_PetKnown = false;
	}

	if(NumDetected() == 0)
		return;
	if(!CanEmote() || !ChannelFree())
		return;
	if(LocalTime() < m_PetCooldown)
		return;

	bool On;
	char aSkin[MAX_SKIN_LENGTH];
	LocalPet(&On, aSkin, sizeof(aSkin));

	const bool Changed = On != m_PetOnSent || (On && str_comp(aSkin, m_aPetSkinSent) != 0);
	if(!Changed && (m_PetKnown || !On))
		return;

	SendPetBeacon(On, aSkin);
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

void CMClientDetect::AbortBeacon(bool Retry)
{
	m_vEmoteQueue.clear();
	m_ProtocolLeft = 0;
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;

	if(Retry && m_QueuedKind == KIND_ANNOUNCE)
	{
		m_AnnouncePending = true;
		m_AnnounceDeadline = LocalTime() + ANNOUNCE_TIMEOUT;
	}
	else if(m_QueuedKind == KIND_REPLY)
	{
		OnBeaconLost();
		if(Retry)
		{
			m_ReplyPending = true;
			m_ReplyTime = LocalTime() + REPLY_DELAY + Jitter(REPLY_JITTER);
		}
	}
	else if(m_QueuedKind == KIND_PET)
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
	AbortBeacon(true);
}

void CMClientDetect::FlushEmoteQueue()
{
	if(m_vEmoteQueue.empty())
		return;

	if(!CanEmote())
	{
		AbortBeacon(true);
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
			AbortBeacon(false);
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

	if(LocalTime() < m_BeaconHeardUntil)
	{
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}

	if(Kind != KIND_ANNOUNCE)
		return;

	if(!g_Config.m_ClMClientMiniGames)
		return;

	if(Peer.m_Answered || Peer.m_Answering)
		return;

	Peer.m_WantsAnswer = true;

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

	if(LocalTime() < m_BeaconHeardUntil)
	{
		Peer.m_WantsAnswer = false;
		Peer.m_Answered = true;
	}

	const int Head = Peer.m_aPayload[0] * EMOTE_RADIX + Peer.m_aPayload[1];
	Peer.m_PetOn = Head != PET_HEAD_NONE;
	Peer.m_aPetSkin[0] = '\0';

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

const char *CMClientDetect::PetSkin(int ClientId) const
{
	if(!Enabled() || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	const CPeer &Peer = m_aPeers[ClientId];
	if(!Peer.m_Detected || !Peer.m_PetOn)
		return nullptr;
	return Peer.m_aPetSkin[0] == '\0' ? "default" : Peer.m_aPetSkin;
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

void CMClientDetect::OnRender()
{
	if(!Enabled())
	{
		if(m_AnnouncePending || m_ReplyPending || !m_vEmoteQueue.empty() || NumDetected() > 0)
			OnReset();
		return;
	}

	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	ForgetLeftPeers();
	ForgetOnRename();
	UpdateAnnounce();
	UpdateReply();
	UpdatePet();
	FlushEmoteQueue();
}
