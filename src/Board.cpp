#include "Board.h"
#include "ChessExceptions.h"
#include <iostream>
#include <sstream>
#include <cctype>
#include <cmath>

namespace {
// Save files written on Windows end lines with "\r\n"; strip trailing whitespace
// so the same file loads everywhere.
std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}
} // namespace

Board::Board() {
    // grid[][] default-initializes every unique_ptr<Piece> to nullptr (empty square)
}

void Board::deepCopyFrom(const Board& other) {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (other.grid[r][c]) {
                grid[r][c] = other.grid[r][c]->clone();
            } else {
                grid[r][c].reset();
            }
        }
    }
    enPassantTarget = other.enPassantTarget;
}

Board::Board(const Board& other) {
    deepCopyFrom(other);
}

Board& Board::operator=(const Board& other) {
    if (this != &other) {
        deepCopyFrom(other);
    }
    return *this;
}

void Board::clearBoard() {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            grid[r][c].reset();
        }
    }
    enPassantTarget = Position();
}

void Board::setupInitialPosition() {
    clearBoard();

    for (int c = 0; c < 8; ++c) {
        grid[1][c] = Piece::createPiece(PieceType::PAWN, Color::WHITE);
        grid[6][c] = Piece::createPiece(PieceType::PAWN, Color::BLACK);
    }

    static const PieceType backRank[8] = {
        PieceType::ROOK, PieceType::KNIGHT, PieceType::BISHOP, PieceType::QUEEN,
        PieceType::KING, PieceType::BISHOP, PieceType::KNIGHT, PieceType::ROOK
    };
    for (int c = 0; c < 8; ++c) {
        grid[0][c] = Piece::createPiece(backRank[c], Color::WHITE);
        grid[7][c] = Piece::createPiece(backRank[c], Color::BLACK);
    }
}

Piece* Board::getPiece(const Position& pos) const {
    if (!pos.isValid()) return nullptr;
    return grid[pos.row][pos.col].get();
}

void Board::setPiece(const Position& pos, std::unique_ptr<Piece> piece) {
    if (!pos.isValid()) throw InvalidPositionException("Cannot place a piece on an off-board square");
    grid[pos.row][pos.col] = std::move(piece);
}

std::unique_ptr<Piece> Board::removePiece(const Position& pos) {
    if (!pos.isValid()) throw InvalidPositionException("Cannot remove a piece from an off-board square");
    return std::move(grid[pos.row][pos.col]);
}

bool Board::isEmpty(const Position& pos) const {
    if (!pos.isValid()) return false;
    return grid[pos.row][pos.col] == nullptr;
}

bool Board::isEnemyAt(const Position& pos, Color color) const {
    Piece* p = getPiece(pos);
    return p != nullptr && p->getColor() != color;
}

bool Board::isAllyAt(const Position& pos, Color color) const {
    Piece* p = getPiece(pos);
    return p != nullptr && p->getColor() == color;
}

Position Board::findKing(Color color) const {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            if (p && p->getType() == PieceType::KING && p->getColor() == color) {
                return Position(r, c);
            }
        }
    }
    return Position(); // invalid - should not happen in a well-formed game
}

bool Board::isSquareAttacked(const Position& pos, Color byColor) const {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            if (p && p->getColor() == byColor) {
                std::vector<Position> attacks = p->getAttackSquares(*this, Position(r, c));
                for (const auto& a : attacks) {
                    if (a == pos) return true;
                }
            }
        }
    }
    return false;
}

bool Board::isInCheck(Color color) const {
    Position kingPos = findKing(color);
    if (!kingPos.isValid()) return false;
    return isSquareAttacked(kingPos, oppositeColor(color));
}

