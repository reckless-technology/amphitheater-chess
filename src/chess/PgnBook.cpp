#include "PgnBook.h"

#include "GameTree.h"
#include "Pgn.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace amphitheater::chess
{

Result<PolyglotBook> BookFromPgn(std::string_view text, int plies, std::vector<std::string> *errors)
{
    const std::vector<PgnGame> games = ReadPgn(text, errors);
    if (games.empty())
    {
        return Error{"no game could be read"};
    }
    // How many games play each move in each position: a position's key and the move as the format packs it.
    std::map<std::pair<std::uint64_t, std::uint16_t>, int> counts;
    for (const PgnGame &game : games)
    {
        const GameTree                                    tree = GameTree::FromPgn(game);
        std::set<std::pair<std::uint64_t, std::uint16_t>> played; // this game's moves, each counted once
        std::vector<GameTree::NodeId>                     waiting{GameTree::kRoot};
        while (!waiting.empty())
        {
            const GameTree::Node &node = tree.node(waiting.back());
            waiting.pop_back();
            if (node.ply >= plies)
            {
                continue;
            }
            const std::uint64_t key = PolyglotKey(node.position);
            for (const GameTree::NodeId child : node.children)
            {
                played.insert({key, PolyglotBook::EncodeMove(tree.node(child).move)});
                waiting.push_back(child);
            }
        }
        for (const auto &move : played)
        {
            ++counts[move];
        }
    }
    if (counts.empty())
    {
        return Error{"its games have no moves"};
    }
    std::vector<PolyglotEntry> entries;
    entries.reserve(counts.size());
    for (const auto &[move, count] : counts)
    {
        entries.push_back({move.first, move.second, static_cast<std::uint16_t>(std::min(count, 65535)), 0});
    }
    return PolyglotBook::FromBytes(PolyglotBook::Encode(std::move(entries)));
}

} // namespace amphitheater::chess
