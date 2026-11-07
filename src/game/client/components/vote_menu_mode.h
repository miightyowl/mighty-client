#ifndef GAME_CLIENT_COMPONENTS_VOTE_MENU_MODE_H
#define GAME_CLIENT_COMPONENTS_VOTE_MENU_MODE_H

enum class EVoteMenuMode
{
	LEGACY,
	DDNET_CATALOG,
	DDNET_SERVER_VOTES,
};

constexpr EVoteMenuMode SelectVoteMenuMode(bool RedesignEnabled, bool IsDDNetCommunity, bool MapCatalogAvailable, bool MapCatalogFailed)
{
	if(!RedesignEnabled || !IsDDNetCommunity)
		return EVoteMenuMode::LEGACY;
	if(MapCatalogAvailable || !MapCatalogFailed)
		return EVoteMenuMode::DDNET_CATALOG;
	return EVoteMenuMode::DDNET_SERVER_VOTES;
}

#endif
