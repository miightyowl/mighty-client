#ifndef GAME_CLIENT_COMPONENTS_MAODIE_WALK_H
#define GAME_CLIENT_COMPONENTS_MAODIE_WALK_H

#include <game/client/component.h>

class CMaodieWalk : public CComponent
{
	bool m_Active = false;
	float m_NextWalkTime = 0.0f;
	float m_PosX = 0.0f;
	float m_Dir = 1.0f;

	void ScheduleNext();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnReset() override;
	void OnRender() override;
};

#endif
