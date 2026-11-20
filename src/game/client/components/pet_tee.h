#ifndef GAME_CLIENT_COMPONENTS_PET_TEE_H
#define GAME_CLIENT_COMPONENTS_PET_TEE_H

#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CPetTee : public CComponent
{
	struct CPet
	{
		vec2 m_Position = vec2(0.0f, 0.0f);
		vec2 m_Velocity = vec2(0.0f, 0.0f);
		vec2 m_Target = vec2(0.0f, 0.0f);
		vec2 m_WanderOffset = vec2(0.0f, 0.0f);
		float m_NextWanderTime = 0.0f;
		float m_NextBlockedCheck = 0.0f;

		vec2 m_LastPlayerPos = vec2(0.0f, 0.0f);
		vec2 m_PlayerVel = vec2(0.0f, 0.0f);
		vec2 m_PlayerVelRaw = vec2(0.0f, 0.0f);
		int m_TrailSide = 0;

		vec2 m_Dir = vec2(1.0f, 0.0f);
		vec2 m_GazeTarget = vec2(1.0f, 0.0f);
		float m_NextGazeTime = 0.0f;

		float m_Alpha = 0.0f;
	};

	CPet m_aPets[MAX_CLIENTS];

	bool SpotFree(vec2 PlayerPos, vec2 Offset, float Scale) const;
	void PickWanderOffset(CPet &Pet, float Time, float Scale, vec2 PlayerPos);
	static void PickGazeTarget(CPet &Pet, float Time);
	void RenderPet(int ClientId, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, float Scale, float Alpha);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMapLoad() override;
};

#endif
