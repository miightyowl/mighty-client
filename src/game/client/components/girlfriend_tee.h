#ifndef GAME_CLIENT_COMPONENTS_GIRLFRIEND_TEE_H
#define GAME_CLIENT_COMPONENTS_GIRLFRIEND_TEE_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CGirlfriendTee : public CComponent
{
	enum class EMood
	{
		FOLLOW,
		DISTRACTED,
		CATCH_UP,
		TIRED,
	};

	vec2 m_Position = vec2(0.0f, 0.0f);
	vec2 m_Velocity = vec2(0.0f, 0.0f);
	vec2 m_Dir = vec2(1.0f, 0.0f);
	vec2 m_DistractTarget = vec2(0.0f, 0.0f);
	float m_Alpha = 0.0f;

	EMood m_Mood = EMood::FOLLOW;
	float m_NextDistractTime = 0.0f;
	float m_DistractUntil = 0.0f;
	float m_SideSign = -1.0f;
	float m_NextSideFlipTime = 0.0f;

	float m_IdleSeconds = 0.0f;
	float m_TiredThreshold = 17.0f;
	float m_LastSurpriseTime = -100.0f;
	float m_LastEmoticonTime = -100.0f;
	float m_LastHammerTime = -100.0f;
	int m_LastAttackTick = -1;
	float m_PrevPlayerSpeed = 0.0f;
	bool m_WasPlayerFrozen = false;
	bool m_aWasFrozen[MAX_CLIENTS] = {};

	int m_EyeEmote = 0;
	float m_EyeEmoteUntil = 0.0f;
	int m_Emoticon = -1;
	float m_EmoticonStart = -100.0f;

	bool m_Initialized = false;
	bool m_Grounded = false;

	void ResetState();
	void SpawnBeside(vec2 PlayerPos, vec2 PlayerDir);
	void UpdateAI(float Time, float Delta, vec2 PlayerPos, vec2 PlayerVel, vec2 PlayerDir, bool PlayerFrozen);
	void UpdateReactions(float Time, int LocalId, vec2 PlayerPos, vec2 PlayerVel, bool PlayerFrozen);
	void ShowEmoticon(int Emoticon, float Time, float MinGap = 1.2f);
	void SetEyeEmote(int Emote, float Time, float Duration);
	void RenderName(float Alpha) const;
	void RenderEmoticon(float Alpha) const;
	void RenderTee(float Alpha) const;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMapLoad() override;
	void OnReset() override;
};

#endif
