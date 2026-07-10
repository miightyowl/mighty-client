/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <memory>
#include <vector>

class CHttpRequest;

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;

	void UpdateLatestRelease();
	std::shared_ptr<CHttpRequest> m_pReleaseTask = nullptr;
	bool m_ReleaseRequested = false;
	bool m_ReleaseLoaded = false;
	char m_aReleaseTitle[128] = "";
	char m_aReleaseDesc[1024] = "";
	char m_aReleaseTag[32] = "";
	struct SReleaseEmphasis
	{
		int m_Start;
		int m_Length;
	};
	std::vector<SReleaseEmphasis> m_vReleaseEmphasis;
};

#endif
