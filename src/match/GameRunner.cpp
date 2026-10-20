#include "GameRunner.h"

#include "chess/Notation.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace amphitheater::match
{

namespace
{

using Clock = std::chrono::steady_clock;

/**
 * A side's name in an outcome's words.
 * @param color The side.
 * @return "White" or "Black".
 */
std::string SideName(chess::Color color)
{
    return color == chess::kWhite ? "White" : "Black";
}

/**
 * The result of a side losing.
 * @param loser The side that loses.
 * @return The result.
 */
chess::GameResult LossFor(chess::Color loser)
{
    return loser == chess::kWhite ? chess::GameResult::kBlackWins : chess::GameResult::kWhiteWins;
}

/**
 * Whether a side can never mate whatever is played, as the rules decide a flag's fall: a lone king, or a king and
 * one minor piece against a lone king.
 * @param position The position.
 * @param side The side that would have to mate.
 * @return Whether it cannot.
 */
bool CannotMate(const chess::Position &position, chess::Color side)
{
    using namespace chess;
    if ((position.Pieces(kPawn, side) | position.Pieces(kRook, side) | position.Pieces(kQueen, side)).IsNotEmpty())
    {
        return false;
    }
    const int minors        = (position.Pieces(kKnight, side) | position.Pieces(kBishop, side)).PopCount();
    const int opponent_rest = position.colors_[EnemyOf(side)].PopCount() - 1;
    return minors == 0 || (minors == 1 && opponent_rest == 0);
}

/**
 * The outcome of a side's flag falling.
 * @param game The game.
 * @param loser The side whose flag fell.
 * @return A loss on time, or a draw when the opponent could never mate.
 */
chess::Outcome FlagFall(const chess::Game &game, chess::Color loser)
{
    if (CannotMate(game.CurrentPosition(), chess::EnemyOf(loser)))
    {
        return {chess::GameResult::kDraw, chess::Termination::kTimeForfeit,
                "Draw: " + SideName(loser) + "'s flag fell and " + SideName(chess::EnemyOf(loser)) + " cannot mate"};
    }
    return {LossFor(loser), chess::Termination::kTimeForfeit, SideName(loser) + " loses on time"};
}

/**
 * A date as PGN writes it.
 * @param when The moment.
 * @return "YYYY.MM.DD", in UTC.
 */
std::string PgnDate(std::chrono::system_clock::time_point when)
{
    const std::chrono::year_month_day date{std::chrono::floor<std::chrono::days>(when)};
    char                              text[16];
    std::snprintf(text, sizeof text, "%04d.%02u.%02u", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    return text;
}

} // namespace

std::optional<uci::Score> MoveNote::WhiteScore() const
{
    // The mover was the side to move in the position its engine searched.
    return score ? std::optional<uci::Score>(uci::WhitesView(*score, mover)) : std::nullopt;
}

std::string MoveNote::Comment() const
{
    if (is_book)
    {
        return "book";
    }
    std::string text;
    if (const auto white = WhiteScore())
    {
        if (white->kind == uci::Score::Kind::kMate)
        {
            text = (white->value >= 0 ? "+M" : "-M") + std::to_string(std::abs(white->value));
        }
        else
        {
            text = white->ToString();
        }
        if (depth)
        {
            text += "/" + std::to_string(*depth);
        }
        text += ' ';
    }
    return text + chess::SecondsText(elapsed) + "s";
}

chess::PgnGame GameRecord::ToPgn() const
{
    chess::PgnGame pgn = chess::PgnGame::FromGame(game);
    pgn.SetTag("Event", settings.event);
    pgn.SetTag("Site", settings.site);
    pgn.SetTag("Date", PgnDate(started));
    pgn.SetTag("Round", settings.round);
    pgn.SetTag("White", white_name);
    pgn.SetTag("Black", black_name);
    pgn.SetTag("Result", chess::ResultText(game.GetOutcome().result));
    if (settings.white_time_control == settings.black_time_control)
    {
        pgn.SetTag("TimeControl", settings.white_time_control.ToPgnTag());
    }
    else
    {
        pgn.SetTag("WhiteTimeControl", settings.white_time_control.ToPgnTag());
        pgn.SetTag("BlackTimeControl", settings.black_time_control.ToPgnTag());
    }
    pgn.SetTag("Termination", chess::TerminationTag(game.GetOutcome().termination));
    pgn.SetTag("PlyCount", std::to_string(game.PlyCount()));
    if (!settings.opening.name.empty())
    {
        pgn.SetTag("Opening", settings.opening.name);
    }
    for (std::size_t index = 0; index < pgn.moves.size() && index < notes.size(); ++index)
    {
        pgn.moves[index].comment = notes[index].Comment();
    }
    const std::string &description = game.GetOutcome().description;
    if (!description.empty())
    {
        if (pgn.moves.empty())
        {
            pgn.comment = description;
        }
        else
        {
            std::string &comment = pgn.moves.back().comment;
            comment += (comment.empty() ? "" : ", ") + description;
        }
    }
    return pgn;
}

GameRunner::GameRunner(Player &white, Player &black, GameSettings settings)
    : white_(white), black_(black), settings_(std::move(settings))
{
}

GameRecord GameRunner::Play(const std::function<void(const GameProgress &)> &on_move, const std::atomic<bool> *cancel)
{
    GameRecord record;
    record.started    = std::chrono::system_clock::now();
    record.settings   = settings_;
    record.white_name = white_.Name();
    record.black_name = black_.Name();

    const auto abandon = [&](chess::Game game, std::string why) {
        game.SetOutcome({chess::GameResult::kNone, chess::Termination::kAbandoned, std::move(why)});
        record.game = std::move(game);
        return record;
    };

    chess::Position start = chess::Position::Start();
    if (!settings_.opening.fen.empty())
    {
        auto position = chess::Position::FromFen(settings_.opening.fen, chess::FenMode::kTolerant);
        if (!position)
        {
            return abandon(chess::Game{}, "the opening's FEN is not a position: " + position.ErrorMessage());
        }
        start = *position;
    }
    chess::Game game(start);
    for (const auto &move : settings_.opening.moves)
    {
        const auto played = game.PlayUci(move);
        if (!played)
        {
            return abandon(game, "the opening's move '" + move + "' cannot be played: " + played.ErrorMessage());
        }
        MoveNote note;
        note.is_book = true;
        note.mover   = chess::EnemyOf(game.CurrentPosition().SideToMove());
        record.notes.push_back(note);
    }

    chess::Clock white_clock(settings_.white_time_control);
    chess::Clock black_clock(settings_.black_time_control);
    if (!game.IsOver())
    {
        for (const chess::Color color : {chess::kWhite, chess::kBlack})
        {
            Player    &player = color == chess::kWhite ? white_ : black_;
            const auto status = player.StartGame(game);
            if (!status)
            {
                game.SetOutcome({LossFor(color), chess::Termination::kDisconnection,
                                 SideName(color) + "'s engine failed to start: " + status.ErrorMessage()});
                break;
            }
        }
        record.white_name = white_.Name();
        record.black_name = black_.Name();
    }

    Adjudicator                    adjudicator(settings_.adjudication);
    std::array<std::mt19937_64, 2> book_generators{std::mt19937_64(settings_.white_book.seed),
                                                   std::mt19937_64(settings_.black_book.seed)};
    std::array<bool, 2>            is_out_of_book{false, false};
    const auto                     tell_players = [&]() {
        const MoveRequest state{game, white_clock, black_clock, Clock::now()};
        white_.MoveMade(state);
        black_.MoveMade(state);
    };
    while (!game.IsOver())
    {
        if (cancel != nullptr && cancel->load())
        {
            game.SetOutcome({chess::GameResult::kNone, chess::Termination::kAbandoned, "Game abandoned"});
            break;
        }
        const chess::Color side   = game.CurrentPosition().SideToMove();
        Player            &player = side == chess::kWhite ? white_ : black_;
        chess::Clock      &clock  = side == chess::kWhite ? white_clock : black_clock;
        // The side's own book, while it has a move and the game is shorter than its depth: no clock, no engine.
        const BookSettings &book = side == chess::kWhite ? settings_.white_book : settings_.black_book;
        if (book.book && !is_out_of_book[side] && (book.depth <= 0 || game.PlyCount() < book.depth))
        {
            if (const auto book_move = book.book->Choose(game.CurrentPosition(), book.selection, book_generators[side]))
            {
                game.Play(*book_move);
                MoveNote note;
                note.is_book = true;
                note.mover   = side;
                record.notes.push_back(note);
                if (on_move)
                {
                    on_move({game, record.notes.back(), white_clock, black_clock});
                }
                if (!game.IsOver())
                {
                    tell_players();
                }
                continue;
            }
            is_out_of_book[side] = true;
        }
        const auto available = clock.TimeAvailable();
        const auto deadline = Clock::now() + (available ? *available + settings_.time_margin : settings_.stall_timeout);
        const MoveReply reply = player.RequestMove({game, white_clock, black_clock, deadline});

        // A side that answers with no move once the game has been cancelled - a person's wait at the GUI given up
        // when the game is stopped - has the game abandoned, not lost on time or by disconnection.
        if (cancel != nullptr && cancel->load() && reply.status != MoveReply::Status::kMove)
        {
            game.SetOutcome({chess::GameResult::kNone, chess::Termination::kAbandoned, "Game abandoned"});
            break;
        }
        if (reply.status == MoveReply::Status::kTimeout)
        {
            if (clock.Control().IsTimed())
            {
                clock.Spend(reply.elapsed, settings_.time_margin);
                game.SetOutcome(FlagFall(game, side));
            }
            else
            {
                game.SetOutcome({LossFor(side), chess::Termination::kStalled, SideName(side) + "'s engine stalls"});
            }
            break;
        }
        if (reply.status == MoveReply::Status::kDisconnected)
        {
            game.SetOutcome({LossFor(side), chess::Termination::kDisconnection,
                             SideName(side) + "'s engine disconnects" +
                                 (reply.detail.empty() ? std::string{} : " (" + reply.detail + ")")});
            break;
        }
        if (clock.Control().IsTimed() && !clock.Spend(reply.elapsed, settings_.time_margin))
        {
            game.SetOutcome(FlagFall(game, side));
            break;
        }
        const auto move = chess::ParseUciMove(game.CurrentPosition(), reply.move);
        if (!move)
        {
            game.SetOutcome({LossFor(side), chess::Termination::kIllegalMove,
                             reply.move.empty() ? SideName(side) + "'s engine names no move"
                                                : SideName(side) + " makes an illegal move: " + reply.move});
            break;
        }
        MoveNote note;
        note.score   = reply.info.score;
        note.mover   = side;
        note.depth   = reply.info.depth;
        note.elapsed = reply.elapsed;
        game.Play(*move);
        record.notes.push_back(note);
        if (on_move)
        {
            on_move({game, record.notes.back(), white_clock, black_clock});
        }
        if (!game.IsOver())
        {
            if (const auto outcome = adjudicator.Judge(game, side, reply.info.score))
            {
                game.SetOutcome(*outcome);
            }
        }
        if (!game.IsOver())
        {
            tell_players();
        }
    }
    white_.EndGame(game);
    black_.EndGame(game);
    record.game = std::move(game);
    return record;
}

} // namespace amphitheater::match
