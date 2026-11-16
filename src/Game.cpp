#include "Game.h"
#include "ChessExceptions.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* colorName(Color c) { return c == Color::WHITE ? "White" : "Black"; }

// A computer-vs-computer game is stopped after this many half-moves so it can never run forever.
constexpr std::size_t MAX_SELF_PLAY_PLIES = 400;

} // namespace

Game::Game(const GameConfig& cfg)
    : currentTurn(Color::WHITE), state(GameState::ONGOING), halfMoveClock(0),
      gameOver(false), config(cfg), ai(cfg.ai) {
    board.setupInitialPosition();
    positionHistory.push_back(board.positionKey(currentTurn));
}

void Game::switchTurn() {
    currentTurn = oppositeColor(currentTurn);
}

bool Game::isComputerTurn() const {
    PlayerType who = (currentTurn == Color::WHITE) ? config.white : config.black;
    return who == PlayerType::COMPUTER;
}

int Game::repetitionCount() const {
    if (positionHistory.empty()) return 0;
    const std::string& current = positionHistory.back();
    return static_cast<int>(std::count(positionHistory.begin(), positionHistory.end(), current));
}

GameState Game::evaluateState() {
    bool inCheck = board.isInCheck(currentTurn);
    std::vector<Move> moves = board.getAllLegalMoves(currentTurn);

    // Now that the position after the last move is known, add + or # to that move's notation.
    // (Moves restored from a save file keep their saved text untouched.)
    if (!moveHistory.empty() && moveHistory.back().notationOverride.empty()) {
        moveHistory.back().checkSuffix = inCheck ? (moves.empty() ? "#" : "+") : "";
    }

    if (moves.empty()) {
        state = inCheck ? GameState::CHECKMATE : GameState::STALEMATE;
    } else if (board.hasInsufficientMaterial()) {
        state = GameState::DRAW_INSUFFICIENT;
    } else if (halfMoveClock >= 100) { // 50 full moves with no pawn move / capture
        state = GameState::DRAW_50MOVE;
    } else if (repetitionCount() >= 3) {
        state = GameState::DRAW_THREEFOLD;
    } else if (inCheck) {
        state = GameState::CHECK;
    } else {
        state = GameState::ONGOING;
    }
    return state;
}

bool Game::announceIfOver(GameState st) {
    switch (st) {
        case GameState::CHECKMATE:
            std::cout << colorName(currentTurn) << " is checkmated. "
                      << colorName(oppositeColor(currentTurn)) << " wins!\n";
            gameOver = true;
            return true;
        case GameState::STALEMATE:
            std::cout << "Stalemate - the game is a draw.\n";
            gameOver = true;
            return true;
        case GameState::DRAW_INSUFFICIENT:
            std::cout << "Draw - neither side has enough material to checkmate.\n";
            gameOver = true;
            return true;
        case GameState::DRAW_50MOVE:
            std::cout << "Draw by the fifty-move rule.\n";
            gameOver = true;
            return true;
        case GameState::DRAW_THREEFOLD:
            std::cout << "Draw by threefold repetition.\n";
            gameOver = true;
            return true;
        case GameState::CHECK:
            std::cout << colorName(currentTurn) << " is in check!\n";
            return false;
        default:
            return false;
    }
}

void Game::handlePromotion(Move& move) {
    std::cout << "Pawn promotion! Choose piece - (Q)ueen, (R)ook, (B)ishop, k(N)ight: ";
    std::string choice;
    std::getline(std::cin, choice);
    char c = choice.empty() ? 'Q' : static_cast<char>(std::toupper(static_cast<unsigned char>(choice[0])));
    PieceType t = Piece::charToPieceType(c);
    if (t != PieceType::QUEEN && t != PieceType::ROOK && t != PieceType::BISHOP && t != PieceType::KNIGHT) {
        std::cout << "Unrecognized choice, defaulting to Queen.\n";
        t = PieceType::QUEEN;
    }
    move.promotionType = t;
}

void Game::printMoveHistory() const {
    if (moveHistory.empty()) {
        std::cout << "No moves have been played yet.\n";
        return;
    }
    std::cout << "Move history:\n";
    for (size_t i = 0; i < moveHistory.size(); ++i) {
        if (i % 2 == 0) std::cout << (i / 2 + 1) << ". ";
        std::cout << moveHistory[i].toAlgebraicNotation() << "  ";
        if (i % 2 == 1) std::cout << "\n";
    }
    std::cout << "\n";
}

