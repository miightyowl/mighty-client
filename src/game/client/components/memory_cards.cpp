#include "memory_cards.h"

#include <iterator>
#include <utility>

namespace
{
	struct SStoredFace
	{
		const char *m_pPath;
		bool m_CustomColors;
		int m_ColorBody;
		int m_ColorFeet;
	};

	const SStoredFace g_aStoredFaces[] = {
		{"memory/01.png", false, 0, 0},
		{"memory/02.png", true, 65395, 7601920},
		{"memory/03.png", true, 0, 11206400},
		{"memory/04.png", false, 0, 0},
		{"memory/05.png", true, 9633563, 16772095},
		{"memory/06.png", true, 10281524, 2752511},
		{"memory/07.png", false, 0, 0},
		{"memory/08.png", false, 0, 0},
		{"memory/09.png", true, 56320, 2677760},
		{"memory/10.png", true, 5793152, 4929590},
		{"memory/11.png", false, 0, 0},
		{"memory/12.png", true, 84, 4194303},
		{"memory/13.png", true, 1871055, 15724592},
		{"memory/14.png", true, 25600, 13631232},
		{"memory/15.png", false, 0, 0},
		{"memory/16.png", true, 1834752, 1820672},
		{"memory/17.png", true, 65280, 11111111},
		{"memory/18.png", true, 6029352, 1538560},
		{"memory/19.png", true, 7601994, 13369343},
		{"memory/13.png", true, 4914688, 2424586},
		{"memory/20.png", true, 11796589, 1093241},
	};

	const char *g_apFillSkins[] = {
		"default",
		"bluekitty",
		"bluestripe",
		"brownbear",
		"cammo",
		"cammostripes",
		"coala",
		"limekitty",
		"pinky",
		"redbopp",
		"redstripe",
		"saddo",
		"toptri",
		"twinbop",
		"twintri",
		"warpaint",
		"x_ninja",
		"beast",
		"bomb",
		"dino",
		"dragon",
		"evil",
		"ghost",
		"giraffe",
		"greyfox",
		"mouse",
		"penguin",
		"teerasta",
		"oldman",
		"nanas",
		"musmann",
		"wartee",
		"whis",
		"jeet",
		"antiantey",
		"greensward",
		"voodoo_tee",
		"veteran",
		"nersif",
		"hammie-chew",
	};

	const int MIN_STORED = 4;
	const int MAX_STORED = 7;

	class CDealRng
	{
		unsigned m_State;

	public:
		explicit CDealRng(int Seed) :
			m_State((unsigned)Seed * 2654435761u ^ 0x5bd1e995u)
		{
			if(m_State == 0)
				m_State = 1;
		}

		unsigned Next()
		{
			m_State ^= m_State << 13;
			m_State ^= m_State >> 17;
			m_State ^= m_State << 5;
			return m_State;
		}

		int Below(int Max)
		{
			return (int)(Next() % (unsigned)Max);
		}

		template<typename T>
		void Shuffle(T *pItems, int Num)
		{
			for(int i = Num - 1; i > 0; i--)
				std::swap(pItems[i], pItems[Below(i + 1)]);
		}
	};

	int RandomColor(CDealRng &Rng)
	{
		const int Hue = Rng.Below(256);
		const int Sat = 110 + Rng.Below(146);
		const int Lgt = 90 + Rng.Below(110);
		return (Hue << 16) | (Sat << 8) | Lgt;
	}
}

int MemoryCards::NumStored()
{
	return (int)std::size(g_aStoredFaces);
}

const char *MemoryCards::StoredPath(int Index)
{
	return g_aStoredFaces[Index].m_pPath;
}

int MemoryCards::Version()
{
	unsigned Hash = 2166136261u;
	auto Mix = [&Hash](unsigned Value) {
		Hash ^= Value;
		Hash *= 16777619u;
	};
	auto MixText = [&Mix](const char *pText) {
		for(; *pText; pText++)
			Mix((unsigned char)*pText);
		Mix(0);
	};

	Mix(NUM_CARDS);
	Mix(2);
	for(const SStoredFace &Face : g_aStoredFaces)
	{
		MixText(Face.m_pPath);
		Mix(Face.m_CustomColors);
		Mix((unsigned)Face.m_ColorBody);
		Mix((unsigned)Face.m_ColorFeet);
	}
	for(const char *pSkin : g_apFillSkins)
		MixText(pSkin);
	return (int)(Hash % 1000u);
}

void MemoryCards::Deal(int Seed, SFace *pFaces, int *pCards)
{
	CDealRng Rng(Seed);

	int aStored[std::size(g_aStoredFaces)];
	for(int i = 0; i < NumStored(); i++)
		aStored[i] = i;
	Rng.Shuffle(aStored, NumStored());

	int aFill[std::size(g_apFillSkins)];
	for(int i = 0; i < (int)std::size(g_apFillSkins); i++)
		aFill[i] = i;
	Rng.Shuffle(aFill, (int)std::size(g_apFillSkins));

	int NumFromStored = MIN_STORED + Rng.Below(MAX_STORED - MIN_STORED + 1);
	if(NumFromStored > NumStored())
		NumFromStored = NumStored();

	for(int Pair = 0; Pair < NUM_PAIRS; Pair++)
	{
		SFace &Face = pFaces[Pair];
		if(Pair < NumFromStored)
		{
			const SStoredFace &Stored = g_aStoredFaces[aStored[Pair]];
			Face.m_Stored = aStored[Pair];
			Face.m_pSkin = nullptr;
			Face.m_CustomColors = Stored.m_CustomColors;
			Face.m_ColorBody = Stored.m_ColorBody;
			Face.m_ColorFeet = Stored.m_ColorFeet;
			continue;
		}

		Face.m_Stored = -1;
		Face.m_pSkin = g_apFillSkins[aFill[Pair - NumFromStored]];
		Face.m_CustomColors = Rng.Below(3) != 0;
		Face.m_ColorBody = Face.m_CustomColors ? RandomColor(Rng) : 0;
		Face.m_ColorFeet = Face.m_CustomColors ? RandomColor(Rng) : 0;
	}

	for(int Card = 0; Card < NUM_CARDS; Card++)
		pCards[Card] = Card / 2;
	Rng.Shuffle(pCards, NUM_CARDS);
}
