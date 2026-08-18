#include "chess_board.h"

#include <base/math.h>
#include <base/str.h>

#include <algorithm>

namespace
{
	const char *START_POSITION =
		"rnbqkbnr"
		"pppppppp"
		"........"
		"........"
		"........"
		"........"
		"PPPPPPPP"
		"RNBQKBNR";

	const int MAX_HALF_MOVES = 100;

	bool CharIn(char Char, const char *pSet)
	{
		for(int i = 0; pSet[i] != '\0'; i++)
		{
			if(pSet[i] == Char)
				return true;
		}
		return false;
	}
}

void CChessBoard::Reset()
{
	for(int i = 0; i < 64; i++)
		m_aSquares[i] = START_POSITION[i];
	m_WhiteToMove = true;
	for(bool &Castling : m_aCastling)
		Castling = true;
	m_EnPassant = -1;
	m_HalfMoves = 0;
}

int CChessBoard::KingSquare(bool White) const
{
	const char King = White ? 'K' : 'k';
	for(int i = 0; i < 64; i++)
	{
		if(m_aSquares[i] == King)
			return i;
	}
	return -1;
}

bool CChessBoard::PathFree(int From, int To) const
{
	const int FileStep = std::clamp(File(To) - File(From), -1, 1);
	const int RankStep = std::clamp(Rank(To) - Rank(From), -1, 1);
	int Square = From + RankStep * 8 + FileStep;
	while(Square != To)
	{
		if(m_aSquares[Square] != '.')
			return false;
		Square += RankStep * 8 + FileStep;
	}
	return true;
}

bool CChessBoard::Attacked(int Square, bool ByWhite) const
{
	for(int From = 0; From < 64; From++)
	{
		const char Piece = m_aSquares[From];
		if(Piece == '.' || !IsOwn(Piece, ByWhite))
			continue;

		const int FileDiff = File(Square) - File(From);
		const int RankDiff = Rank(Square) - Rank(From);
		const int FileDist = absolute(FileDiff);
		const int RankDist = absolute(RankDiff);
		const char Upper = Piece >= 'a' ? Piece - ('a' - 'A') : Piece;

		switch(Upper)
		{
		case 'P':
			if(FileDist == 1 && RankDiff == (ByWhite ? -1 : 1))
				return true;
			break;
		case 'N':
			if((FileDist == 1 && RankDist == 2) || (FileDist == 2 && RankDist == 1))
				return true;
			break;
		case 'B':
			if(FileDist == RankDist && FileDist > 0 && PathFree(From, Square))
				return true;
			break;
		case 'R':
			if((FileDist == 0) != (RankDist == 0) && PathFree(From, Square))
				return true;
			break;
		case 'Q':
			if(((FileDist == RankDist && FileDist > 0) || ((FileDist == 0) != (RankDist == 0))) && PathFree(From, Square))
				return true;
			break;
		case 'K':
			if(FileDist <= 1 && RankDist <= 1 && (FileDist != 0 || RankDist != 0))
				return true;
			break;
		default:
			break;
		}
	}
	return false;
}

bool CChessBoard::InCheck(bool White) const
{
	const int King = KingSquare(White);
	return King >= 0 && Attacked(King, !White);
}

