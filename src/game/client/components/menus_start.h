/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MENUS_START_H
#define GAME_CLIENT_COMPONENTS_MENUS_START_H

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <memory>
#include <vector>

class IHttpRequest;

class CMenusStart : public CComponentInterfaces
{
public:
	void RenderStartMenu(CUIRect MainView);

private:
	bool CheckHotKey(int Key) const;

	void UpdateLatestRelease();
	static bool VersionNewer(const char *pLatest, const char *pCurrent);

	enum class EUpdateState
	{
		NONE,
		DOWNLOADING,
		INSTALLED,
		FAILED,
	};
	void StartUpdateDownload();
	void UpdateDownloadState();
	bool InstallUpdate();
	std::shared_ptr<IHttpRequest> m_pUpdateTask = nullptr;
	EUpdateState m_UpdateState = EUpdateState::NONE;
	char m_aUpdateAssetUrl[512] = "";

	std::shared_ptr<IHttpRequest> m_pReleaseTask = nullptr;
	bool m_ReleaseRequested = false;
	bool m_ReleaseLoaded = false;
	bool m_UpdateAvailable = false;
	char m_aReleaseTitle[128] = "";
	char m_aReleaseDesc[1024] = "";
	char m_aReleaseTag[32] = "";
	char m_aReleaseUrl[256] = "";
	struct SReleaseEmphasis
	{
		int m_Start;
		int m_Length;
	};
	std::vector<SReleaseEmphasis> m_vReleaseEmphasis;
};

#endif
