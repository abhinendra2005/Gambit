# Gambit: Console Chess with an AI Opponent

A complete, console-based chess game written in modern C++, built around a modular,
object-oriented class hierarchy. Play against a friend, play against the computer, or
watch the computer play itself. The computer opponent uses **minimax search (in its
negamax form) with alpha-beta pruning, capture-first move ordering and quiescence search**.

## Features

- **Full legal move validation**: every rule-legal move for every piece, including
  rejecting moves that would leave your own king in check (pins are handled
  automatically as a side-effect of this).
- **Check / checkmate / stalemate detection**
- **Castling**: both kingside and queenside, with all real rules enforced
  (king and rook unmoved and on their home squares, empty in-between squares, king not
  currently in check, king does not pass through or land on an attacked square).
- **En passant capture**
- **Pawn promotion**: choose Queen / Rook / Bishop / Knight when prompted, or in the
  move itself (`e7e8q`), including under-promotion.
- **Draw rules**: fifty-move rule, **threefold repetition** and **insufficient material**
- **Computer opponent**: selectable search depth, a `hint` command, and a
  computer-vs-computer mode (see [The AI](#the-ai))
- **Move history**: recorded and viewable in simple algebraic notation, with `+` for
  check and `#` for checkmate
- **Undo**: steps back through the game via board snapshots; restores the board **and**
  the half-move clock. Against the computer it takes back your move and its reply.
- **File-based save/load**: human-readable save format, strictly validated, and
  all-or-nothing (a bad file never corrupts the game in progress)
- **Exception-based error handling**: a small custom exception hierarchy
  (`ChessException` and friends) instead of error codes or crashes
- **Test suite**: perft counts against published numbers, one test per fixed bug,
  and AI correctness tests (`make test`)
- Built entirely on the STL (`vector`, `array`, `unique_ptr`, streams), with
  no manual `new`/`delete` anywhere in the codebase

## Design / OOP principles

| Principle | Where it shows up |
|---|---|
| **Abstraction** | `Piece` exposes a piece-agnostic interface (`getPseudoLegalMoves`, `getAttackSquares`, `getSymbol`, ...). Callers (`Board`, `Game`, `AI`) never care what concrete piece they're holding. `Game::makeMove("e2 e4")` hides everything underneath. |
| **Encapsulation** | `Piece::color` / `moved` are `protected` and only reachable through accessors. `Board`'s grid is `private`; all access goes through validated methods that throw on bad input. `Game`'s state is private. |
| **Inheritance** | `Pawn`, `Knight`, `Bishop`, `Rook`, `Queen`, `King` all derive from the abstract `Piece` base class. All exceptions derive from `std::runtime_error`. |
| **Runtime polymorphism** | `Board` stores `std::unique_ptr<Piece>` and calls virtual methods (`getPseudoLegalMoves`, `getAttackSquares`, `clone`, ...). The correct derived-class behavior is resolved at runtime for every square on the board. |
| **RAII / ownership** | Every piece has exactly one owner (`unique_ptr`), so a capture is just an overwrite and cleanup is automatic. `Board` follows the Rule of Five: deep copy through `clone()`, defaulted `noexcept` moves. |

### Class hierarchy

```
Piece (abstract)
 ├── Pawn
 ├── Knight
 ├── Bishop
 ├── Rook
 ├── Queen
 └── King

Position     - one square (row, col) with algebraic conversion
Move         - a single ply: from/to squares + metadata (capture, castle, promotion, check/mate suffix)
Board        - owns the 8x8 grid, all legality/check logic, draw detection helpers, serialization
AI           - the search: negamax, alpha-beta, move ordering, quiescence, evaluation
                (configured by AISettings, returns a SearchResult)
Game         - turn management, move history, undo, repetition tracking, save/load,
                the computer player and the console I/O loop (configured by GameConfig)
ChessException hierarchy - InvalidMoveException, InvalidPositionException,
                            NoPieceException, FileIOException
```

### Key architectural decisions

- **`getPseudoLegalMoves` vs. `getAttackSquares`.** Each piece implements two
  move-related methods: one for *moves it could actually make* (respecting
  occupancy/capture rules) and one for *squares it controls*, used purely for
  check/attacked-square detection. The distinction matters for pawns, whose
  diagonal "attack" doesn't require an enemy piece to be sitting there for the
  square to count as attacked (for example in castling-through-check checks).
- **Check-safety is a filter, not special-cased per piece.** `Board::getLegalMoves`
  takes every pseudo-legal move, simulates it on a cloned board, and discards
  it if the mover's own king ends up in check. This single mechanism handles
  pins, discovered checks, and "can't move into check" uniformly for every
  piece type, including the king itself.
- **`Board` is deep-copyable** (`clone()` / copy constructor) via each piece's
  virtual `clone()` method (the "virtual copy constructor" idiom). That makes
  move simulation, undo snapshots, the AI's search and save/load all simple to
  implement correctly with `unique_ptr`-owned pieces.
- **One way to apply a move.** Humans (`makeMove`) and the computer (`computerMove`) both
  finish in `Game::commitMove`, which updates the snapshot, history, half-move clock, turn
  and repetition record together, and rolls back if anything throws.
- **Validate first, then change anything.** `Board::executeMove` checks the move before
  touching the board, and `loadGame` parses and validates the whole file into temporaries
  before replacing the game state, so failures leave the game exactly as it was.
- **The AI is a layer on top of the engine.** It only uses `Board`'s public API
  (`getAllLegalMoves`, `clone`, `executeMove`, `isInCheck`) and never changes the board it
  is given; no move-generation code had to change to add it.

## The AI

### How it works

1. **Negamax / minimax.** Each side picks the move that is best for it assuming the
   opponent replies as well as possible. Negamax writes this with a single function: a
   position's score for the side to move is minus the opponent's best score.
2. **Alpha-beta pruning.** `alpha` is the best score the side to move is already guaranteed,
   `beta` is the best the opponent will allow. When `alpha >= beta` the rest of the branch
   cannot change the result, so it is skipped. The score is identical to plain minimax.
3. **Capture-first move ordering (MVV-LVA).** Pruning only works well if good moves are tried
   first. Captures are sorted by Most Valuable Victim / Least Valuable Attacker (pawn takes
   queen before queen takes pawn); promotions go first of all.
4. **Quiescence search.** At the depth limit the search keeps playing captures until the
   position is quiet, so it never stops in the middle of an exchange (the horizon effect).
   When the side to move is in check it searches every legal reply, which also lets it see
   checkmate at the horizon.
5. **Evaluation.** Material from `Piece::getValue()` (x100, so a pawn is 100 centipawns)
   plus piece-square tables that reward good squares. Checkmate scores `MATE_SCORE - ply`, so
   faster mates are preferred. Dead draws score 0.

Every technique can be switched off on its own (`--no-pruning`, `--no-ordering`,
`--no-quiescence`), which is how the test suite proves alpha-beta returns the same score as
plain minimax and how the savings below were measured.

### Measured effect

Positions visited at the same depth (from `make test`; all variants return the same score):

| Position, depth | Plain minimax | Alpha-beta | + capture-first ordering |
|---|---|---|---|
| Start position, 3 | 17,127 | 1,015 | 1,009 |
| "Kiwipete", 2 (no quiescence) | 2,088 | 357 | 132 |
| "Position 3", 3 | 43,444 | 1,648 | 435 |
| "Kiwipete", 2 (with quiescence) | n/a | 78,912 | 2,041 |

### Strength and speed

Depth is the number of plies searched ahead. Times were measured on the development
machine (one core); yours will differ.

| Depth | Typical thinking time | Menu level |
|---|---|---|
| 2 | instant | Easy |
| 3 | about 0.02 s | Medium (default) |
| 4 | about 0.1 s | Hard |
| 5 | about 1-7 s | `--depth 5` |
| 6 | about 8-11 s from the opening, longer in tactical middlegames | `--depth 6` |

## Building

Requires a C++ compiler with C++14 or newer support (g++ or clang++). Tested with g++ 13 on
Linux, warning-free with `-Wall -Wextra -pedantic`, and clean under AddressSanitizer and UBSan.

```bash
make          # builds ./chess
make test     # builds and runs the test suite
make clean    # removes build artifacts
```

Or compile directly:

```bash
g++ -O2 -Iinclude -o chess src/*.cpp
```

On Windows (MinGW-w64), list the files explicitly:

```bash
g++ -O2 -Iinclude src/AI.cpp src/Bishop.cpp src/Board.cpp src/Game.cpp src/King.cpp src/Knight.cpp src/Pawn.cpp src/Piece.cpp src/Queen.cpp src/Rook.cpp src/main.cpp -o chess
```

## Running

```bash
./chess                          # interactive menu
./chess --two-player             # two humans share the keyboard
./chess --vs-computer            # you play White against the computer
./chess --vs-computer --play-as black --depth 4
./chess --computer-vs-computer   # watch the computer play itself
```

Computer options: `--depth N` (1-6, default 3), `--no-pruning` (plain minimax; this also
switches quiescence off, because without pruning it is impractically slow), `--no-ordering`,
`--no-quiescence`. `./chess --help` lists everything.

### In-game commands

| Command | Effect |
|---|---|
| `e2 e4`, `e2e4` or `E2 E4` | Move the piece on e2 to e4 |
| `e7e8q` | A promotion move with the piece chosen up front (q, r, b or n) |
| `hint` | The computer suggests a move for the side to move |
| `depth <1-6>` | Change how many plies ahead the computer searches |
| `board` | Show the board again |
| `save <filename>` | Save the current game |
| `load <filename>` | Load a game from disk |
| `history` | Show move history |
| `undo` | Undo the last move (against the computer: your move and its reply) |
| `resign` | Resign the game |
| `help` | Show the command list |
| `quit` / `exit` | Exit the program |

On a promotion move without a piece letter, you'll be prompted to choose Queen / Rook / Bishop / Knight.

## Project layout

```
chess_game/
├── include/            # 14 headers
│   ├── Position.h
│   ├── ChessExceptions.h
│   ├── Piece.h
│   ├── MoveGenUtils.h  # shared sliding/offset move-generation helpers
│   ├── Pawn.h / Knight.h / Bishop.h / Rook.h / Queen.h / King.h
│   ├── Move.h
│   ├── Board.h
│   ├── AI.h            # the computer player's search
│   └── Game.h
├── src/                # 11 implementation files
│   ├── Piece.cpp
│   ├── Pawn.cpp / Knight.cpp / Bishop.cpp / Rook.cpp / Queen.cpp / King.cpp
│   ├── Board.cpp
│   ├── AI.cpp
│   ├── Game.cpp
│   └── main.cpp
├── tests/
│   └── test_engine.cpp # perft, bug-fix and AI tests (114 checks)
├── Makefile
└── README.md
```

## Testing

`make test` runs `tests/test_engine.cpp`, which needs no test framework:

- **Perft**: counts every reachable position to a fixed depth and compares with published
  numbers for three standard positions (start position to depth 4: 197,281; "Kiwipete" to
  depth 3: 97,862; "Position 3" to depth 4: 43,238). A match means move generation is exact.
- **One test per fixed bug**: undo restores the clock, a failed load changes nothing, uppercase
  and compact input, threefold repetition, insufficient material, `+` / `#` notation, castling
  and `executeMove` validation.
- **AI tests**: finds mate in 1 and mate in 2 (for both colours), does not hang a queen, takes a
  free queen, alpha-beta equals plain minimax, pruning and ordering cut the node count, the
  search never modifies the caller's board, and a 120-move self-play game contains only legal moves.

## Extending it

The architecture is deliberately open for extension:

- **New piece type**: derive from `Piece`, implement the pure-virtual methods, add one line
  to `Piece::createPiece`'s switch. No other file needs to change.
- **Stronger AI**: the search is self-contained in `AI`. Natural next steps are make/unmake
  instead of cloning the board for every candidate move, a transposition table (Zobrist
  hashing), iterative deepening, killer-move and history heuristics, and bitboards.
- **Alternate front end** (GUI, network play): `Game`'s public interface (`makeMove`,
  `undoLastMove`, `saveGame`, `loadGame`, `printBoard`) is independent of the console loop in
  `run()`, so a different front end can drive the same engine. The promotion prompt currently
  lives in `Game` and would need to move out.

## Known limitations

- The AI searches a fixed depth: no iterative deepening, no transposition table, no opening book.
  It does not know about repetition or the fifty-move rule while searching, and it considers
  only queen promotions.
- Speed: every candidate move clones the board (about 32 small heap allocations).
- The fifty-move draw is automatic at 50 moves (in FIDE rules it is claimable, and automatic at 75).
- `undo` snapshots and the repetition history are in-memory only and do not persist across a
  save/load round trip (a reloaded game's history is notation-only, for display).
- Algebraic notation output is simplified (`e2-e4`, `e4xd5`, `O-O`, `e7-e8=Q`, with `+` / `#`)
  rather than full disambiguated SAN.