void Game::printHelp() const {
    std::cout <<
        "Commands:\n"
        "  e2 e4          - move the piece on e2 to e4  (also: e2e4, E2 E4)\n"
        "  e7e8q          - a promotion move with the piece chosen up front (q, r, b or n)\n"
        "  hint           - ask the computer for a suggested move\n"
        "  depth <1-6>    - set how many plies ahead the computer searches\n"
        "  board          - show the board again\n"
        "  save <file>    - save the current game to a file\n"
        "  load <file>    - load a game from a file\n"
        "  history        - show move history\n"
        "  undo           - undo the last move (against the computer: your move and its reply)\n"
        "  resign         - resign the game\n"
        "  help           - show this message\n"
        "  quit           - exit the program\n";
}

void Game::printPlayers() const {
    auto describe = [&](PlayerType t) -> std::string {
        if (t == PlayerType::HUMAN) return "Human";
        const AISettings& s = ai.getSettings();
        std::ostringstream out;
        out << "Computer (depth " << s.depth
            << (s.useAlphaBeta ? ", alpha-beta" : ", plain minimax")
            << (s.useMoveOrdering ? ", capture-first ordering" : "")
            << (s.useQuiescence ? ", quiescence" : "") << ")";
        return out.str();
    };
    std::cout << "White: " << describe(config.white) << "\n"
              << "Black: " << describe(config.black) << "\n";
}

void Game::printBoard() const {
    board.print(false);
}

// ---------------------------------------------------------------------------
// Applying moves and undo
// ---------------------------------------------------------------------------

// The single place where a validated move is applied to the game state. Both
// the human path (makeMove) and the computer path (computerMove) end up here.
void Game::commitMove(Move move) {
    snapshots.push_back(Snapshot{board.clone(), halfMoveClock});
    try {
        board.executeMove(move);
    } catch (...) {
        snapshots.pop_back(); // keep the game exactly as it was
        throw;
    }

    moveHistory.push_back(move);
    if (move.movedType == PieceType::PAWN || move.isCapture()) {
        halfMoveClock = 0;
    } else {
        ++halfMoveClock;
    }
    switchTurn();
    positionHistory.push_back(board.positionKey(currentTurn));
}

void Game::makeMove(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> tokens;
    for (std::string t; iss >> t;) tokens.push_back(t);

    std::string fromStr, toStr;
    char promoChar = 0;
    if (tokens.size() == 1 && (tokens[0].size() == 4 || tokens[0].size() == 5)) {
        // compact form: "e2e4" or "e7e8q"
        fromStr = tokens[0].substr(0, 2);
        toStr = tokens[0].substr(2, 2);
        if (tokens[0].size() == 5) promoChar = tokens[0][4];
    } else if (tokens.size() == 2 || tokens.size() == 3) {
        fromStr = tokens[0];
        toStr = tokens[1];
        if (tokens.size() == 3) {
            if (tokens[2].size() != 1) throw InvalidMoveException("Promotion piece must be one letter: q, r, b or n");
            promoChar = tokens[2][0];
        }
    } else {
        throw InvalidMoveException("A move needs two squares, e.g. 'e2 e4' or 'e2e4'");
    }

    Position from = Position::fromAlgebraic(fromStr);
    Position to = Position::fromAlgebraic(toStr);
    if (!from.isValid() || !to.isValid()) {
        throw InvalidPositionException("'" + fromStr + "' or '" + toStr + "' is not a valid square (use a1-h8)");
    }

    Piece* p = board.getPiece(from);
    if (!p) throw NoPieceException("There is no piece on " + fromStr);
    if (p->getColor() != currentTurn) {
        throw InvalidMoveException(std::string("That piece does not belong to ") + colorName(currentTurn));
    }

    std::vector<Move> legalMoves = board.getLegalMoves(from);
    Move* chosen = nullptr;
    for (auto& m : legalMoves) {
        if (m.to == to) { chosen = &m; break; }
    }
    if (!chosen) {
        throw InvalidMoveException("Illegal move: " + fromStr + " to " + toStr);
    }

    if (promoChar != 0) {
        PieceType pt = Piece::charToPieceType(promoChar);
        if (pt != PieceType::QUEEN && pt != PieceType::ROOK && pt != PieceType::BISHOP && pt != PieceType::KNIGHT)
            throw InvalidMoveException("Promotion piece must be q, r, b or n");
        if (chosen->type != MoveType::PROMOTION)
            throw InvalidMoveException("That move is not a promotion");
        chosen->promotionType = pt;
    } else if (chosen->type == MoveType::PROMOTION) {
        handlePromotion(*chosen);
    }

    commitMove(*chosen);
}

