#ifndef GAME_CLIENT_COMPONENTS_FINISH_FIREWORKS_H
#define GAME_CLIENT_COMPONENTS_FINISH_FIREWORKS_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

class CFinishFireworks : public CComponent
{
	struct SRocket
	{
		vec2 m_StartPos;
		vec2 m_TargetPos;
		float m_LaunchTime;
		float m_FlightTime;
		ColorRGBA m_Color;
	};
	std::vector<SRocket> m_vRockets;
	vec2 m_CenterPos = vec2(0.0f, 0.0f);
	float m_CelebrationEnd = -1.0f;
	float m_NextRocketTime = 0.0f;

	int m_LaunchSample = -2;
	int m_ExplodeSample = -2;

	void EnsureSoundsLoaded();
	void LaunchRocket(vec2 CenterPos, float Now);
	void SpawnExplosion(vec2 Pos, ColorRGBA Color);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnRender() override;
	void OnReset() override;

	void Start();
};

#endif