std::vector<Move> Board::getLegalMoves(const Position& from, bool capturesOnly) const {
    std::vector<Move> legal;

    Piece* piece = getPiece(from);
    if (!piece) throw NoPieceException("No piece at " + from.toAlgebraic());

    Color color = piece->getColor();
    std::vector<Position> pseudoTargets = piece->getPseudoLegalMoves(*this, from);

    for (const auto& target : pseudoTargets) {
        Move m;
        m.from = from;
        m.to = target;
        m.movedType = piece->getType();
        m.movedColor = color;

        Piece* destPiece = getPiece(target);
        if (destPiece) m.capturedType = destPiece->getType();

        if (piece->getType() == PieceType::PAWN) {
            if (std::abs(target.row - from.row) == 2) {
                m.type = MoveType::DOUBLE_PAWN;
            } else if (target.col != from.col && !destPiece) {
                // Diagonal move onto an empty square can only be en passant
                m.type = MoveType::EN_PASSANT;
                m.capturedType = PieceType::PAWN;
            }
            if (target.row == 0 || target.row == 7) {
                m.type = MoveType::PROMOTION;
                m.promotionType = PieceType::QUEEN; // default; caller may override before executing
            }
        }

        // Quiescence search only wants "loud" moves: skip quiet moves BEFORE the
        // expensive clone-and-test below.
        if (capturesOnly && !m.isCapture() && m.type != MoveType::PROMOTION) continue;

        // Reject the move if it would leave the mover's own king in check.
        Board temp = this->clone();
        temp.executeMove(m);
        if (!temp.isInCheck(color)) {
            legal.push_back(m);
        }
    }

    // Castling: only offered when the king is on its home square and has not
    // moved, is not currently in check, and does not pass through or land on an
    // attacked square. (Hardening: the king's square and the rook's colour are now
    // verified too, instead of trusting the 'moved' flags alone.)
    const int homeRow = (color == Color::WHITE) ? 0 : 7;
    if (!capturesOnly && piece->getType() == PieceType::KING && !piece->hasMoved() &&
        from.row == homeRow && from.col == 4 && !isInCheck(color)) {
        int row = from.row;
        Color enemy = oppositeColor(color);

        Piece* kingsideRook = getPiece(Position(row, 7));
        if (kingsideRook && kingsideRook->getType() == PieceType::ROOK &&
            kingsideRook->getColor() == color && !kingsideRook->hasMoved() &&
            isEmpty(Position(row, 5)) && isEmpty(Position(row, 6)) &&
            !isSquareAttacked(Position(row, 5), enemy) &&
            !isSquareAttacked(Position(row, 6), enemy)) {
            Move m;
            m.from = from;
            m.to = Position(row, 6);
            m.movedType = PieceType::KING;
            m.movedColor = color;
            m.type = MoveType::CASTLE_KINGSIDE;
            legal.push_back(m);
        }

        Piece* queensideRook = getPiece(Position(row, 0));
        if (queensideRook && queensideRook->getType() == PieceType::ROOK &&
            queensideRook->getColor() == color && !queensideRook->hasMoved() &&
            isEmpty(Position(row, 1)) && isEmpty(Position(row, 2)) && isEmpty(Position(row, 3)) &&
            !isSquareAttacked(Position(row, 2), enemy) &&
            !isSquareAttacked(Position(row, 3), enemy)) {
            Move m;
            m.from = from;
            m.to = Position(row, 2);
            m.movedType = PieceType::KING;
            m.movedColor = color;
            m.type = MoveType::CASTLE_QUEENSIDE;
            legal.push_back(m);
        }
    }

    return legal;
}

std::vector<Move> Board::getAllLegalMoves(Color color, bool capturesOnly) const {
    std::vector<Move> all;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            if (p && p->getColor() == color) {
                std::vector<Move> pieceMoves = getLegalMoves(Position(r, c), capturesOnly);
                all.insert(all.end(), pieceMoves.begin(), pieceMoves.end());
            }
        }
    }
    return all;
}

