#pragma once
#include <array>
#include <memory>
#include <vector>
#include <string>
#include <iosfwd>
#include "Piece.h"
#include "Move.h"

// Board encapsulates all board state (the 8x8 grid of pieces, en-passant state)
// and every rule that depends purely on "what does the board look like right now":
// legality of moves (including check-safety, castling, en passant, promotion),
// check/checkmate/stalemate detection, draw-by-insufficient-material detection,
// a position key for repetition detection, and serialization for save/load.
class Board {
private:
    std::array<std::array<std::unique_ptr<Piece>, 8>, 8> grid;
    Position enPassantTarget; // square a pawn can be captured en passant on, invalid if none

    void deepCopyFrom(const Board& other);

public:
    Board();
    Board(const Board& other);
    Board& operator=(const Board& other);
    Board(Board&&) noexcept = default;
    Board& operator=(Board&&) noexcept = default;
    ~Board() = default;

    void setupInitialPosition();
    void clearBoard();

    Piece* getPiece(const Position& pos) const;
    void setPiece(const Position& pos, std::unique_ptr<Piece> piece);
    std::unique_ptr<Piece> removePiece(const Position& pos);

    bool isEmpty(const Position& pos) const;
    bool isEnemyAt(const Position& pos, Color color) const;
    bool isAllyAt(const Position& pos, Color color) const;

    Position getEnPassantTarget() const { return enPassantTarget; }
    void setEnPassantTarget(const Position& pos) { enPassantTarget = pos; }
    void clearEnPassantTarget() { enPassantTarget = Position(); }

    Position findKing(Color color) const;
    bool isSquareAttacked(const Position& pos, Color byColor) const;
    bool isInCheck(Color color) const;

    // Fully legal moves for the piece on 'from' (pseudo-legal moves filtered
    // by "does this leave my own king in check", plus castling when applicable).
    // capturesOnly = true returns only captures and promotions - used by the AI's
    // quiescence search so it does not waste time simulating quiet moves.
    std::vector<Move> getLegalMoves(const Position& from, bool capturesOnly = false) const;

    // All legal moves for every piece of the given color - empty means
    // checkmate (if in check) or stalemate (if not).
    std::vector<Move> getAllLegalMoves(Color color, bool capturesOnly = false) const;

    // Applies an already-validated Move to the board (handles capture removal,
    // castling rook hop, en passant capture, promotion, and en-passant-target bookkeeping).
    // The move's own fields are validated BEFORE anything is changed, so a bad
    // Move throws without leaving the board half-modified.
    void executeMove(const Move& move);

    // True when neither side can possibly deliver checkmate (K vs K, K+minor vs K,
    // or only same-coloured bishops). Used for the insufficient-material draw.
    bool hasInsufficientMaterial() const;

    // A string that identifies the position for repetition purposes: piece
    // placement + side to move + castling rights + en-passant square (only if an
    // en passant capture is actually possible).
    std::string positionKey(Color sideToMove) const;

    Board clone() const;

    void print(bool useUnicode = false) const;

    // Simple, human-readable serialization used by save/load.
    void serialize(std::ostream& out) const;
    // Parses into a temporary and only replaces *this once everything has been
    // read and validated, so a failed load never leaves a half-built board behind.
    void deserialize(std::istream& in);
};
