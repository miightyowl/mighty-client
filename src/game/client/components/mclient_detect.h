#ifndef GAME_CLIENT_COMPONENTS_MCLIENT_DETECT_H
#define GAME_CLIENT_COMPONENTS_MCLIENT_DETECT_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <vector>

class CMClientDetect : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;

	bool OnEmoticon(int ClientId, int Emoticon);

	void Announce();
	bool Announcing() const;

	bool Enabled() const;
	bool IsMClient(int ClientId) const;
	int NumDetected() const;

	bool EmoteInFlight() const { return m_EmoteWaiting; }

private:
	enum EKind
	{
		KIND_ANNOUNCE,
		KIND_REPLY,
		NUM_KINDS,
	};

	struct CPeer
	{
		bool m_Detected;
		char m_aName[MAX_NAME_LENGTH];
		int m_Step;
		int m_Kind;
	};

	CPeer m_aPeers[MAX_CLIENTS] = {};

	std::vector<int> m_vEmoteQueue;
	int m_QueuedKind = -1;
	bool m_EmoteWaiting = false;
	float m_EmoteTime = 0.0f;
	float m_NextEmoteTime = 0.0f;
	float m_EmoteGap = 0.4f;
	int m_EmoteRetries = 0;

	bool m_AnnouncePending = false;
	float m_AnnounceDeadline = 0.0f;
	float m_AnnounceCooldown = 0.0f;
	float m_AnnounceListenTime = 0.0f;

	bool m_ReplyPending = false;
	float m_ReplyTime = 0.0f;

	bool CanEmote() const;
	bool IsLocal(int ClientId) const;
	void ClearPeers();
	void ClearPeer(int ClientId);
	void ForgetLeftPeers();
	void UpdateAnnounce();
	void UpdateReply();
	void SendBeacon(int Kind);
	void AbortBeacon(bool Retry);
	void FlushEmoteQueue();
	bool HandleEcho(int Emoticon);
	bool Decode(int ClientId, int Emoticon);
	void OnBeacon(int ClientId, int Kind);
};

#endif