bool CChessBoard::PseudoLegal(int From, int To) const
{
	if(!Valid(From) || !Valid(To) || From == To)
		return false;

	const char Piece = m_aSquares[From];
	if(Piece == '.' || !IsOwn(Piece, m_WhiteToMove))
		return false;

	const char Target = m_aSquares[To];
	if(Target != '.' && IsOwn(Target, m_WhiteToMove))
		return false;

	const int FileDiff = File(To) - File(From);
	const int RankDiff = Rank(To) - Rank(From);
	const int FileDist = absolute(FileDiff);
	const int RankDist = absolute(RankDiff);
	const char Upper = Piece >= 'a' ? Piece - ('a' - 'A') : Piece;

	switch(Upper)
	{
	case 'P':
	{
		const int Forward = m_WhiteToMove ? -1 : 1;
		const int StartRank = m_WhiteToMove ? 6 : 1;
		if(FileDiff == 0 && RankDiff == Forward && Target == '.')
			return true;
		if(FileDiff == 0 && RankDiff == 2 * Forward && Rank(From) == StartRank &&
			Target == '.' && m_aSquares[From + Forward * 8] == '.')
			return true;
		if(FileDist == 1 && RankDiff == Forward && (Target != '.' || To == m_EnPassant))
			return true;
		return false;
	}
	case 'N':
		return (FileDist == 1 && RankDist == 2) || (FileDist == 2 && RankDist == 1);
	case 'B':
		return FileDist == RankDist && PathFree(From, To);
	case 'R':
		return (FileDist == 0) != (RankDist == 0) && PathFree(From, To);
	case 'Q':
		return ((FileDist == RankDist) || ((FileDist == 0) != (RankDist == 0))) && PathFree(From, To);
	case 'K':
	{
		if(FileDist <= 1 && RankDist <= 1)
			return true;

		if(RankDist != 0 || FileDist != 2)
			return false;
		const int HomeSquare = m_WhiteToMove ? 60 : 4;
		if(From != HomeSquare)
			return false;

		const bool KingSide = FileDiff > 0;
		const int Right = (m_WhiteToMove ? 0 : 2) + (KingSide ? 0 : 1);
		if(!m_aCastling[Right])
			return false;

		const int RookSquare = HomeSquare + (KingSide ? 3 : -4);
		if(m_aSquares[RookSquare] != (m_WhiteToMove ? 'R' : 'r'))
			return false;
		for(int Square = std::min(From, RookSquare) + 1; Square < std::max(From, RookSquare); Square++)
		{
			if(m_aSquares[Square] != '.')
				return false;
		}
		return true;
	}
	default:
		return false;
	}
}

void CChessBoard::PlayMove(int From, int To)
{
	const char Piece = m_aSquares[From];
	const char Upper = Piece >= 'a' ? Piece - ('a' - 'A') : Piece;
	const bool Capture = m_aSquares[To] != '.';

	if(Upper == 'P' && To == m_EnPassant && m_aSquares[To] == '.')
		m_aSquares[To + (m_WhiteToMove ? 8 : -8)] = '.';

	if(Upper == 'K' && absolute(File(To) - File(From)) == 2)
	{
		const bool KingSide = To > From;
		const int RookFrom = From + (KingSide ? 3 : -4);
		const int RookTo = From + (KingSide ? 1 : -1);
		m_aSquares[RookTo] = m_aSquares[RookFrom];
		m_aSquares[RookFrom] = '.';
	}

	m_aSquares[To] = Piece;
	m_aSquares[From] = '.';

	if(Upper == 'P' && (Rank(To) == 0 || Rank(To) == 7))
		m_aSquares[To] = m_WhiteToMove ? 'Q' : 'q';

	m_EnPassant = -1;
	if(Upper == 'P' && absolute(Rank(To) - Rank(From)) == 2)
		m_EnPassant = (From + To) / 2;

	if(Upper == 'K')
	{
		m_aCastling[m_WhiteToMove ? 0 : 2] = false;
		m_aCastling[m_WhiteToMove ? 1 : 3] = false;
	}

	const int aRookSquares[4] = {63, 56, 7, 0};
	for(int i = 0; i < 4; i++)
	{
		if(From == aRookSquares[i] || To == aRookSquares[i])
			m_aCastling[i] = false;
	}

	m_HalfMoves = (Capture || Upper == 'P') ? 0 : m_HalfMoves + 1;
	m_WhiteToMove = !m_WhiteToMove;
}

bool CChessBoard::CanMove(int From, int To) const
{
	return PseudoLegal(From, To);
}

bool CChessBoard::ApplyMove(int From, int To)
{
	if(!CanMove(From, To))
		return false;
	PlayMove(From, To);
	return true;
}

