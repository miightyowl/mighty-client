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

		vec2 m_Dir = vec2(1.0f, 0.0f);
		vec2 m_GazeTarget = vec2(1.0f, 0.0f);
		float m_NextGazeTime = 0.0f;

		float m_Alpha = 0.0f;
	};

	CPet m_aPets[MAX_CLIENTS];

	static void PickWanderOffset(CPet &Pet, float Time, float Scale);
	static void PickGazeTarget(CPet &Pet, float Time);
	void RenderPet(int ClientId, const char *pSkinName, float Scale, float Alpha);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnMapLoad() override;
};

#endif
