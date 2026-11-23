#ifndef GAME_CLIENT_COMPONENTS_FINISH_FANS_H
#define GAME_CLIENT_COMPONENTS_FINISH_FANS_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

class CAnimState;

class CFinishFans : public CComponent
{
	struct SFan
	{
		vec2 m_Pos;
		vec2 m_FinishPos;
		vec2 m_Dir = vec2(1.0f, 0.0f);
		float m_Phase;
		ColorRGBA m_ColorBody;
		ColorRGBA m_ColorFeet;
		int m_Weapon;
		vec2 m_IdleDir = vec2(1.0f, 0.0f);
		bool m_IdleAtFinish = false;
		float m_NextGlance = 0.0f;
		bool m_Noticed = false;
		float m_NoticedAt = -1.0f;
	};
	int m_NoticeRaceTick = -1;
	void RenderEmoticon(int Emoticon, vec2 Pos, float EmoticonTime, float Length, float Alpha);
	static void PickIdleGaze(SFan &Fan, float Time);
	std::vector<SFan> m_vFans;

	float m_CelebrationStart = -1.0f;
	int m_CelebrationClientId = -1;
	bool m_Gone = false;
	int m_FinishedRaceTick = -1;

	void FindSpots();
	void StartCelebration(int ClientId);
	void RenderFan(SFan &Fan, float Time, float FrameTime, float Alpha);
	void RenderWeapon(const SFan &Fan, const CAnimState &State, vec2 Pos, float Time, bool Celebrating, float Alpha);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnReset() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
};

#endif
