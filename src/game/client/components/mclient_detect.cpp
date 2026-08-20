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

	const float MIN_EMOTE_GAP = 0.4f;
	const float MAX_EMOTE_GAP = 3.5f;
	const float EMOTE_GAP_GROWTH = 1.8f;

	const float ECHO_TIMEOUT = 2.0f;
	const int MAX_ECHO_RETRIES = 4;

	const float ANNOUNCE_TIMEOUT = 10.0f;
	const float ANNOUNCE_COOLDOWN = 30.0f;
	const float ANNOUNCE_LISTEN = 8.0f;
	const float BEACON_ROUND = 10.0f;
	const float REPLY_DELAY = 0.3f;
	const float REPLY_JITTER = 1.5f;

	int BeaconCheck(int Kind)
	{
		return (BEACON_OP + BEACON_MAGIC + Kind) % EMOTE_RADIX;
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
	m_BeaconHeardUntil = LocalTime() + BEACON_ROUND;
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

	if(!CanEmote() || !m_vEmoteQueue.empty())
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

	if(LocalTime() < m_ReplyTime || !m_vEmoteQueue.empty())
		return;

	SendBeacon(KIND_REPLY);
	MarkAnswering();
	m_ReplyPending = false;
}

void CMClientDetect::SendBeacon(int Kind)
{
	m_vEmoteQueue.push_back(BEACON_OP);
	m_vEmoteQueue.push_back(BEACON_MAGIC);
	m_vEmoteQueue.push_back(Kind);
	m_vEmoteQueue.push_back(BeaconCheck(Kind));
	m_QueuedKind = Kind;
}

void CMClientDetect::AbortBeacon(bool Retry)
{
	m_vEmoteQueue.clear();
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
	m_QueuedKind = -1;
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
			AbortBeacon(false);
			m_ReplyPending = false;
			return;
		}

		m_EmoteGap = std::min(m_EmoteGap * EMOTE_GAP_GROWTH, MAX_EMOTE_GAP);
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
	if(m_vEmoteQueue.empty())
	{
		OnBeaconSent();
		m_QueuedKind = -1;
	}
	m_EmoteWaiting = false;
	m_EmoteTime = 0.0f;
	m_EmoteRetries = 0;
	return true;
}

bool CMClientDetect::Decode(int ClientId, int Emoticon)
{
	CPeer &Peer = m_aPeers[ClientId];

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
		Peer.m_Kind = Emoticon;
		Peer.m_Step = Emoticon >= 0 && Emoticon < NUM_KINDS ? 3 : 0;
		return true;
	case 3:
		if(Emoticon == BeaconCheck(Peer.m_Kind))
			OnBeacon(ClientId, Peer.m_Kind);
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
	FlushEmoteQueue();
}
