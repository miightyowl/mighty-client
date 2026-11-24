#ifndef GAME_CLIENT_COMPONENTS_MCLIENT_UPDATE_INSTALL_H
#define GAME_CLIENT_COMPONENTS_MCLIENT_UPDATE_INSTALL_H

#include <base/types.h>

#include <engine/shared/jobs.h>

#include <atomic>
#include <string>

class CMClientUpdateInstallJob : public IJob
{
public:
	CMClientUpdateInstallJob(const char *pArchivePath, const char *pInstallDir, const char *pClientExec);

	bool Success() const { return m_Success; }
	int Progress() const { return m_Progress; }

protected:
	void Run() override;

private:
	bool Install();

	std::string m_ArchivePath;
	std::string m_InstallDir;
	std::string m_ClientExec;
	std::atomic<bool> m_Success = false;
	std::atomic<int> m_Progress = 0;
};

#endif
