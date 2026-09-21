#ifndef GAME_CLIENT_COMPONENTS_MIGHTY_LANGUAGE_H
#define GAME_CLIENT_COMPONENTS_MIGHTY_LANGUAGE_H

constexpr const char *MIGHTY_LANG_CODE = "mty";
constexpr const char *MIGHTY_LANG_NAME = "mighty (secret language)";
constexpr const char *MIGHTY_CHAT_LABEL = "mighty";

bool IsMightyMessage(const char *pBody);
int EncodeMighty(const char *pPlain, char *pOut, int OutSize);
bool DecodeMighty(const char *pBody, char *pOut, int OutSize);

#endif
