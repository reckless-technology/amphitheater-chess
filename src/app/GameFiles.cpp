#include "GameFiles.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace uci_host::app
{

GameInfo InfoFromPgn(const chess::PgnGame &game)
{
    GameInfo   info;
    const auto tag_or = [&game](const char *name, const char *fallback) {
        const std::string value = game.Tag(name);
        return value.empty() || value == "?" ? std::string(fallback) : value;
    };
    info.white                  = tag_or("White", "White");
    info.black                  = tag_or("Black", "Black");
    info.event                  = tag_or("Event", "");
    const std::string eco       = game.Tag("ECO");
    const std::string opening   = game.Tag("Opening");
    const std::string variation = game.Tag("Variation");
    info.opening                = (eco.empty() || eco == "?" ? "" : eco + " ") + opening +
                                  (variation.empty() ? "" : (opening.empty() ? "" : ", ") + variation);
    const std::string control   = game.Tag("TimeControl");
    if (!control.empty() && control != "?" && control != "-")
    {
        if (const auto parsed = chess::TimeControl::ParsePgnTag(control))
        {
            info.time_control = parsed->ToString();
        }
    }
    const std::string date = game.Tag("Date");
    if (!date.empty() && date != "????.??.??")
    {
        info.white_details = "White \xc2\xb7 " + date;
        info.black_details = "Black \xc2\xb7 " + date;
    }
    return info;
}

chess::PgnGame PgnOf(const chess::Game &game, const GameInfo &info, const std::optional<chess::PgnGame> &source,
                     const std::string &date)
{
    if (source)
    {
        const chess::Game read = source->ToGame();
        if (read.UciMoves() == game.UciMoves() && read.StartPosition().ToFen() == game.StartPosition().ToFen())
        {
            chess::PgnGame kept = *source;
            if (game.IsOver())
            {
                kept.result = game.GetOutcome().result;
                kept.SetTag("Result", chess::ResultText(kept.result));
            }
            return kept;
        }
    }
    chess::PgnGame pgn = chess::PgnGame::FromGame(game);
    pgn.SetTag("Event", info.event.empty() ? "?" : info.event);
    pgn.SetTag("Site", "?");
    pgn.SetTag("Date", date);
    pgn.SetTag("Round", "-");
    pgn.SetTag("White", info.white);
    pgn.SetTag("Black", info.black);
    pgn.SetTag("Result", chess::ResultText(pgn.result));
    return pgn;
}

std::string TodayInPgn()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm           local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[16];
    std::snprintf(text, sizeof text, "%04d.%02d.%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return text;
}

PastedGames ReadPasted(std::string_view text)
{
    PastedGames pasted;
    std::size_t first = text.find_first_not_of(" \t\r\n");
    std::size_t last  = text.find_last_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return pasted;
    }
    const std::string_view trimmed = text.substr(first, last - first + 1);
    if (trimmed.find('\n') == std::string_view::npos && trimmed.find('[') == std::string_view::npos)
    {
        if (auto position = chess::Position::FromFen(trimmed, chess::FenMode::kTolerant))
        {
            pasted.position = *position;
            return pasted;
        }
    }
    std::vector<std::string> errors;
    pasted.games   = chess::ReadPgn(trimmed, &errors);
    pasted.skipped = errors.size();
    return pasted;
}

} // namespace uci_host::app
