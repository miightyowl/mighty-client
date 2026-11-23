#ifndef GAME_CLIENT_MCLIENT_MARKER_H
#define GAME_CLIENT_MCLIENT_MARKER_H

namespace MClientMarker
{
inline constexpr unsigned VALUE_MASK = 0x00ffffffu;
inline constexpr unsigned BODY = 0x4du;
inline constexpr unsigned FEET = 0x42u;
inline constexpr unsigned BEACON_BODY = 0x4du;
inline constexpr unsigned BEACON_FEET = 0x43u;

inline int Mark(int Color, unsigned Byte)
{
	return static_cast<int>((static_cast<unsigned>(Color) & VALUE_MASK) | (Byte << 24));
}

inline bool Has(int ColorBody, int ColorFeet, unsigned BodyByte, unsigned FeetByte)
{
	return (static_cast<unsigned>(ColorBody) >> 24) == BodyByte && (static_cast<unsigned>(ColorFeet) >> 24) == FeetByte;
}

inline bool HasBrowserMarker(int ColorBody, int ColorFeet)
{
	return Has(ColorBody, ColorFeet, BODY, FEET);
}

inline bool IsMClient(int ColorBody, int ColorFeet)
{
	return HasBrowserMarker(ColorBody, ColorFeet) || Has(ColorBody, ColorFeet, BEACON_BODY, BEACON_FEET);
}
}

#endif
