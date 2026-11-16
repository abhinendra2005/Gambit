#pragma once
#include <string>
#include <vector>
#include "Board.h"
#include "Move.h"

// Tunable switches for the search. Every technique can be turned off on its own,
// which is handy both for testing (alpha-beta must give the same score as plain
// minimax) and for demonstrating how much each idea saves.
struct AISettings {
    int  depth = 3;               // search depth in plies (half-moves)
    bool useAlphaBeta = true;     // false = plain minimax, no pruning
    bool useMoveOrdering = true;  // try captures first (MVV-LVA) so pruning works better
    bool useQuiescence = true;    // at the horizon keep searching captures until quiet
};

struct SearchResult {
    bool found = false;     // false only when there are no legal moves
    Move bestMove;
    int score = 0;          // centipawns, from the point of view of the side that moves
    long long nodes = 0;    // positions visited - a direct measure of search effort
    int depth = 0;
};

// Chess AI: negamax search (minimax written with a sign flip), optional
// alpha-beta pruning, capture-first move ordering, quiescence search, and a
// material + piece-square-table evaluation.
//
// The AI never changes the Board it is given: it clones the position for every
// candidate move, using the same Board::executeMove the real game uses.
class AI {
public:
    static constexpr int MATE_SCORE = 100000;  // score of "checkmate right now"
    static constexpr int INF = 1000000;        // larger than any real score

    explicit AI(const AISettings& s = AISettings()) : settings(s) {}

    const AISettings& getSettings() const { return settings; }
    void setSettings(const AISettings& s) { settings = s; }
    void setDepth(int depth) { settings.depth = depth < 1 ? 1 : depth; }

    // Finds the best move for 'side' in the given position.
    SearchResult findBestMove(const Board& board, Color side) const;

    // Static evaluation in centipawns (100 = one pawn), positive = good for 'side'.
    static int evaluate(const Board& board, Color side);

    // Human-readable score from White's point of view, e.g. "+0.35" or "White mates in 2".
    // 'score' is from the point of view of 'side' (as returned in SearchResult).
    static std::string describeScore(int score, Color side);

private:
    int negamax(const Board& board, Color side, int depth, int alpha, int beta,
                int ply, long long& nodes) const;
    int quiesce(const Board& board, Color side, int alpha, int beta,
                int ply, int qdepth, long long& nodes) const;
    void orderMoves(std::vector<Move>& moves) const;

    AISettings settings;
};
