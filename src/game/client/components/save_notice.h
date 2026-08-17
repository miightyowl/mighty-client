#ifndef GAME_CLIENT_COMPONENTS_SAVE_NOTICE_H
#define GAME_CLIENT_COMPONENTS_SAVE_NOTICE_H

#include <engine/console.h>

#include <game/client/component.h>

#include <string>
#include <vector>

class CSaveNotice : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnMapLoad() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

private:
	struct SSave
	{
		std::string m_Timestamp;
		std::string m_Players;
		std::string m_Code;
	};

	bool m_Pending = false;

	void ShowSaves(bool Verbose);

	static void ConShowSaves(IConsole::IResult *pResult, void *pUserData);
	void CollectSaves(const char *pMap, std::vector<SSave> &vSaves) const;
};

#endif
