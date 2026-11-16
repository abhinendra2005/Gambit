#pragma once
#include <vector>
#include <string>
#include "Board.h"
#include "Move.h"
#include "AI.h"

enum class GameState {
    ONGOING, CHECK, CHECKMATE, STALEMATE,
    DRAW_50MOVE, DRAW_THREEFOLD, DRAW_INSUFFICIENT
};

enum class PlayerType { HUMAN, COMPUTER };

// Who plays each colour, and how strong the computer is.
struct GameConfig {
    PlayerType white = PlayerType::HUMAN;
    PlayerType black = PlayerType::HUMAN;
    AISettings ai;
};

// Game is the engine's orchestration layer: it owns a Board, tracks whose
// turn it is, records move history, offers undo via board snapshots, drives the
// computer player, and handles the console I/O loop, exception handling and save/load.
class Game {
private:
    // Everything undo must restore. (Bug fix: the half-move clock used to be
    // forgotten, which could trigger the fifty-move draw too early after an undo.)
    struct Snapshot {
        Board board;
        int halfMoveClock;
    };

    Board board;
    Color currentTurn;
    std::vector<Move> moveHistory;
    std::vector<Snapshot> snapshots;           // state before each move, enables undo
    std::vector<std::string> positionHistory;  // position key after every ply (threefold repetition)
    GameState state;
    int halfMoveClock;                         // half-moves since last pawn move/capture (50-move rule)
    bool gameOver;
    GameConfig config;
    AI ai;

    void switchTurn();
    void commitMove(Move move);                // the single place a move is applied to the game
    void handlePromotion(Move& move);
    void computerMove();
    bool isComputerTurn() const;
    bool announceIfOver(GameState st);         // prints the result; true if the game has ended
    int repetitionCount() const;
    void undoOnePly();
    void printMoveHistory() const;
    void printHelp() const;
    void printHint();
    void printPlayers() const;

public:
    explicit Game(const GameConfig& cfg = GameConfig());

    void run();                                  // main interactive loop
    void makeMove(const std::string& input);      // "e2 e4", "e2e4" or "e7e8q"
    void undoLastMove();                          // takes back the computer's reply too when playing it
    GameState evaluateState();                    // also adds + / # to the last move's notation
    void saveGame(const std::string& filename) const;
    void loadGame(const std::string& filename);   // all-or-nothing: a failed load changes nothing
    void printBoard() const;

    bool isGameOver() const { return gameOver; }
    Color getCurrentTurn() const { return currentTurn; }
    int getHalfMoveClock() const { return halfMoveClock; }
    const Board& getBoard() const { return board; }
    const std::vector<Move>& getMoveHistory() const { return moveHistory; }
};
