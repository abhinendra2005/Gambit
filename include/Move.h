#pragma once
#include <string>
#include "Position.h"
#include "Piece.h"

enum class MoveType {
    NORMAL,
    DOUBLE_PAWN,
    EN_PASSANT,
    CASTLE_KINGSIDE,
    CASTLE_QUEENSIDE,
    PROMOTION
};

struct Move {
    Position from;
    Position to;
    PieceType movedType = PieceType::NONE;
    Color movedColor = Color::NONE;
    PieceType capturedType = PieceType::NONE;   // NONE means no capture
    MoveType type = MoveType::NORMAL;
    PieceType promotionType = PieceType::NONE;  // valid only when type == PROMOTION

    // "+" (check) or "#" (checkmate). It is only known once the resulting
    // position has been evaluated, so Game fills it in afterwards.
    std::string checkSuffix;

    // If set (e.g. after reloading a save file where only notation was kept),
    // toAlgebraicNotation() returns this verbatim instead of recomputing it.
    std::string notationOverride;

    bool isCapture() const { return capturedType != PieceType::NONE; }

    std::string toAlgebraicNotation() const {
        if (!notationOverride.empty()) return notationOverride;

        if (type == MoveType::CASTLE_KINGSIDE) return "O-O" + checkSuffix;
        if (type == MoveType::CASTLE_QUEENSIDE) return "O-O-O" + checkSuffix;

        std::string s = from.toAlgebraic();
        s += (isCapture() || type == MoveType::EN_PASSANT) ? "x" : "-";
        s += to.toAlgebraic();

        if (type == MoveType::PROMOTION) {
            s += "=";
            s += Piece::pieceTypeToChar(promotionType);
        }
        s += checkSuffix;
        if (type == MoveType::EN_PASSANT) {
            s += " e.p.";
        }
        return s;
    }
};
