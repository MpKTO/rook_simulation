#include "rooks/simulation.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

    void usage(std::ostream& out)
    {
        out << "Usage: rooks [--seed NUMBER] [N | SQUARE SQUARE ...]\n"
            << "  N: 4, 5, or 6; randomly placed rooks (default: 5).\n"
            << "  Or supply 4 to 6 distinct squares, e.g. A1 C3 F6 H8.\n"
            << "  --seed: unsigned 32-bit seed; omitted = random seed.\n"
            << "  --help: show this message.\n";
    }

    std::uint32_t parse_number(std::string_view text)
    {
        std::uint32_t value     = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
        {
            throw std::invalid_argument("Invalid unsigned number: " + std::string(text));
        }
        return value;
    }

    void print_board(const std::vector<rooks::Rook>& pieces)
    {
        std::array<std::array<std::size_t, rooks::board_size>, rooks::board_size> cells{};
        for (std::size_t i = 0; i < pieces.size(); ++i)
        {
            const auto square                                                                   = pieces[i].square;
            cells[static_cast<std::size_t>(square.rank)][static_cast<std::size_t>(square.file)] = i + 1;
        }
        for (int rank = rooks::board_size - 1; rank >= 0; --rank)
        {
            std::cout << rank + 1 << " ";
            for (const auto id : cells[static_cast<std::size_t>(rank)])
            {
                std::cout << std::setw(4) << (id ? "R" + std::to_string(id) : ".");
            }
            std::cout << '\n';
        }
        std::cout << "     A   B   C   D   E   F   G   H\n";
    }

    void print_event(const rooks::Event& event)
    {
        const double seconds = std::chrono::duration<double>(event.elapsed).count();
        std::cout << '[' << std::fixed << std::setprecision(3) << std::setw(8) << seconds << "s #" << event.sequence << "] R" << event.rook + 1
                  << ' ';
        switch (event.kind)
        {
            case rooks::EventKind::moved:
                std::cout << rooks::square_name(event.from) << " -> " << rooks::square_name(event.target) << " | move " << event.moves << '/'
                          << rooks::moves_per_rook;
                if (event.moves == rooks::moves_per_rook)
                {
                    std::cout << " | FINISHED";
                }
                else
                {
                    std::cout << " | pause " << event.pause.count() << "ms";
                }
                break;
            case rooks::EventKind::waiting:
                std::cout << "at " << rooks::square_name(event.from) << " waits for " << rooks::square_name(event.target) << " (path blocked)";
                break;
            case rooks::EventKind::timeout:
                std::cout << "at " << rooks::square_name(event.from) << ": target " << rooks::square_name(event.target)
                          << " still blocked after 5 seconds; choosing another";
                break;
            case rooks::EventKind::stuck:
                std::cout << "at " << rooks::square_name(event.from) << ": permanently STUCK (" << event.moves << '/' << rooks::moves_per_rook
                          << "); retry cycle continues until game ends";
                break;
        }
        std::cout << '\n';
    }

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        std::optional<std::uint32_t>  requested_seed;
        std::vector<std::string_view> positions;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if (arg == "--help")
            {
                usage(std::cout);
                return 0;
            }
            if (arg == "--seed")
            {
                if (requested_seed || i + 1 == argc)
                {
                    throw std::invalid_argument("Supply --seed exactly once, with a number.");
                }
                requested_seed = parse_number(argv[++i]);
            }
            else if (arg.starts_with("--"))
            {
                throw std::invalid_argument("Unknown option: " + std::string(arg));
            }
            else
            {
                positions.push_back(arg);
            }
        }

        const auto               seed = requested_seed ? *requested_seed : static_cast<std::uint32_t>(std::random_device{}());
        std::mt19937             random(seed);
        std::vector<rooks::Rook> initial;
        if (positions.size() <= 1)
        {
            const auto count = positions.empty() ? 5U : parse_number(positions[0]);
            initial          = rooks::random_rooks(count, random);
        }
        else
        {
            for (const auto text : positions)
            {
                initial.push_back({rooks::parse_square(text)});
            }
        }
        rooks::Simulation simulation(initial, seed);

        std::cout.exceptions(std::ios::badbit | std::ios::failbit);
        std::cout << "8x8 rook simulation | seed " << seed << " | " << initial.size() << " rooks\n"
                  << "50 moves each; pauses 200-300ms; blocked-target timeout 5s.\n"
                  << "Finished rooks stay on the board.\n\nInitial board:\n";
        print_board(initial);
        std::cout << '\n';

        const auto  result   = simulation.run(print_event);
        std::size_t finished = 0;
        for (const auto& rook : result)
        {
            finished += rook.finished() ? 1U : 0U;
        }
        std::cout << "\nGame ended: " << finished << " finished, " << result.size() - finished << " permanently stuck.\n";
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const auto& rook = result[i];
            std::cout << "R" << i + 1 << " at " << rooks::square_name(rook.square) << ": " << (rook.finished() ? "FINISHED" : "STUCK") << " ("
                      << rook.moves << '/' << rooks::moves_per_rook << ")\n";
        }
        std::cout << "\nFinal board:\n";
        print_board(result);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