bool CChessBoard::HasAnyMove(bool White) const
{
	if(White != m_WhiteToMove)
		return false;
	for(int From = 0; From < 64; From++)
	{
		if(m_aSquares[From] == '.' || !IsOwn(m_aSquares[From], White))
			continue;
		for(int To = 0; To < 64; To++)
		{
			if(CanMove(From, To))
				return true;
		}
	}
	return false;
}

CChessBoard::EResult CChessBoard::Result() const
{
	if(KingSquare(true) < 0)
		return RESULT_BLACK_WON;
	if(KingSquare(false) < 0)
		return RESULT_WHITE_WON;
	if(!HasAnyMove(m_WhiteToMove))
		return RESULT_DRAW;
	if(m_HalfMoves >= MAX_HALF_MOVES)
		return RESULT_DRAW;
	return RESULT_RUNNING;
}

void CChessBoard::Serialize(char *pBuf, size_t Size) const
{
	char aSquares[65];
	for(int i = 0; i < 64; i++)
		aSquares[i] = m_aSquares[i];
	aSquares[64] = '\0';

	char aCastling[5];
	int Num = 0;
	const char aRights[4] = {'K', 'Q', 'k', 'q'};
	for(int i = 0; i < 4; i++)
	{
		if(m_aCastling[i])
			aCastling[Num++] = aRights[i];
	}
	if(Num == 0)
		aCastling[Num++] = '-';
	aCastling[Num] = '\0';

	str_format(pBuf, Size, "%s %c %s %d %d", aSquares, m_WhiteToMove ? 'w' : 'b', aCastling, m_EnPassant, m_HalfMoves);
}

bool CChessBoard::Parse(const char *pStr)
{
	char aSquares[80];
	const char *pRest = str_next_token(pStr, " ", aSquares, sizeof(aSquares));
	if(!pRest || str_length(aSquares) != 64)
		return false;
	for(int i = 0; i < 64; i++)
	{
		if(!CharIn(aSquares[i], "PNBRQKpnbrqk."))
			return false;
	}

	char aSide[4];
	pRest = str_next_token(pRest, " ", aSide, sizeof(aSide));
	if(!pRest || (str_comp(aSide, "w") != 0 && str_comp(aSide, "b") != 0))
		return false;

	char aCastling[8];
	pRest = str_next_token(pRest, " ", aCastling, sizeof(aCastling));
	if(!pRest)
		return false;

	char aEnPassant[8];
	pRest = str_next_token(pRest, " ", aEnPassant, sizeof(aEnPassant));
	if(!pRest)
		return false;
	int EnPassant;
	if(!str_toint(aEnPassant, &EnPassant) || EnPassant < -1 || EnPassant >= 64)
		return false;

	char aHalfMoves[8];
	str_next_token(pRest, " ", aHalfMoves, sizeof(aHalfMoves));
	int HalfMoves;
	if(!str_toint(aHalfMoves, &HalfMoves) || HalfMoves < 0)
		return false;

	for(int i = 0; i < 64; i++)
		m_aSquares[i] = aSquares[i];
	m_WhiteToMove = aSide[0] == 'w';
	const char aRights[4] = {'K', 'Q', 'k', 'q'};
	for(int i = 0; i < 4; i++)
		m_aCastling[i] = CharIn(aRights[i], aCastling);
	m_EnPassant = EnPassant;
	m_HalfMoves = HalfMoves;
	return true;
}

bool CChessBoard::operator==(const CChessBoard &Other) const
{
	for(int i = 0; i < 64; i++)
	{
		if(m_aSquares[i] != Other.m_aSquares[i])
			return false;
	}
	for(int i = 0; i < 4; i++)
	{
		if(m_aCastling[i] != Other.m_aCastling[i])
			return false;
	}
	return m_WhiteToMove == Other.m_WhiteToMove && m_EnPassant == Other.m_EnPassant && m_HalfMoves == Other.m_HalfMoves;
}
