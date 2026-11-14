#ifndef GAME_CLIENT_COMPONENTS_DDNET_STAFF_H
#define GAME_CLIENT_COMPONENTS_DDNET_STAFF_H

#include <game/client/component.h>

#include <memory>
#include <set>
#include <string>

class IHttpRequest;

class CDDNetStaff : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	void OnInit() override;
	void OnUpdate() override;
	void OnShutdown() override;

	bool IsStaff(const char *pName) const;
	const std::set<std::string> &Names() const { return m_Names; }
	bool HasFetchedNames() const { return m_HasFetchedNames; }

	static bool DecodeSlug(const char *pSlug, std::string &Name);
	static std::set<std::string> ParseStaffPage(const char *pHtml, size_t Size);

private:
	std::set<std::string> m_Names;
	std::shared_ptr<IHttpRequest> m_pRequest;
	bool m_Fetched = false;
	bool m_HasFetchedNames = false;

	void LoadBuiltin();
	bool LoadSaved();
	void PollRequest();
};

#endif
