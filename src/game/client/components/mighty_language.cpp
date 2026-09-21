#include "mighty_language.h"

#include <base/str.h>

#include <algorithm>
#include <cstdint>

namespace
{
	constexpr const char *MIGHTY_MARKER = "myla";
	constexpr int MIGHTY_MARKER_LEN = 4;
	constexpr int MIGHTY_MARKER_SYLLABLES = 2;
	constexpr int MIGHTY_SYLLABLES_PER_WORD = 3;

	constexpr unsigned char s_aKey[32] = {
		0x6D, 0x69, 0x67, 0x68, 0x74, 0x79, 0x2D, 0x6C,
		0x61, 0x6E, 0x67, 0x75, 0x61, 0x67, 0x65, 0x21,
		0x9E, 0x3B, 0x7F, 0x2A, 0xD4, 0x18, 0xC7, 0x55,
		0x01, 0xEE, 0x42, 0x8D, 0x36, 0xAF, 0x70, 0x19};

	constexpr const char *MIGHTY_CONSONANTS = "bcdfghjklmnprstv";
	constexpr const char *MIGHTY_ENDINGS = "aeioulry";

	int AlphabetIndex(const char *pAlphabet, char Character)
	{
		for(int i = 0; pAlphabet[i] != '\0'; i++)
		{
			if(pAlphabet[i] == Character)
				return i;
		}
		return -1;
	}

} // namespace

bool IsMightyMessage(const char *pBody)
{
	if(pBody == nullptr)
		return false;
	const char *pPayload = str_startswith(pBody, MIGHTY_MARKER);
	return pPayload != nullptr && pPayload[0] != '\0';
}

int EncodeMighty(const char *pPlain, char *pOut, int OutSize)
{
	if(pPlain == nullptr || pOut == nullptr || OutSize <= 0)
		return -1;

	const int PlainLen = str_length(pPlain);
	if(PlainLen == 0)
		return -1;

	const int NumPayloadSyllables = (PlainLen * 8 + 6) / 7;
	const int TotalSyllables = MIGHTY_MARKER_SYLLABLES + NumPayloadSyllables;
	const int NumSpaces = (TotalSyllables + MIGHTY_SYLLABLES_PER_WORD - 1) / MIGHTY_SYLLABLES_PER_WORD - 1;
	const int EncodedLen = MIGHTY_MARKER_LEN + NumPayloadSyllables * 2 + NumSpaces;
	if(EncodedLen + 1 > OutSize)
		return -1;

	int Out = 0;
	for(int i = 0; i < MIGHTY_MARKER_LEN; i++)
		pOut[Out++] = MIGHTY_MARKER[i];

	const unsigned char *pIn = (const unsigned char *)pPlain;
	uint32_t BitBuffer = 0;
	int NumBits = 0;
	int In = 0;
	int RemainingSyllables = TotalSyllables;
	int SyllablesInWord = MIGHTY_MARKER_SYLLABLES;
	int CurrentWordSize = RemainingSyllables == 4 ? 2 : MIGHTY_SYLLABLES_PER_WORD;
	while(In < PlainLen || NumBits > 0)
	{
		while(NumBits < 7 && In < PlainLen)
		{
			BitBuffer = (BitBuffer << 8) | (pIn[In] ^ s_aKey[In % 32]);
			NumBits += 8;
			In++;
		}

		int Value;
		if(NumBits >= 7)
		{
			NumBits -= 7;
			Value = (BitBuffer >> NumBits) & 0x7F;
		}
		else
		{
			Value = (BitBuffer << (7 - NumBits)) & 0x7F;
			NumBits = 0;
		}
		BitBuffer = NumBits == 0 ? 0 : BitBuffer & ((1U << NumBits) - 1);

		if(SyllablesInWord == CurrentWordSize)
		{
			pOut[Out++] = ' ';
			RemainingSyllables -= CurrentWordSize;
			CurrentWordSize = RemainingSyllables == 4 ? 2 : std::min(RemainingSyllables, MIGHTY_SYLLABLES_PER_WORD);
			SyllablesInWord = 0;
		}
		pOut[Out++] = MIGHTY_CONSONANTS[Value >> 3];
		pOut[Out++] = MIGHTY_ENDINGS[Value & 0x07];
		SyllablesInWord++;
	}
	pOut[Out] = '\0';
	return Out;
}

bool DecodeMighty(const char *pBody, char *pOut, int OutSize)
{
	if(pBody == nullptr || pOut == nullptr || !IsMightyMessage(pBody))
		return false;

	if(OutSize <= 0)
		return false;

	const char *pPayload = pBody + MIGHTY_MARKER_LEN;
	if(pPayload[0] == '\0')
		return false;

	uint32_t BitBuffer = 0;
	int NumBits = 0;
	int Out = 0;
	int NumPayloadSyllables = 0;
	int SyllablesInWord = MIGHTY_MARKER_SYLLABLES;
	while(*pPayload != '\0')
	{
		if(*pPayload == ' ')
		{
			if(SyllablesInWord < 2 || SyllablesInWord > MIGHTY_SYLLABLES_PER_WORD)
				return false;
			pPayload++;
			if(*pPayload == '\0')
				return false;
			SyllablesInWord = 0;
		}
		else if(SyllablesInWord >= MIGHTY_SYLLABLES_PER_WORD)
			return false;

		if(pPayload[0] == '\0' || pPayload[1] == '\0' || pPayload[0] == ' ' || pPayload[1] == ' ')
			return false;
		const int Consonant = AlphabetIndex(MIGHTY_CONSONANTS, pPayload[0]);
		const int Ending = AlphabetIndex(MIGHTY_ENDINGS, pPayload[1]);
		if(Consonant < 0 || Ending < 0)
			return false;

		BitBuffer = (BitBuffer << 7) | (Consonant << 3) | Ending;
		NumBits += 7;
		while(NumBits >= 8)
		{
			NumBits -= 8;
			const unsigned char Byte = (unsigned char)((BitBuffer >> NumBits) & 0xFF) ^ s_aKey[Out % 32];
			if(Byte == 0 || Out + 1 >= OutSize)
				return false;
			pOut[Out++] = (char)Byte;
		}
		BitBuffer = NumBits == 0 ? 0 : BitBuffer & ((1U << NumBits) - 1);

		pPayload += 2;
		NumPayloadSyllables++;
		SyllablesInWord++;
	}

	const int ExpectedSyllables = (Out * 8 + 6) / 7;
	if(Out == 0 || SyllablesInWord < 2 || NumPayloadSyllables != ExpectedSyllables || BitBuffer != 0)
		return false;
	pOut[Out] = '\0';
	return str_utf8_check(pOut) != 0;
}
