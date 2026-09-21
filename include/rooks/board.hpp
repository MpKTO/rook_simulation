#pragma once

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace rooks
{

    inline constexpr int         board_size     = 8;
    inline constexpr int         moves_per_rook = 50;
    inline constexpr std::size_t min_rooks      = 4;
    inline constexpr std::size_t max_rooks      = 6;

    struct Square
    {
        int file = 0; // A..H correspond to 0..7.
        int rank = 0; // 1..8 correspond to 0..7.

        bool operator==(const Square&) const = default;
    };

    struct Rook
    {
        Square square;
        int    moves = 0;
        bool   stuck = false;

        bool finished() const
        {
            return moves == moves_per_rook;
        }
    };

    bool inside(Square square);
    Square parse_square(std::string_view text);
    std::string square_name(Square square);

    std::vector<Rook> random_rooks(std::size_t count, std::mt19937& random);
    Square random_target(Square from, std::mt19937& random, std::optional<Square> excluded = std::nullopt);

    // Pure board rules: no threads, timers, or output. The simulation supplies locking.
    class Board
    {
    public:
        explicit Board(std::vector<Rook> rooks);

        const std::vector<Rook>& rooks() const
        {
            return rooks_;
        }
        std::optional<std::size_t> occupant_at(Square square) const;
        bool path_clear(std::size_t rook, Square target) const;
        bool try_move(std::size_t rook, Square target);
        bool done() const;

    private:
        void update_stuck();

        std::vector<Rook> rooks_;
    };

} // namespace rooks
