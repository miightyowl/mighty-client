#ifndef GAME_CLIENT_COMPONENTS_PETTING_H
#define GAME_CLIENT_COMPONENTS_PETTING_H

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

class CPetting : public CComponent
{
	IGraphics::CTextureHandle m_PetTexture;
	int m_NumFrames = 0;

	class CPetState
	{
	public:
		float m_LastAimY = 0.0f;
		int m_LastDir = 0;
		float m_aReversalTimes[8] = {0};
		int m_ReversalCount = 0;
		float m_MinAimY = 0.0f;
		float m_MaxAimY = 0.0f;
		float m_AimRangeResetTime = 0.0f;

		float m_PetUntil = 0.0f;
		bool m_PetFromRight = false;
	};
	CPetState m_aStates[MAX_CLIENTS];

	void ResetOscillation(CPetState &State);
	void UpdatePetting();
	void RenderHands();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
};

#endif
