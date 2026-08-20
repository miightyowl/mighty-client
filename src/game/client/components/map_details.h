#ifndef GAME_CLIENT_COMPONENTS_MAP_DETAILS_H
#define GAME_CLIENT_COMPONENTS_MAP_DETAILS_H

#include <game/client/component.h>

constexpr int MAX_MAP_STARS = 5;

class CMapDetails : public CComponent
{
	int m_Stars = -1;
	int m_MedianTimeSeconds = -1;

	void Reset();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	int Stars() const { return m_Stars; }
	bool HasMedianTime() const { return m_MedianTimeSeconds >= 0; }
	int MedianTimeSeconds() const { return m_MedianTimeSeconds; }
};

#endif
