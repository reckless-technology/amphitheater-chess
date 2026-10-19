#include "SearchTracker.h"

#include "chess/Notation.h"

#include <cmath>
#include <cstdio>

namespace uci_host::app
{

namespace
{

/** What a mate counts as on the graph, in pawns: past any scale it draws. */
constexpr double kMatePawns = 100.0;

/** What a "position" command names. */
struct SentPosition
{
    chess::Position position;           ///< The position.
    std::string     last_move_numbered; ///< Its last move, numbered in SAN ("13.Ne2"); empty with no move.
};

/**
 * Read a "position" command into the position it names.
 * @param line The command.
 * @return The position, or nothing for a line that names none legibly.
 */
std::optional<SentPosition> PositionOf(std::string_view line)
{
    const auto words = uci::SplitWords(line);
    if (words.size() < 2 || words[0] != "position")
    {
        return std::nullopt;
    }
    std::size_t     index = 1;
    chess::Position position;
    if (words[1] == "startpos")
    {
        position = chess::Position::Start();
        index    = 2;
    }
    else if (words[1] == "fen")
    {
        std::string fen;
        for (index = 2; index < words.size() && words[index] != "moves"; ++index)
        {
            fen += (fen.empty() ? "" : " ") + std::string(words[index]);
        }
        auto parsed = chess::Position::FromFen(fen, chess::FenMode::kTolerant);
        if (!parsed)
        {
            return std::nullopt;
        }
        position = *parsed;
    }
    else
    {
        return std::nullopt;
    }
    std::string last;
    if (index < words.size() && words[index] == "moves")
    {
        for (++index; index < words.size(); ++index)
        {
            const auto move = chess::ParseUciMove(position, words[index]);
            if (!move)
            {
                return std::nullopt;
            }
            last     = PvToSan(position, {std::string(words[index])});
            position = position.MakeMove(*move);
        }
    }
    return SentPosition{position, last};
}

} // namespace

std::string PvToSan(const chess::Position &position, const std::vector<std::string> &pv, std::size_t most)
{
    std::string     text;
    chess::Position at    = position;
    std::size_t     count = 0;
    for (const std::string &uci_move : pv)
    {
        if (most != 0 && count == most)
        {
            break;
        }
        const auto move = chess::ParseUciMove(at, uci_move);
        if (!move)
        {
            break;
        }
        const bool is_white = at.SideToMove() == chess::kWhite;
        if (!text.empty())
        {
            text += ' ';
        }
        if (is_white)
        {
            text += std::to_string(at.FullmoveNumber()) + ".";
        }
        else if (count == 0)
        {
            text += std::to_string(at.FullmoveNumber()) + "...";
        }
        text += chess::MoveToSan(at, *move);
        at = at.MakeMove(*move);
        ++count;
    }
    return text;
}

std::string WhiteScoreText(const uci::Score &score, chess::Color side_to_move)
{
    const int value = side_to_move == chess::kWhite ? score.value : -score.value;
    if (score.kind == uci::Score::Kind::kMate)
    {
        return "#" + std::to_string(value);
    }
    char text[32];
    std::snprintf(text, sizeof text, "%+.2f", value / 100.0);
    return text;
}

double WhitePawns(const uci::Score &score, chess::Color side_to_move)
{
    const double sign = side_to_move == chess::kWhite ? 1.0 : -1.0;
    if (score.kind == uci::Score::Kind::kMate)
    {
        return sign * (score.value >= 0 ? kMatePawns : -kMatePawns);
    }
    return sign * score.value / 100.0;
}

std::string NodesText(std::int64_t nodes)
{
    char text[32];
    if (nodes < 1000)
    {
        std::snprintf(text, sizeof text, "%lld n", static_cast<long long>(nodes));
    }
    else if (nodes < 1000000)
    {
        std::snprintf(text, sizeof text, "%.1f kn", static_cast<double>(nodes) / 1e3);
    }
    else if (nodes < 1000000000)
    {
        std::snprintf(text, sizeof text, "%.1f Mn", static_cast<double>(nodes) / 1e6);
    }
    else
    {
        std::snprintf(text, sizeof text, "%.2f Gn", static_cast<double>(nodes) / 1e9);
    }
    return text;
}

SearchTracker::SearchTracker(std::string name)
    : name_(std::move(name)), position_(chess::Position::Start()), sent_position_(chess::Position::Start())
{
}

void SearchTracker::Begin(const chess::Position &position, bool is_ponder, TimePoint at)
{
    position_     = position;
    is_searching_ = true;
    is_ponder_    = is_ponder;
    started_      = at;
    lines_.clear();
    nodes_.reset();
    nps_.reset();
    hash_full_.reset();
    ponder_san_.clear();
}

void SearchTracker::PonderHit(TimePoint at)
{
    if (is_searching_ && is_ponder_)
    {
        is_ponder_ = false;
        started_   = at;
    }
}

void SearchTracker::Info(const uci::SearchInfo &info)
{
    if (info.nodes)
    {
        nodes_ = info.nodes;
    }
    if (info.nodes_per_second)
    {
        nps_ = info.nodes_per_second;
    }
    if (info.hash_full)
    {
        hash_full_ = info.hash_full;
    }
    if (info.pv.empty() || !info.score)
    {
        return;
    }
    Line &line    = lines_[info.multipv.value_or(1)];
    line.depth    = info.depth.value_or(line.depth);
    line.seldepth = info.selective_depth.value_or(0);
    line.score    = info.score;
    line.pv       = info.pv;
}

void SearchTracker::End()
{
    is_searching_ = false;
    is_ponder_    = false;
}

void SearchTracker::Feed(bool is_to_engine, std::string_view line, TimePoint at)
{
    const auto words = uci::SplitWords(line);
    if (words.empty())
    {
        return;
    }
    if (is_to_engine)
    {
        if (words[0] == "position")
        {
            if (const auto sent = PositionOf(line))
            {
                sent_position_  = sent->position;
                sent_last_move_ = sent->last_move_numbered;
            }
        }
        else if (words[0] == "go")
        {
            bool is_ponder = false;
            for (const auto word : words)
            {
                is_ponder |= word == "ponder";
            }
            Begin(sent_position_, is_ponder, at);
            if (is_ponder)
            {
                ponder_san_ = sent_last_move_; // the move pondered on is the last of the position sent
            }
        }
        else if (words[0] == "ponderhit")
        {
            PonderHit(at);
        }
        return;
    }
    if (words[0] == "info")
    {
        Info(uci::ParseInfoLine(line));
    }
    else if (words[0] == "bestmove")
    {
        End();
    }
}

std::optional<chess::Move> SearchTracker::BestMove() const
{
    const auto best = lines_.find(1);
    if (best == lines_.end() || best->second.pv.empty())
    {
        return std::nullopt;
    }
    const auto move = chess::ParseUciMove(position_, best->second.pv.front());
    return move ? std::optional<chess::Move>(*move) : std::nullopt;
}

std::optional<double> SearchTracker::BestWhitePawns() const
{
    const auto best = lines_.find(1);
    if (best == lines_.end() || !best->second.score)
    {
        return std::nullopt;
    }
    return WhitePawns(*best->second.score, position_.SideToMove());
}

std::optional<std::string> SearchTracker::BestScoreText() const
{
    const auto best = lines_.find(1);
    if (best == lines_.end() || !best->second.score)
    {
        return std::nullopt;
    }
    return WhiteScoreText(*best->second.score, position_.SideToMove());
}

EngineBlock SearchTracker::Block(TimePoint now, std::size_t most_lines) const
{
    EngineBlock block;
    block.name = name_;
    if (is_searching_)
    {
        char text[64];
        if (is_ponder_)
        {
            block.state       = ponder_san_.empty() ? "pondering" : "pondering " + ponder_san_;
            block.is_thinking = false;
        }
        else
        {
            const double seconds = std::chrono::duration<double>(now - started_).count();
            std::snprintf(text, sizeof text, "thinking %.1f s", seconds);
            block.state = text;
        }
    }
    std::string stats;
    const auto  append = [&stats](const std::string &part) { stats += (stats.empty() ? "" : " \xc2\xb7 ") + part; };
    if (const auto best = lines_.find(1); best != lines_.end())
    {
        append("depth " + std::to_string(best->second.depth) +
               (best->second.seldepth > 0 ? "/" + std::to_string(best->second.seldepth) : std::string()));
    }
    if (nodes_)
    {
        append(NodesText(*nodes_));
    }
    if (nps_)
    {
        append(NodesText(*nps_) + "/s");
    }
    if (hash_full_)
    {
        append("hash " + std::to_string(*hash_full_ / 10) + " %");
    }
    block.stats = stats;
    for (const auto &[rank, line] : lines_)
    {
        if (block.lines.size() == most_lines)
        {
            break;
        }
        AnalysisLine shown;
        shown.depth = std::to_string(line.depth) + (line.seldepth > 0 ? "/" + std::to_string(line.seldepth) : "");
        if (line.score)
        {
            shown.score       = WhiteScoreText(*line.score, position_.SideToMove());
            const double pawn = WhitePawns(*line.score, position_.SideToMove());
            shown.sign        = pawn > 0.0 ? 1 : pawn < 0.0 ? -1 : 0;
        }
        shown.pv = PvToSan(position_, line.pv, 16);
        block.lines.push_back(std::move(shown));
    }
    return block;
}

} // namespace uci_host::app
