#include "rooks/board.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace rooks
{
    namespace
    {

        constexpr std::array<Square, 4> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

        bool between(int value, int a, int b)
        {
            return std::min(a, b) <= value && value <= std::max(a, b);
        }

        void check_count(std::size_t count)
        {
            if (count < min_rooks || count > max_rooks)
            {
                throw std::invalid_argument("The board must contain 4 to 6 rooks.");
            }
        }

    } // namespace

    bool inside(Square square)
    {
        return square.file >= 0 && square.file < board_size && square.rank >= 0 && square.rank < board_size;
    }

    Square parse_square(std::string_view text)
    {
        if (text.size() != 2)
        {
            throw std::invalid_argument("Invalid square: " + std::string(text));
        }
        char file = text[0];
        if (file >= 'a' && file <= 'h')
        {
            file = static_cast<char>(file - 'a' + 'A');
        }
        const Square square{file - 'A', text[1] - '1'};
        if (!inside(square))
        {
            throw std::invalid_argument("Invalid square: " + std::string(text));
        }
        return square;
    }

    std::string square_name(Square square)
    {
        if (!inside(square))
        {
            throw std::invalid_argument("Square is outside the board.");
        }
        return {static_cast<char>('A' + square.file), static_cast<char>('1' + square.rank)};
    }

    std::vector<Rook> random_rooks(std::size_t count, std::mt19937& random)
    {
        check_count(count);
        std::array<Square, static_cast<std::size_t>(board_size) * static_cast<std::size_t>(board_size)> squares{};
        for (int rank = 0; rank < board_size; ++rank)
        {
            for (int file = 0; file < board_size; ++file)
            {
                squares[static_cast<std::size_t>(rank) * static_cast<std::size_t>(board_size) + static_cast<std::size_t>(file)] = {file, rank};
            }
        }
        std::shuffle(squares.begin(), squares.end(), random);

        std::vector<Rook> result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            result.push_back({squares[i]});
        }
        return result;
    }

    Square random_target(Square from, std::mt19937& random, std::optional<Square> excluded)
    {
        if (!inside(from))
        {
            throw std::invalid_argument("Starting square is outside the board.");
        }
        std::array<Square, 2ULL * (board_size - 1)> candidates{};
        std::size_t                                 count = 0;
        for (int coordinate = 0; coordinate < board_size; ++coordinate)
        {
            const Square horizontal{coordinate, from.rank};
            const Square vertical{from.file, coordinate};
            if (horizontal != from && horizontal != excluded)
            {
                candidates[count++] = horizontal;
            }
            if (vertical != from && vertical != excluded)
            {
                candidates[count++] = vertical;
            }
        }
        // Deliberately independent of occupancy: blocked destinations stay in the draw.
        return candidates[std::uniform_int_distribution<std::size_t>(0, count - 1)(random)];
    }

    Board::Board(std::vector<Rook> rooks)
        : rooks_(std::move(rooks))
    {
        check_count(rooks_.size());
        for (std::size_t i = 0; i < rooks_.size(); ++i)
        {
            const auto& rook = rooks_[i];
            if (!inside(rook.square) || rook.moves < 0 || rook.moves > moves_per_rook)
            {
                throw std::invalid_argument("Invalid rook position or move count.");
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (rook.square == rooks_[j].square)
                {
                    throw std::invalid_argument("Two rooks cannot start on the same square.");
                }
            }
        }
        update_stuck();
    }

    std::optional<std::size_t> Board::occupant_at(Square square) const
    {
        for (std::size_t i = 0; i < rooks_.size(); ++i)
        {
            if (rooks_[i].square == square)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    bool Board::path_clear(std::size_t rook, Square target) const
    {
        const Square from = rooks_.at(rook).square;
        if (!inside(target) || target == from || (from.file != target.file && from.rank != target.rank))
        {
            return false;
        }
        for (std::size_t i = 0; i < rooks_.size(); ++i)
        {
            if (i == rook)
            {
                continue;
            }
            const Square other = rooks_[i].square;
            if (from.rank == target.rank && other.rank == from.rank && between(other.file, from.file, target.file))
            {
                return false;
            }
            if (from.file == target.file && other.file == from.file && between(other.rank, from.rank, target.rank))
            {
                return false;
            }
        }
        return true;
    }

    bool Board::try_move(std::size_t rook, Square target)
    {
        if (rooks_.at(rook).finished() || !path_clear(rook, target))
        {
            return false;
        }
        rooks_[rook].square = target;
        ++rooks_[rook].moves;
        update_stuck();
        return true;
    }

    bool Board::done() const
    {
        return std::all_of(rooks_.begin(),
                           rooks_.end(),
                           [](const Rook& rook)
                           {
                               return rook.finished() || rook.stuck;
                           });
    }

    void Board::update_stuck()
    {
        std::array<bool, max_rooks> movable{};
        bool                        changed;
        do
        {
            changed = false;
            for (std::size_t i = 0; i < rooks_.size(); ++i)
            {
                if (rooks_[i].finished() || movable[i])
                {
                    continue;
                }
                for (const Square direction : directions)
                {
                    const Square neighbor{rooks_[i].square.file + direction.file, rooks_[i].square.rank + direction.rank};
                    if (!inside(neighbor))
                    {
                        continue;
                    }
                    const auto occupant = occupant_at(neighbor);
                    // An empty neighbor allows a move now. A movable neighbor can
                    // vacate later. Finished rooks never become movable. Repeating
                    // this propagation also detects groups trapped by one another.
                    if (!occupant || movable[*occupant])
                    {
                        movable[i] = true;
                        changed    = true;
                        break;
                    }
                }
            }
        }
        while (changed);

        for (std::size_t i = 0; i < rooks_.size(); ++i)
        {
            rooks_[i].stuck = !rooks_[i].finished() && !movable[i];
        }
    }

} // namespace rooks
