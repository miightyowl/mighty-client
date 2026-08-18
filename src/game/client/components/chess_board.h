#ifndef GAME_CLIENT_COMPONENTS_CHESS_BOARD_H
#define GAME_CLIENT_COMPONENTS_CHESS_BOARD_H

#include <cstddef>

class CChessBoard
{
public:
	enum EResult
	{
		RESULT_RUNNING,
		RESULT_WHITE_WON,
		RESULT_BLACK_WON,
		RESULT_DRAW,
	};

	CChessBoard() { Reset(); }

	void Reset();

	char Piece(int Square) const { return m_aSquares[Square]; }
	bool WhiteToMove() const { return m_WhiteToMove; }

	static bool IsWhite(char Piece) { return Piece >= 'A' && Piece <= 'Z'; }
	static bool IsBlack(char Piece) { return Piece >= 'a' && Piece <= 'z'; }
	static bool IsOwn(char Piece, bool White) { return White ? IsWhite(Piece) : IsBlack(Piece); }

	bool CanMove(int From, int To) const;

	bool ApplyMove(int From, int To);

	bool InCheck(bool White) const;
	bool HasAnyMove(bool White) const;
	EResult Result() const;

	void Serialize(char *pBuf, size_t Size) const;
	bool Parse(const char *pStr);
	bool operator==(const CChessBoard &Other) const;

private:
	char m_aSquares[64];
	bool m_WhiteToMove;
	bool m_aCastling[4];
	int m_EnPassant;
	int m_HalfMoves;

	static int File(int Square) { return Square % 8; }
	static int Rank(int Square) { return Square / 8; }
	static bool Valid(int Square) { return Square >= 0 && Square < 64; }

	bool PseudoLegal(int From, int To) const;
	bool PathFree(int From, int To) const;
	bool Attacked(int Square, bool ByWhite) const;
	int KingSquare(bool White) const;
	void PlayMove(int From, int To);
};

#endif
