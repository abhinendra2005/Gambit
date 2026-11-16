#include "Game.h"
#include <iostream>
#include <string>

namespace {

void printUsage(const char* prog) {
    std::cout <<
        "Usage: " << prog << " [options]\n"
        "\n"
        "With no options an interactive menu is shown.\n"
        "\n"
        "Game mode:\n"
        "  --two-player             two humans share the keyboard\n"
        "  --vs-computer            play against the computer\n"
        "  --play-as white|black    your colour with --vs-computer (default: white)\n"
        "  --computer-vs-computer   watch the computer play itself\n"
        "\n"
        "Computer settings:\n"
        "  --depth N                search depth in plies, 1-6 (default: 3)\n"
        "  --no-pruning             plain minimax instead of alpha-beta (much slower; this also\n"
        "                           switches quiescence off, which is impractical without pruning)\n"
        "  --no-ordering            do not try captures first\n"
        "  --no-quiescence          stop searching exactly at the depth limit\n"
        "\n"
        "  -h, --help               show this message\n";
}

// Reads a number in [lo, hi] from the user; returns 'def' on empty / bad input / EOF.
int askInt(const std::string& prompt, int lo, int hi, int def) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) return def;
    try {
        int v = std::stoi(line);
        if (v >= lo && v <= hi) return v;
    } catch (...) {
    }
    return def;
}

enum class Mode { UNSET, TWO_PLAYER, VS_COMPUTER, COMPUTER_VS_COMPUTER };

} // namespace

int main(int argc, char* argv[]) {
    try {
        GameConfig cfg;
        Mode mode = Mode::UNSET;
        Color humanColor = Color::WHITE;
        bool depthGiven = false;

        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") {
                printUsage(argv[0]);
                return 0;
            } else if (a == "--two-player") {
                mode = Mode::TWO_PLAYER;
            } else if (a == "--vs-computer") {
                mode = Mode::VS_COMPUTER;
            } else if (a == "--computer-vs-computer") {
                mode = Mode::COMPUTER_VS_COMPUTER;
            } else if (a == "--play-as" && i + 1 < argc) {
                std::string c = argv[++i];
                if (c == "white") humanColor = Color::WHITE;
                else if (c == "black") humanColor = Color::BLACK;
                else { std::cerr << "--play-as expects 'white' or 'black'\n"; return 2; }
            } else if (a == "--depth" && i + 1 < argc) {
                int d = 0;
                try { d = std::stoi(argv[++i]); } catch (...) { d = 0; }
                if (d < 1 || d > 6) { std::cerr << "--depth expects a number from 1 to 6\n"; return 2; }
                cfg.ai.depth = d;
                depthGiven = true;
            } else if (a == "--no-pruning") {
                cfg.ai.useAlphaBeta = false;
            } else if (a == "--no-ordering") {
                cfg.ai.useMoveOrdering = false;
            } else if (a == "--no-quiescence") {
                cfg.ai.useQuiescence = false;
            } else {
                std::cerr << "Unknown or incomplete option: " << a << "\n\n";
                printUsage(argv[0]);
                return 2;
            }
        }

        // Quiescence search without alpha-beta has no cut-offs and explodes in tactical
        // positions, so textbook "plain minimax" means a fixed-depth search with neither.
        if (!cfg.ai.useAlphaBeta) cfg.ai.useQuiescence = false;

        if (mode == Mode::UNSET) {
            std::cout << "===================================\n"
                         "        CONSOLE CHESS ENGINE\n"
                         "===================================\n"
                         "  1) Two players\n"
                         "  2) Play against the computer\n"
                         "  3) Watch computer vs computer\n";
            int choice = askInt("Choose a mode [1-3, default 1]: ", 1, 3, 1);
            mode = (choice == 2) ? Mode::VS_COMPUTER
                 : (choice == 3) ? Mode::COMPUTER_VS_COMPUTER
                                 : Mode::TWO_PLAYER;

            if (mode == Mode::VS_COMPUTER) {
                int c = askInt("Play as 1) White or 2) Black [default 1]: ", 1, 2, 1);
                humanColor = (c == 2) ? Color::BLACK : Color::WHITE;
            }
            if (mode != Mode::TWO_PLAYER && !depthGiven) {
                int level = askInt("Difficulty 1) Easy  2) Medium  3) Hard [default 2]: ", 1, 3, 2);
                cfg.ai.depth = level + 1; // Easy = 2 plies, Medium = 3, Hard = 4
            }
        }

        switch (mode) {
            case Mode::VS_COMPUTER:
                cfg.white = (humanColor == Color::WHITE) ? PlayerType::HUMAN : PlayerType::COMPUTER;
                cfg.black = (humanColor == Color::BLACK) ? PlayerType::HUMAN : PlayerType::COMPUTER;
                break;
            case Mode::COMPUTER_VS_COMPUTER:
                cfg.white = PlayerType::COMPUTER;
                cfg.black = PlayerType::COMPUTER;
                break;
            default:
                break; // both humans
        }

        Game game(cfg);
        game.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown error occurred." << std::endl;
        return 1;
    }
    return 0;
}
