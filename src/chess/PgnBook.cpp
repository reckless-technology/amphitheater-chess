#include "PgnBook.h"

#include "GameTree.h"
#include "Pgn.h"

#include <algorithm>
#include <set>

namespace amphitheater::chess
{

int PgnBookBuilder::Add(std::string_view text, std::vector<std::string> *errors)
{
    const std::vector<PgnGame> games = ReadPgn(text, errors);
    for (const PgnGame &game : games)
    {
        const GameTree                                    tree = GameTree::FromPgn(game);
        std::set<std::pair<std::uint64_t, std::uint16_t>> played; // this game's moves, each counted once
        std::vector<GameTree::NodeId>                     waiting{GameTree::kRoot};
        while (!waiting.empty())
        {
            const GameTree::Node &node = tree.node(waiting.back());
            waiting.pop_back();
            if (node.ply >= plies_)
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
            ++counts_[move];
        }
    }
    games_ += static_cast<long long>(games.size());
    return static_cast<int>(games.size());
}

std::vector<PolyglotEntry> PgnBookBuilder::Entries(int least_games) const
{
    long long most = 0;
    for (const auto &[move, count] : counts_)
    {
        most = std::max(most, count);
    }
    std::vector<PolyglotEntry> entries;
    for (const auto &[move, count] : counts_)
    {
        if (count < least_games)
        {
            continue;
        }
        const long long weight = most <= 65535 ? count : std::max(1LL, count * 65535 / most);
        entries.push_back({move.first, move.second, static_cast<std::uint16_t>(weight), 0});
    }
    return entries;
}

Result<PolyglotBook> BookFromPgn(std::string_view text, int plies, std::vector<std::string> *errors)
{
    PgnBookBuilder builder(plies);
    if (builder.Add(text, errors) == 0)
    {
        return Error{"no game could be read"};
    }
    std::vector<PolyglotEntry> entries = builder.Entries();
    if (entries.empty())
    {
        return Error{"its games have no moves"};
    }
    return PolyglotBook::FromBytes(PolyglotBook::Encode(std::move(entries)));
}

} // namespace amphitheater::chess
