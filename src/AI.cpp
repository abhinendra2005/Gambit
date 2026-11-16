#include "AI.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace {

// How many extra plies quiescence search may add at the horizon before it
// gives up and just evaluates. Keeps long capture chains from exploding.
constexpr int MAX_QUIESCENCE_DEPTH = 6;

// Material values in centipawns, used for move ordering (MVV-LVA).
int pieceValue(PieceType t) {
    switch (t) {
        case PieceType::PAWN:   return 100;
        case PieceType::KNIGHT: return 300;
        case PieceType::BISHOP: return 300;
        case PieceType::ROOK:   return 500;
        case PieceType::QUEEN:  return 900;
        case PieceType::KING:   return 20000;
        default:                return 0;
    }
}

// Piece-square tables ("simplified evaluation function" by T. Michniewski).
// Written from White's point of view with the table's first row = rank 8.
// They nudge pieces towards good squares: knights to the centre, pawns forward,
// the king tucked away behind its pawns, rooks to the 7th rank, and so on.
const int PST_PAWN[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0
};
const int PST_KNIGHT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};
const int PST_BISHOP[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};
const int PST_ROOK[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0
};
const int PST_QUEEN[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20
};
const int PST_KING[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

// Positional bonus for a piece on (row, col). Our rows count up from rank 1, the
// tables count down from rank 8, so White flips the row; Black uses the mirror image.
int positionalBonus(PieceType type, Color color, int row, int col) {
    int idx = (color == Color::WHITE) ? (7 - row) * 8 + col : row * 8 + col;
    switch (type) {
        case PieceType::PAWN:   return PST_PAWN[idx];
        case PieceType::KNIGHT: return PST_KNIGHT[idx];
        case PieceType::BISHOP: return PST_BISHOP[idx];
        case PieceType::ROOK:   return PST_ROOK[idx];
        case PieceType::QUEEN:  return PST_QUEEN[idx];
        case PieceType::KING:   return PST_KING[idx];
        default:                return 0;
    }
}

// Move-ordering score: bigger = searched earlier.
//   MVV-LVA: take the Most Valuable Victim with the Least Valuable Attacker first
//   (pawn takes queen is tried before queen takes pawn). Promotions go first of all.
int moveOrderScore(const Move& m) {
    int s = 0;
    if (m.isCapture()) s += 10000 + 10 * pieceValue(m.capturedType) - pieceValue(m.movedType) / 100;
    if (m.type == MoveType::PROMOTION) s += 20000 + pieceValue(m.promotionType);
    return s;
}

} // namespace

int AI::evaluate(const Board& board, Color side) {
    // A position where nobody can ever mate is a dead draw.
    if (board.hasInsufficientMaterial()) return 0;

    int score = 0; // White's point of view
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = board.getPiece(Position(r, c));
            if (!p) continue;
            // Material comes straight from Piece::getValue() (pawn=1 ... queen=9) x 100.
            int v = p->getValue() * 100 + positionalBonus(p->getType(), p->getColor(), r, c);
            score += (p->getColor() == Color::WHITE) ? v : -v;
        }
    }
    return (side == Color::WHITE) ? score : -score;
}

void AI::orderMoves(std::vector<Move>& moves) const {
    // stable_sort keeps the original order among equal scores, so results are deterministic.
    std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
        return moveOrderScore(a) > moveOrderScore(b);
    });
}

