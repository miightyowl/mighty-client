#ifndef GAME_CLIENT_COMPONENTS_MEMORY_CARDS_H
#define GAME_CLIENT_COMPONENTS_MEMORY_CARDS_H

namespace MemoryCards
{
	inline constexpr int NUM_COLUMNS = 5;
	inline constexpr int NUM_ROWS = 4;
	inline constexpr int NUM_CARDS = NUM_COLUMNS * NUM_ROWS;
	inline constexpr int NUM_PAIRS = NUM_CARDS / 2;

	struct SFace
	{
		int m_Stored;
		const char *m_pSkin;
		bool m_CustomColors;
		int m_ColorBody;
		int m_ColorFeet;
	};

	int NumStored();
	const char *StoredPath(int Index);
	int Version();
	void Deal(int Seed, SFace *pFaces, int *pCards);
}

#endif