void Board::executeMove(const Move& move) {
    // ---- 1. Validate everything first, so a bad Move cannot leave a half-applied board.
    if (!move.from.isValid() || !move.to.isValid())
        throw InvalidPositionException("Move has an off-board square");
    if (!getPiece(move.from))
        throw InvalidMoveException("No piece to move from " + move.from.toAlgebraic());

    if (move.type == MoveType::PROMOTION) {
        PieceType t = move.promotionType;
        if (t != PieceType::QUEEN && t != PieceType::ROOK &&
            t != PieceType::BISHOP && t != PieceType::KNIGHT)
            throw InvalidMoveException("A pawn can only promote to a queen, rook, bishop or knight");
    }
    if (move.type == MoveType::CASTLE_KINGSIDE || move.type == MoveType::CASTLE_QUEENSIDE) {
        int rookCol = (move.type == MoveType::CASTLE_KINGSIDE) ? 7 : 0;
        Piece* rook = getPiece(Position(move.from.row, rookCol));
        if (!rook || rook->getType() != PieceType::ROOK)
            throw InvalidMoveException("Castling needs a rook on its home square");
    }

    // ---- 2. Apply the move.
    std::unique_ptr<Piece> movingPiece = removePiece(move.from);

    if (move.type == MoveType::EN_PASSANT) {
        Position capturedPawnPos(move.from.row, move.to.col);
        removePiece(capturedPawnPos);
    }

    movingPiece->setMoved(true);

    if (move.type == MoveType::PROMOTION) {
        auto promoted = Piece::createPiece(move.promotionType, movingPiece->getColor());
        promoted->setMoved(true);
        setPiece(move.to, std::move(promoted));
    } else {
        setPiece(move.to, std::move(movingPiece));
    }

    if (move.type == MoveType::CASTLE_KINGSIDE) {
        auto rook = removePiece(Position(move.from.row, 7));
        rook->setMoved(true);
        setPiece(Position(move.from.row, 5), std::move(rook));
    } else if (move.type == MoveType::CASTLE_QUEENSIDE) {
        auto rook = removePiece(Position(move.from.row, 0));
        rook->setMoved(true);
        setPiece(Position(move.from.row, 3), std::move(rook));
    }

    if (move.type == MoveType::DOUBLE_PAWN) {
        setEnPassantTarget(Position((move.from.row + move.to.row) / 2, move.from.col));
    } else {
        clearEnPassantTarget();
    }
}

bool Board::hasInsufficientMaterial() const {
    int knights = 0, lightBishops = 0, darkBishops = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            if (!p) continue;
            switch (p->getType()) {
                case PieceType::KING:   break;
                case PieceType::KNIGHT: ++knights; break;
                case PieceType::BISHOP: ((r + c) % 2 == 0 ? darkBishops : lightBishops)++; break;
                default: return false; // a pawn, rook or queen can always (eventually) mate
            }
        }
    }
    int minors = knights + lightBishops + darkBishops;
    if (minors <= 1) return true;                                // K vs K, K+B vs K, K+N vs K
    if (knights == 0 && (lightBishops == 0 || darkBishops == 0))  // only same-coloured bishops
        return true;
    return false;
}

std::string Board::positionKey(Color sideToMove) const {
    std::string key;
    key.reserve(80);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            key += p ? p->getSymbol() : '.';
        }
    }
    key += (sideToMove == Color::WHITE) ? 'w' : 'b';

    // Castling rights: king and that rook both still unmoved on their home squares.
    auto canCastle = [&](Color col, int rookCol) {
        int row = (col == Color::WHITE) ? 0 : 7;
        Piece* k = getPiece(Position(row, 4));
        Piece* rk = getPiece(Position(row, rookCol));
        return k && k->getType() == PieceType::KING && k->getColor() == col && !k->hasMoved() &&
               rk && rk->getType() == PieceType::ROOK && rk->getColor() == col && !rk->hasMoved();
    };
    key += canCastle(Color::WHITE, 7) ? 'K' : '-';
    key += canCastle(Color::WHITE, 0) ? 'Q' : '-';
    key += canCastle(Color::BLACK, 7) ? 'k' : '-';
    key += canCastle(Color::BLACK, 0) ? 'q' : '-';

    // En passant only matters if a pawn of the side to move could actually capture it.
    if (enPassantTarget.isValid()) {
        int dir = (sideToMove == Color::WHITE) ? 1 : -1;
        bool capturePossible = false;
        for (int dc : {-1, 1}) {
            Piece* p = getPiece(Position(enPassantTarget.row - dir, enPassantTarget.col + dc));
            if (p && p->getType() == PieceType::PAWN && p->getColor() == sideToMove) capturePossible = true;
        }
        if (capturePossible) key += enPassantTarget.toAlgebraic();
    }
    return key;
}

Board Board::clone() const {
    Board copy;
    copy.deepCopyFrom(*this);
    return copy;
}