void Game::undoOnePly() {
    Snapshot& s = snapshots.back();
    board = std::move(s.board);
    halfMoveClock = s.halfMoveClock;   // the fix: the clock is restored along with the board
    snapshots.pop_back();
    moveHistory.pop_back();
    positionHistory.pop_back();
    switchTurn();
    gameOver = false;
}

void Game::undoLastMove() {
    if (snapshots.empty()) {
        throw ChessException("Nothing to undo.");
    }
    undoOnePly();
    // Against the computer, also take back its reply so it is the human's turn again.
    if (isComputerTurn() && !snapshots.empty()) {
        undoOnePly();
    }
    std::cout << "Move undone.\n";
}

// ---------------------------------------------------------------------------
// Computer player
// ---------------------------------------------------------------------------

void Game::computerMove() {
    std::cout << colorName(currentTurn) << " (computer) is thinking...\n";
    auto start = std::chrono::steady_clock::now();
    SearchResult r = ai.findBestMove(board, currentTurn);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (!r.found) {
        // evaluateState() already ended the game when there are no legal moves, so this should not happen.
        throw ChessException("The computer has no legal move.");
    }

    Move m = r.bestMove;
    std::string text = m.toAlgebraicNotation();
    Color mover = currentTurn;
    commitMove(m);

    std::ostringstream timeText;
    timeText << std::fixed << std::setprecision(2) << seconds;
    std::cout << colorName(mover) << " (computer) plays " << text
              << "   [eval " << AI::describeScore(r.score, mover)
              << ", depth " << r.depth << ", " << r.nodes << " nodes, "
              << timeText.str() << "s]\n";
}

void Game::printHint() {
    SearchResult r = ai.findBestMove(board, currentTurn);
    if (!r.found) {
        std::cout << "There are no legal moves.\n";
        return;
    }
    std::cout << "Suggested move: " << r.bestMove.toAlgebraicNotation()
              << "   [eval " << AI::describeScore(r.score, currentTurn)
              << ", depth " << r.depth << ", " << r.nodes << " nodes]\n";
}

// ---------------------------------------------------------------------------
// Save / load
// ---------------------------------------------------------------------------

void Game::saveGame(const std::string& filename) const {
    std::ofstream out(filename);
    if (!out) throw FileIOException("Could not open '" + filename + "' for writing");

    out << "CHESSSAVE v1\n";
    out << "TURN " << (currentTurn == Color::WHITE ? "WHITE" : "BLACK") << "\n";
    out << "HALFMOVE " << halfMoveClock << "\n";
    board.serialize(out);
    out << "HISTORY " << moveHistory.size() << "\n";
    for (const auto& m : moveHistory) {
        out << m.toAlgebraicNotation() << "\n";
    }
    out << "END\n";

    if (!out) throw FileIOException("An error occurred while writing to '" + filename + "'");
}

