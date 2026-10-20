#include "PlaySession.h"

#include "SearchTracker.h"

#include "chess/Notation.h"
#include "chess/PolyglotBook.h"
#include "match/EnginePlayer.h"
#include "match/Player.h"

namespace uci_host::app
{

namespace
{

using Clock = std::chrono::steady_clock;

/** How long the runner waits for a move with no clock when a person plays: a day, which no person takes. */
constexpr chess::Milliseconds kHumanStall{24LL * 60 * 60 * 1000};

/**
 * An engine's own book, read from its file.
 * @param entry The engine.
 * @return The book's settings; none where it has no book or the file cannot be read.
 */
match::BookSettings BookOf(const EngineEntry &entry)
{
    match::BookSettings settings;
    if (entry.book_path.empty())
    {
        return settings;
    }
    auto book = chess::PolyglotBook::Load(entry.book_path);
    if (book)
    {
        settings.book  = std::make_shared<const chess::PolyglotBook>(std::move(*book));
        settings.depth = entry.book_depth;
    }
    return settings;
}

/**
 * An engine's time control: the game's, with its own depth limit.
 * @param control The game's.
 * @param entry The engine.
 * @return The engine's.
 */
chess::TimeControl ControlOf(const chess::TimeControl &control, const EngineEntry &entry)
{
    chess::TimeControl own = control;
    if (entry.depth)
    {
        own.depth = entry.depth;
    }
    return own;
}

} // namespace

ClockReading ClockReading::Of(const chess::Clock &white, const chess::Clock &black)
{
    ClockReading reading;
    reading.white_remaining   = white.Remaining();
    reading.black_remaining   = black.Remaining();
    reading.white_moves_to_go = white.MovesToGo();
    reading.black_moves_to_go = black.MovesToGo();
    reading.white_increment   = white.Increment();
    reading.black_increment   = black.Increment();
    return reading;
}

chess::TimeControl ResumedTimeControl(const chess::TimeControl &control, chess::Milliseconds remaining, int moves_to_go,
                                      chess::Milliseconds increment)
{
    if (control.sessions.empty())
    {
        return control;
    }
    chess::TimeControl resumed = control;
    resumed.sessions.clear();
    resumed.sessions.push_back({moves_to_go, remaining, increment});
    if (moves_to_go > 0)
    {
        resumed.sessions.push_back(control.sessions.back());
    }
    return resumed;
}

PlaySession::PlaySession(PlaySettings settings) : settings_(std::move(settings))
{
    chess::Game start;
    if (!settings_.start_fen.empty())
    {
        if (auto position = chess::Position::FromFen(settings_.start_fen, chess::FenMode::kTolerant))
        {
            start = chess::Game(*position);
        }
    }
    for (const std::string &move : settings_.start_moves)
    {
        start.PlayUci(move);
    }
    const chess::Clock white_clock(settings_.white_resumed.value_or(settings_.time_control));
    const chess::Clock black_clock(settings_.black_resumed.value_or(settings_.time_control));
    snapshot_.game = start;
    snapshot_.notes.assign(static_cast<std::size_t>(start.PlyCount()), match::MoveNote{});
    for (match::MoveNote &note : snapshot_.notes)
    {
        note.is_book = true;
    }
    snapshot_.white_remaining   = white_clock.Remaining();
    snapshot_.black_remaining   = black_clock.Remaining();
    snapshot_.white_moves_to_go = white_clock.MovesToGo();
    snapshot_.black_moves_to_go = black_clock.MovesToGo();
    snapshot_.white_increment   = white_clock.Increment();
    snapshot_.black_increment   = black_clock.Increment();
    // The clocks at every position so far: a resumed game's as they were recorded, the rest (the unclocked opening
    // moves) as the clocks start.
    snapshot_.clocks = settings_.resumed_clocks;
    snapshot_.clocks.resize(static_cast<std::size_t>(start.PlyCount()) + 1, ClockReading::Of(white_clock, black_clock));
    snapshot_.clocks.back() = ClockReading::Of(white_clock, black_clock);
    snapshot_.turn_started  = Clock::now();
    snapshot_.white_name    = settings_.white.Name();
    snapshot_.black_name    = settings_.black.Name();
    thread_                 = std::thread([this] { Run(); });
}

PlaySession::~PlaySession()
{
    Stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void PlaySession::Run()
{
    std::vector<std::unique_ptr<match::Player>> players;
    for (const SideSpec *side : {&settings_.white, &settings_.black})
    {
        if (side->is_human)
        {
            players.push_back(std::make_unique<match::CallbackPlayer>(
                side->human_name, [this](const match::MoveRequest &request) { return AwaitHuman(request); }));
        }
        else
        {
            const chess::TimeControl &own = side == &settings_.white
                                                ? settings_.white_resumed.value_or(settings_.time_control)
                                                : settings_.black_resumed.value_or(settings_.time_control);
            auto engine = std::make_unique<match::EnginePlayer>(side->engine.config, ControlOf(own, side->engine));
            const chess::Color colour = side == &settings_.white ? chess::kWhite : chess::kBlack;
            const SideSpec    &other  = side == &settings_.white ? settings_.black : settings_.white;
            std::string        name   = EngineLabel(side->engine.DisplayName(), other.Name(), colour);
            engine->SetLogger([this, name, colour](uci::LineDirection direction, std::string_view line) {
                const std::lock_guard<std::mutex> lock(mutex_);
                log_.push_back({name, colour, direction, std::string(line), Clock::now()});
            });
            players.push_back(std::move(engine));
        }
    }

    match::GameSettings game;
    game.opening.fen                       = settings_.start_fen;
    game.opening.moves                     = settings_.start_moves;
    const chess::TimeControl white_control = settings_.white_resumed.value_or(settings_.time_control);
    const chess::TimeControl black_control = settings_.black_resumed.value_or(settings_.time_control);
    game.white_time_control =
        settings_.white.is_human ? white_control : ControlOf(white_control, settings_.white.engine);
    game.black_time_control =
        settings_.black.is_human ? black_control : ControlOf(black_control, settings_.black.engine);
    game.time_margin = chess::Milliseconds{100};
    game.event       = settings_.event;
    if (settings_.white.is_human || settings_.black.is_human)
    {
        game.stall_timeout = kHumanStall;
    }
    if (!settings_.white.is_human)
    {
        game.white_book = BookOf(settings_.white.engine);
    }
    if (!settings_.black.is_human)
    {
        game.black_book = BookOf(settings_.black.engine);
    }

    match::GameRunner runner(*players[0], *players[1], game);
    match::GameRecord record = runner.Play(
        [this](const match::GameProgress &progress) {
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.game = progress.game;
            snapshot_.notes.push_back(progress.note);
            snapshot_.white_remaining   = progress.white_clock.Remaining();
            snapshot_.black_remaining   = progress.black_clock.Remaining();
            snapshot_.white_moves_to_go = progress.white_clock.MovesToGo();
            snapshot_.black_moves_to_go = progress.black_clock.MovesToGo();
            snapshot_.white_increment   = progress.white_clock.Increment();
            snapshot_.black_increment   = progress.black_clock.Increment();
            snapshot_.clocks.resize(static_cast<std::size_t>(progress.game.PlyCount()),
                                    snapshot_.clocks.empty() ? ClockReading{} : snapshot_.clocks.back());
            snapshot_.clocks.push_back(ClockReading::Of(progress.white_clock, progress.black_clock));
            snapshot_.turn_started = Clock::now();
            ++snapshot_.version;
        },
        &cancel_);

    const std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.game             = record.game;
    snapshot_.white_name       = record.white_name;
    snapshot_.black_name       = record.black_name;
    snapshot_.is_finished      = true;
    snapshot_.is_human_to_move = false;
    record_                    = std::move(record);
    ++snapshot_.version;
}

match::MoveReply PlaySession::AwaitHuman(const match::MoveRequest &request)
{
    const auto                   asked = Clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    human_move_.reset();
    snapshot_.is_human_to_move = true;
    snapshot_.turn_started     = asked;
    ++snapshot_.version;
    wake_.wait_until(lock, request.deadline, [this] { return human_move_.has_value() || cancel_.load(); });
    snapshot_.is_human_to_move = false;
    ++snapshot_.version;
    match::MoveReply reply;
    reply.elapsed = std::chrono::duration_cast<chess::Milliseconds>(Clock::now() - asked);
    if (human_move_ && !cancel_.load())
    {
        reply.move = chess::MoveToUci(*human_move_);
        human_move_.reset();
        return reply;
    }
    reply.status = match::MoveReply::Status::kTimeout;
    return reply;
}

PlaySnapshot PlaySession::Snapshot() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

int PlaySession::Version() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.version;
}

bool PlaySession::SubmitHumanMove(chess::Move move)
{
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_.is_human_to_move || human_move_)
    {
        return false;
    }
    bool is_legal = false;
    for (const chess::Move legal : snapshot_.game.CurrentPosition().GenerateLegalMoves())
    {
        is_legal |= legal == move;
    }
    if (!is_legal)
    {
        return false;
    }
    human_move_ = move;
    wake_.notify_all();
    return true;
}

void PlaySession::Stop()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    cancel_ = true;
    wake_.notify_all();
}

std::vector<PlayLogLine> PlaySession::DrainLog()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PlayLogLine>          lines;
    lines.swap(log_);
    return lines;
}

bool PlaySession::IsFinished() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.is_finished;
}

std::optional<match::GameRecord> PlaySession::Record() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return record_;
}

} // namespace uci_host::app
