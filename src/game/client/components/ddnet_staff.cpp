#include "ddnet_staff.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/external/json-parser/json.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <string_view>

static constexpr const char *STAFF_URL = "https://ddnet.org/staff/";
static constexpr const char *MCLIENT_FILE = "mclient.json";
static constexpr size_t MIN_PLAUSIBLE_STAFF = 10;

static const char *const BUILTIN_STAFF_SLUGS[] = {
	"deen", "heinrich5991", "Learath2", "Robyt3", "louis", "melon", "murpi", "snailx3",
	"0x314", "APFFF", "AvapiX", "bencie", "Beemo", "Cendren", "danishmod29", "dmen", "Draggory", "Gumba",
	"Gwendal", "Insanity", "ilaK", "JUST-32-A-32-SNIPER", "Keiss", "Mery-32--9829-", "Ounni", "Paralix", "ReD",
	"Rheme", "Sans3108", "Sca", "Stouls", "Timiitz", "vZechz", "z0rba", "Carrey", "Chertila", "Deeper",
	"Ham5terzilla", "k0rae", "Kandiifloss", "Flaundie", "NANAMAR", "63-32-turtles", "Pink-95-Ko", "TOWARISH",
	"Xteriche", "ZombieTop", "matatas", "De-235-ivid-33-", "Grk0ne", "Mimi", "-46-pet", "pa", "Paxtell",
	"strauberi", "Suado-32-Cowboy", "G-829--851-vnpy-820-", "Aliaxy", "Skaydox", "Hav", "Shinoa", "Souly",
	"Bloodslain", "garry", "Ryu", "-39--94--39-", "Gloomy", "kyokaiwei", "Pioooooo", "Shiriusu", "summeri",
	"Zerol-32-Acqua", "ZZARO", "-183--34013--34013--34013--34013-", "-25705--35199--25705--35199-", "Lem0n"};

bool CDDNetStaff::DecodeSlug(const char *pSlug, std::string &Name)
{
	Name.clear();
	for(const char *p = pSlug; *p;)
	{
		if(*p != '-')
		{
			Name += *p++;
			continue;
		}
		const char *pEnd = str_find(p + 1, "-");
		if(!pEnd || pEnd == p + 1 || pEnd - p > 9)
			return false;
		int CodePoint = 0;
		for(const char *pDigit = p + 1; pDigit < pEnd; pDigit++)
		{
			if(*pDigit < '0' || *pDigit > '9')
				return false;
			CodePoint = CodePoint * 10 + (*pDigit - '0');
		}
		if(CodePoint <= 0 || CodePoint > 0x10FFFF)
			return false;
		char aUtf8[4];
		const int Length = str_utf8_encode(aUtf8, CodePoint);
		Name.append(aUtf8, Length);
		p = pEnd + 1;
	}
	return !Name.empty();
}

std::set<std::string> CDDNetStaff::ParseStaffPage(const char *pHtml, size_t Size)
{
	std::set<std::string> Names;
	const std::string_view Html(pHtml, Size);

	const size_t Begin = Html.find("Team Leaders");
	if(Begin == std::string_view::npos)
		return Names;
	const size_t Moderators = Html.find("DDRace Moderators", Begin);
	if(Moderators == std::string_view::npos)
		return Names;
	const size_t End = std::min(Html.find("Forum Moderators", Moderators), Html.find("<h3", Moderators));
	if(End == std::string_view::npos)
		return Names;

	static constexpr std::string_view LINK = "href=\"/players/";
	for(size_t Pos = Html.find(LINK, Begin); Pos < End; Pos = Html.find(LINK, Pos))
	{
		Pos += LINK.size();
		const size_t SlugEnd = Html.find_first_of("/\"", Pos);
		if(SlugEnd == std::string_view::npos || SlugEnd > End)
			break;
		const std::string Slug(Html.substr(Pos, SlugEnd - Pos));
		std::string Name;
		if(DecodeSlug(Slug.c_str(), Name))
			Names.insert(Name);
		Pos = SlugEnd;
	}
	return Names;
}

void CDDNetStaff::LoadBuiltin()
{
	m_Names.clear();
	for(const char *pSlug : BUILTIN_STAFF_SLUGS)
	{
		std::string Name;
		if(DecodeSlug(pSlug, Name))
			m_Names.insert(Name);
	}
}

bool CDDNetStaff::LoadSaved()
{
	void *pFileData;
	unsigned FileSize;
	if(!Storage()->ReadFile(MCLIENT_FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return false;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pData = json_parse_ex(&JsonSettings, static_cast<const json_char *>(pFileData), FileSize, aError);
	free(pFileData);
	if(pData == nullptr)
		return false;

	std::set<std::string> Names;
	const json_value &Staff = (*pData)["ddnet_staff"];
	if(Staff.type == json_array)
	{
		for(unsigned i = 0; i < Staff.u.array.length; ++i)
		{
			if(Staff[i].type == json_string && Staff[i].u.string.length > 0)
				Names.insert(Staff[i].u.string.ptr);
		}
	}
	json_value_free(pData);

	if(Names.size() < MIN_PLAUSIBLE_STAFF)
		return false;
	m_Names = std::move(Names);
	return true;
}

void CDDNetStaff::OnInit()
{
	m_HasFetchedNames = LoadSaved();
	if(!m_HasFetchedNames)
		LoadBuiltin();
}

void CDDNetStaff::OnUpdate()
{
	PollRequest();

	if(m_Fetched || m_pRequest || !g_Config.m_ClNamePlatesModeratorMark)
		return;

	m_Fetched = true;
	m_pRequest = HttpGet(STAFF_URL);
	m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pRequest->MaxResponseSize(1024 * 1024);
	m_pRequest->LogProgress(HTTPLOG::FAILURE);
	Http()->Run(m_pRequest);
}

void CDDNetStaff::PollRequest()
{
	if(!m_pRequest || !m_pRequest->Done())
		return;

	if(m_pRequest->State() == EHttpState::DONE && m_pRequest->StatusCode() < 400)
	{
		unsigned char *pData;
		size_t DataSize;
		m_pRequest->Result(&pData, &DataSize);
		std::set<std::string> Names = ParseStaffPage(reinterpret_cast<const char *>(pData), DataSize);
		if(Names.size() >= MIN_PLAUSIBLE_STAFF)
		{
			const bool Changed = !m_HasFetchedNames || Names != m_Names;
			m_Names = std::move(Names);
			m_HasFetchedNames = true;
			if(Changed)
				GameClient()->m_Menus.SaveMClient();
			log_info("ddnet_staff", "loaded %d staff members from ddnet.org", (int)m_Names.size());
		}
		else
		{
			log_error("ddnet_staff", "could not parse staff list from ddnet.org, keeping the previous list");
		}
	}
	m_pRequest = nullptr;
}

void CDDNetStaff::OnShutdown()
{
	if(m_pRequest)
		m_pRequest->Abort();
	m_pRequest = nullptr;
}

bool CDDNetStaff::IsStaff(const char *pName) const
{
	return pName[0] != '\0' && m_Names.contains(pName);
}