void Game::loadGame(const std::string& filename) {
    // All-or-nothing: everything is parsed into local variables first. The Game's own
    // state is only touched at the very end, after the whole file has been validated.
    // (Bug fix: previously a bad file could leave the game with an empty board.)
    std::ifstream in(filename);
    if (!in) throw FileIOException("Could not open '" + filename + "' for reading");

    std::string line;
    auto readLine = [&](const char* errMsg) {
        if (!std::getline(in, line)) throw FileIOException(errMsg);
        line = rtrim(line);
    };

    readLine("Malformed save file: empty file");
    if (line.rfind("CHESSSAVE", 0) != 0) {
        throw FileIOException("'" + filename + "' is not a valid chess save file");
    }

    Color newTurn;
    readLine("Malformed save file: missing TURN line");
    {
        std::istringstream ss(line);
        std::string tag, turnStr;
        ss >> tag >> turnStr;
        if (tag != "TURN" || (turnStr != "WHITE" && turnStr != "BLACK"))
            throw FileIOException("Malformed save file: bad TURN line");
        newTurn = (turnStr == "WHITE") ? Color::WHITE : Color::BLACK;
    }

    int newClock = 0;
    readLine("Malformed save file: missing HALFMOVE line");
    {
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag >> newClock) || tag != "HALFMOVE" || newClock < 0)
            throw FileIOException("Malformed save file: bad HALFMOVE line");
    }

    Board newBoard;
    newBoard.deserialize(in); // throws FileIOException on any problem; newBoard is a temporary

    readLine("Malformed save file: missing HISTORY line");
    int count = 0;
    {
        std::istringstream ss(line);
        std::string tag;
        if (!(ss >> tag >> count) || tag != "HISTORY" || count < 0 || count > 100000)
            throw FileIOException("Malformed save file: bad HISTORY line");
    }

    std::vector<Move> newHistory;
    for (int i = 0; i < count; ++i) {
        readLine("Malformed save file: incomplete history");
        if (line.empty()) throw FileIOException("Malformed save file: empty history entry");
        Move m;
        m.notationOverride = line; // notation-only record; undo across a load is not supported
        newHistory.push_back(m);
    }

    readLine("Malformed save file: missing END line");
    if (line != "END") throw FileIOException("Malformed save file: missing END line");

    // A position where the side that is NOT to move is in check cannot arise in a real game.
    if (newBoard.isInCheck(oppositeColor(newTurn)))
        throw FileIOException("Illegal position: the side that just moved is in check");

    // ---- Commit: nothing below can fail, so the Game changes atomically.
    board = std::move(newBoard);
    currentTurn = newTurn;
    halfMoveClock = newClock;
    moveHistory = std::move(newHistory);
    snapshots.clear();                    // undo history does not survive a save/load round trip
    positionHistory.clear();
    positionHistory.push_back(board.positionKey(currentTurn)); // repetition tracking restarts here
    state = GameState::ONGOING;
    gameOver = false;
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void Game::run() {
    std::cout << "===================================\n";
    std::cout << "        CONSOLE CHESS ENGINE\n";
    std::cout << "===================================\n";
    printPlayers();
    std::cout << "\n";
    printHelp();
    std::cout << "\n";

    const bool computerOnly = config.white == PlayerType::COMPUTER && config.black == PlayerType::COMPUTER;
    bool refresh = true; // redraw + re-evaluate only when the position has actually changed

    while (!gameOver) {
        if (refresh) {
            printBoard();
            GameState st = evaluateState();
            if (announceIfOver(st)) break;
            refresh = false;
        }

        if (isComputerTurn()) {
            if (computerOnly && moveHistory.size() >= MAX_SELF_PLAY_PLIES) {
                std::cout << "Stopping the computer-vs-computer game after "
                          << MAX_SELF_PLAY_PLIES << " half-moves.\n";
                break;
            }
            try {
                computerMove();
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
                break;
            }
            refresh = true;
            continue;
        }

        std::cout << colorName(currentTurn) << " to move> ";
        std::string line;
        if (!std::getline(std::cin, line)) break;
        line = rtrim(line);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string first;
        iss >> first;
        std::string cmd = toLower(first);

        try {
            if (cmd == "quit" || cmd == "exit") {
                std::cout << "Goodbye!\n";
                break;
            } else if (cmd == "save") {
                std::string f;
                iss >> f;
                if (f.empty()) f = "chess_save.txt";
                saveGame(f);
                std::cout << "Game saved to " << f << "\n";
            } else if (cmd == "load") {
                std::string f;
                iss >> f;
                if (f.empty()) f = "chess_save.txt";
                loadGame(f);
                std::cout << "Game loaded from " << f << "\n";
                refresh = true;
            } else if (cmd == "history") {
                printMoveHistory();
            } else if (cmd == "undo") {
                undoLastMove();
                refresh = true;
            } else if (cmd == "resign") {
                std::cout << colorName(currentTurn) << " resigns. "
                          << colorName(oppositeColor(currentTurn)) << " wins!\n";
                gameOver = true;
            } else if (cmd == "help") {
                printHelp();
            } else if (cmd == "hint") {
                printHint();
            } else if (cmd == "board") {
                printBoard();
            } else if (cmd == "depth") {
                int d = 0;
                if (!(iss >> d) || d < 1 || d > 6) throw ChessException("Usage: depth <1-6>");
                ai.setDepth(d);
                std::cout << "Computer search depth is now " << d << " plies.\n";
            } else {
                makeMove(line);
                refresh = true;
            }
        } catch (const ChessException& e) {
            std::cout << "Error: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Unexpected error: " << e.what() << "\n";
        }

        std::cout << "\n";
    }
}