// Quiescence search: at the horizon, don't just evaluate - first play out the
// captures, so we never stop in the middle of an exchange (the "horizon effect").
int AI::quiesce(const Board& board, Color side, int alpha, int beta,
                int ply, int qdepth, long long& nodes) const {
    ++nodes;
    if (qdepth <= 0) return evaluate(board, side);

    std::vector<Move> moves;
    int best;

    if (board.isInCheck(side)) {
        // In check there is no "standing pat": we must escape, so look at every legal reply.
        moves = board.getAllLegalMoves(side);
        if (moves.empty()) return -(MATE_SCORE - ply); // checkmated
        best = -INF;
    } else {
        // "Stand pat": the side to move may decline all captures and keep the static score.
        int standPat = evaluate(board, side);
        if (settings.useAlphaBeta) {
            if (standPat >= beta) return standPat;
            if (standPat > alpha) alpha = standPat;
        }
        best = standPat;
        moves = board.getAllLegalMoves(side, /*capturesOnly=*/true);
    }

    if (settings.useMoveOrdering) orderMoves(moves);

    for (const Move& m : moves) {
        Board child = board.clone();
        child.executeMove(m);
        int score = -quiesce(child, oppositeColor(side), -beta, -alpha, ply + 1, qdepth - 1, nodes);
        if (score > best) best = score;
        if (settings.useAlphaBeta) {
            if (best > alpha) alpha = best;
            if (alpha >= beta) break; // beta cut-off
        }
    }
    return best;
}

// Negamax: minimax written so both players "maximise" - the score of a position
// for the side to move is the negative of the best score the opponent can get.
//   alpha = best score the side to move is already guaranteed
//   beta  = best score the opponent will allow; once alpha >= beta the rest of
//           this branch cannot change the result and is skipped (pruning).
int AI::negamax(const Board& board, Color side, int depth, int alpha, int beta,
                int ply, long long& nodes) const {
    if (depth <= 0) {
        if (settings.useQuiescence)
            return quiesce(board, side, alpha, beta, ply, MAX_QUIESCENCE_DEPTH, nodes); // counts itself
        ++nodes;
        return evaluate(board, side);
    }
    ++nodes;

    std::vector<Move> moves = board.getAllLegalMoves(side);
    if (moves.empty()) {
        // Checkmate (prefer mates that happen sooner: larger score for smaller ply) or stalemate.
        return board.isInCheck(side) ? -(MATE_SCORE - ply) : 0;
    }

    if (settings.useMoveOrdering) orderMoves(moves);

    int best = -INF;
    for (const Move& m : moves) {
        Board child = board.clone();
        child.executeMove(m);
        int score = -negamax(child, oppositeColor(side), depth - 1, -beta, -alpha, ply + 1, nodes);
        if (score > best) best = score;
        if (settings.useAlphaBeta) {
            if (best > alpha) alpha = best;
            if (alpha >= beta) break; // beta cut-off
        }
    }
    return best;
}

SearchResult AI::findBestMove(const Board& board, Color side) const {
    SearchResult result;
    result.depth = settings.depth < 1 ? 1 : settings.depth;

    std::vector<Move> moves = board.getAllLegalMoves(side);
    if (moves.empty()) return result; // checkmate or stalemate: nothing to play

    if (settings.useMoveOrdering) orderMoves(moves);

    int alpha = -INF;
    const int beta = INF;
    int best = -INF;
    long long nodes = 1;

    for (const Move& m : moves) {
        Board child = board.clone();
        child.executeMove(m);
        int score = -negamax(child, oppositeColor(side), result.depth - 1, -beta, -alpha, 1, nodes);
        if (score > best) {
            best = score;
            result.bestMove = m;
            result.found = true;
        }
        if (settings.useAlphaBeta && best > alpha) alpha = best;
    }

    result.score = best;
    result.nodes = nodes;
    return result;
}

std::string AI::describeScore(int score, Color side) {
    int whiteScore = (side == Color::WHITE) ? score : -score;
    std::ostringstream out;
    if (std::abs(whiteScore) > MATE_SCORE - 1000) {
        int plies = MATE_SCORE - std::abs(whiteScore);
        int fullMoves = (plies + 1) / 2;
        out << (whiteScore > 0 ? "White" : "Black") << " mates in " << fullMoves;
    } else {
        out << std::showpos << std::fixed << std::setprecision(2) << whiteScore / 100.0;
    }
    return out.str();
}
