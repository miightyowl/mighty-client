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

	// skin of the companion tee that player broadcast, nullptr when they have none
	const char *PetSkin(int ClientId) const;

	bool EmoteInFlight() const { return m_EmoteWaiting; }
	bool EmoteChannelBusy() const { return !m_vEmoteQueue.empty(); }
	// drop the frame we are sending so a mini game can have the emoticon channel
	void YieldEmoteChannel();

private:
	enum EKind
	{
		KIND_ANNOUNCE,
		KIND_REPLY,
		KIND_PET,
		NUM_KINDS,
	};

	enum
	{
		MAX_PET_NAME = MAX_SKIN_LENGTH - 1,
		// the head of a pet frame holds the length of the skin name, or one of these
		PET_HEAD_NONE = 0,
		PET_HEAD_UNKNOWN = MAX_PET_NAME + 1,
		PET_HEAD_WIDE = 32,
		// two head digits, then three digits per pair of characters
		MAX_PAYLOAD = 2 + 4 * ((MAX_PET_NAME + 1) / 2),
	};

	struct CPeer
	{
		bool m_Detected;
		char m_aName[MAX_NAME_LENGTH];
		int m_Step;
		int m_Kind;
		int m_aPayload[MAX_PAYLOAD];
		int m_PayloadLen;
		int m_PayloadNeed;
		float m_SymbolTime;
		bool m_WantsAnswer;
		bool m_Answering;
		bool m_Answered;
		bool m_PetOn;
		char m_aPetSkin[MAX_SKIN_LENGTH];
	};

	CPeer m_aPeers[MAX_CLIENTS] = {};

	std::vector<int> m_vEmoteQueue;
	int m_QueuedKind = -1;
	bool m_EmoteWaiting = false;
	float m_EmoteTime = 0.0f;
	float m_NextEmoteTime = 0.0f;
	float m_EmoteGap = 0.0f;
	int m_EmoteRetries = 0;

	bool m_AnnouncePending = false;
	float m_AnnounceDeadline = 0.0f;
	float m_AnnounceCooldown = 0.0f;
	float m_AnnounceListenTime = 0.0f;

	bool m_ReplyPending = false;
	float m_ReplyTime = 0.0f;

	// the pet state the other M-Client players around us know about
	bool m_PetOnSent = false;
	char m_aPetSkinSent[MAX_SKIN_LENGTH] = "";
	bool m_PetKnown = false;
	int m_PetLocalId = -1;
	float m_PetCooldown = 0.0f;
	bool m_QueuedPetOn = false;
	char m_aQueuedPetSkin[MAX_SKIN_LENGTH] = "";

	char m_aLocalName[MAX_NAME_LENGTH] = "";
	float m_BeaconHeardUntil = 0.0f;

	bool CanEmote() const;
	bool ChannelFree() const;
	bool IsLocal(int ClientId) const;
	void ClearPeers();
	void ClearPeer(int ClientId);
	void ForgetLeftPeers();
	void ForgetOnRename();
	bool AnswerOwed() const;
	void MarkAnswering();
	void OnBeaconSent();
	void OnBeaconLost();
	void UpdateAnnounce();
	void UpdateReply();
	void UpdatePet();
	void LocalPet(bool *pOn, char *pSkin, int SkinSize) const;
	void SendBeacon(int Kind);
	void SendPetBeacon(bool On, const char *pSkin);
	void AbortBeacon(bool Retry);
	void FlushEmoteQueue();
	bool HandleEcho(int Emoticon);
	bool Decode(int ClientId, int Emoticon);
	void OnBeacon(int ClientId, int Kind);
	void OnPetBeacon(int ClientId);
};

#endif