void Board::print(bool useUnicode) const {
    std::cout << "\n   +------------------------+\n";
    for (int r = 7; r >= 0; --r) {
        std::cout << " " << (r + 1) << " | ";
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            if (p) {
                std::cout << (useUnicode ? p->getUnicodeSymbol() : std::string(1, p->getSymbol())) << ' ';
            } else {
                std::cout << ". ";
            }
        }
        std::cout << "|\n";
    }
    std::cout << "   +------------------------+\n";
    std::cout << "     a b c d e f g h\n\n";
}

void Board::serialize(std::ostream& out) const {
    out << "BOARD\n";
    for (int r = 7; r >= 0; --r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            out << (p ? p->getSymbol() : '.');
        }
        out << '\n';
    }
    out << "MOVED\n";
    for (int r = 7; r >= 0; --r) {
        for (int c = 0; c < 8; ++c) {
            Piece* p = grid[r][c].get();
            out << ((p && p->hasMoved()) ? '1' : '0');
        }
        out << '\n';
    }
    out << "ENPASSANT " << (enPassantTarget.isValid() ? enPassantTarget.toAlgebraic() : "NONE") << "\n";
}

void Board::deserialize(std::istream& in) {
    // Build the result in a local Board; *this is only replaced at the very end
    // (strong exception guarantee: if anything below throws, *this is untouched).
    Board parsed;
    std::string line;

    auto readLine = [&](const char* errMsg) {
        if (!std::getline(in, line)) throw FileIOException(errMsg);
        line = rtrim(line);
    };

    readLine("Malformed save file: expected BOARD section");
    if (line != "BOARD") throw FileIOException("Malformed save file: expected BOARD section");

    std::vector<std::string> boardLines;
    for (int i = 0; i < 8; ++i) {
        readLine("Malformed save file: bad board row");
        if (line.size() < 8) throw FileIOException("Malformed save file: bad board row");
        boardLines.push_back(line);
    }

    readLine("Malformed save file: expected MOVED section");
    if (line != "MOVED") throw FileIOException("Malformed save file: expected MOVED section");

    std::vector<std::string> movedLines;
    for (int i = 0; i < 8; ++i) {
        readLine("Malformed save file: bad moved-flags row");
        if (line.size() < 8) throw FileIOException("Malformed save file: bad moved-flags row");
        movedLines.push_back(line);
    }

    readLine("Malformed save file: expected ENPASSANT line");
    if (line.rfind("ENPASSANT", 0) != 0)
        throw FileIOException("Malformed save file: expected ENPASSANT line");
    {
        std::istringstream epss(line);
        std::string tag, epStr;
        epss >> tag >> epStr;
        if (epStr == "NONE") {
            parsed.enPassantTarget = Position();
        } else {
            Position ep = Position::fromAlgebraic(epStr);
            if (!ep.isValid() || (ep.row != 2 && ep.row != 5))
                throw FileIOException("Malformed save file: bad en passant square");
            parsed.enPassantTarget = ep;
        }
    }

    int whiteKings = 0, blackKings = 0;
    for (int i = 0; i < 8; ++i) {
        int r = 7 - i; // boardLines[0] holds rank 8
        for (int c = 0; c < 8; ++c) {
            char ch = boardLines[i][c];
            if (ch == '.') continue;
            PieceType type = Piece::charToPieceType(ch);
            if (type == PieceType::NONE)
                throw FileIOException("Malformed save file: unknown piece character");
            if (type == PieceType::PAWN && (r == 0 || r == 7))
                throw FileIOException("Malformed save file: pawn on the first or last rank");
            Color pieceColor = std::isupper(static_cast<unsigned char>(ch)) ? Color::WHITE : Color::BLACK;
            if (type == PieceType::KING) (pieceColor == Color::WHITE ? whiteKings : blackKings)++;
            auto piece = Piece::createPiece(type, pieceColor);
            if (movedLines[i][c] == '1') piece->setMoved(true);
            parsed.grid[r][c] = std::move(piece);
        }
    }
    if (whiteKings != 1 || blackKings != 1)
        throw FileIOException("Malformed save file: each side needs exactly one king");

    *this = std::move(parsed);
}
