#include "MainController.h"

#include "AboutDialog.h"
#include "BundledEngines.h"
#include "Commands.h"
#include "CompetitionDialog.h"
#include "Controls.h"
#include "EngineManagerDialog.h"
#include "GameFiles.h"
#include "GamePickerDialog.h"
#include "NewGameDialog.h"
#include "StandingsDialog.h"
#include "Theme.h"
#include "match/Openings.h"
#include "version/Version.h"

#include "AppUpdates.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace uci_host::app
{

namespace
{

/**
 * A side's strip details: "White · engine · Threads 1, Hash 64 MB, ponder", "Black · you".
 * @param is_white Which side.
 * @param side Who plays it.
 * @return The text.
 */
std::string DetailsOf(bool is_white, const SideSpec &side)
{
    std::string text = is_white ? "White" : "Black";
    if (side.is_human)
    {
        return text + " \xc2\xb7 you, at the board";
    }
    text += " \xc2\xb7 engine";
    const std::string summary = side.engine.Summary();
    return summary.empty() ? text : text + " \xc2\xb7 " + summary;
}

/**
 * A time control as a person reads it: a preset's words where it is one, else the command line's text.
 * @param control The time control.
 * @return The text; "no clock" for none.
 */
std::string TimeControlText(const chess::TimeControl &control)
{
    if (!control.IsTimed())
    {
        return "no clock";
    }
    const std::string text = control.ToString();
    for (const TimeControlPreset &preset : TimeControlPresets())
    {
        if (preset.text != nullptr && *preset.text != '\0')
        {
            if (const auto parsed = chess::TimeControl::Parse(preset.text); parsed && parsed->ToString() == text)
            {
                return preset.label;
            }
        }
    }
    return text;
}

/**
 * A result's large line and its words.
 * @param outcome How the game ended.
 * @return The title ("1-0", "Stopped") and the detail.
 */
std::pair<std::string, std::string> BannerOf(const chess::Outcome &outcome)
{
    if (outcome.termination == chess::Termination::kAbandoned)
    {
        return {"Stopped", outcome.description.empty() ? "Game abandoned" : outcome.description};
    }
    std::string title = chess::ResultText(outcome.result);
    if (title == "1/2-1/2")
    {
        title = "\xc2\xbd-\xc2\xbd";
    }
    return {title, outcome.description};
}

} // namespace

MainController::MainController(MainView &view, ControllerFiles files, ControllerServices services)
    : view_(view), services_(std::move(services)), files_(std::move(files))
{
    if (!services_.read_clipboard)
    {
        services_.read_clipboard = [this] {
            reckless::view::Host *host = view_.host();
            return host != nullptr ? host->ClipboardText() : std::string();
        };
    }
    if (!services_.write_clipboard)
    {
        services_.write_clipboard = [this](const std::string &text) {
            reckless::view::Host *host = view_.host();
            return host != nullptr && host->SetClipboardText(text);
        };
    }
    if (!services_.choose_file)
    {
        services_.choose_file = [this](const reckless::view::FileRequest       &request,
                                       std::function<void(const std::string &)> picked) {
            view_.ChooseFile(request, std::move(picked));
        };
    }
    std::string error;
    if (auto loaded = EngineCatalogue::Load(files_.engines, &error))
    {
        catalogue_ = std::move(*loaded);
        // The engines the install carries, added to the list or moved to this install's paths and versions. A list
        // that could not be read is never seeded, so it is never written over.
        if (!files_.bundled_engines.empty() &&
            SeedBundledEngines(catalogue_, ReadBundledEngines(files_.bundled_engines, DetectCpuLevels())) &&
            !files_.engines.empty() && !catalogue_.Save(files_.engines))
        {
            error_ = "The engine list could not be saved";
        }
    }
    else
    {
        error_ = "The engine list could not be read: " + error;
    }
    view_.on_command    = [this](int command) { Command(command); };
    view_.on_select_ply = [this](int ply) { ShowPly(ply); };
    view_.on_move       = [this](chess::Move move) { BoardMove(move); };
    if (!services_.set_menu_bar)
    {
        // No platform's bar: the window's own, drawn across its top, its shortcuts pressed by whoever drives it.
        own_menus_ = std::make_shared<reckless::app::MenuBarController>();
        view_.SetMenuBarView(std::make_unique<reckless::app::MenuBarView>(own_menus_, theme::MenuBarLook()));
        services_.set_menu_bar = [menus = own_menus_](const reckless::app::MenuBar &bar,
                                                      std::function<void(int id)>   chosen) {
            menus->Set(bar, std::move(chosen));
        };
    }
    Refresh();
    RefreshMenuBar();
}

MainController::~MainController()
{
    analysis_.reset();
    competition_.reset(); // stops play; each worker finishes the move it is on
    if (dialog_)
    {
        view_.SetOverlay(nullptr);
        dialog_.reset();
    }
    if (play_)
    {
        RetirePlay();
    }
    retiring_.clear(); // each waits for its thread: an engine thinking finishes its move first
}

bool MainController::IsPlaying() const
{
    return play_ != nullptr && !snapshot_.is_finished;
}

bool MainController::IsPersonToMove() const
{
    return IsPlaying() && snapshot_.is_human_to_move && view_ply_ == game_.PlyCount();
}

void MainController::ShowGame(chess::Game game, GameInfo info)
{
    if (play_)
    {
        RetirePlay();
    }
    is_following_competition_ = false;
    trackers_.clear();
    game_ = std::move(game);
    info_ = std::move(info);
    source_pgn_.reset();
    file_name_.clear();
    view_ply_ = game_.PlyCount();
    evals_.assign(static_cast<std::size_t>(game_.PlyCount()), std::nullopt);
    view_.board().SetBanner({}, {});
    Refresh();
}

void MainController::ShowPly(int ply)
{
    view_ply_ = std::clamp(ply, 0, game_.PlyCount());
    Refresh();
}

void MainController::Command(int command)
{
    switch (command)
    {
    case kFirst:
        ShowPly(0);
        break;
    case kBack:
        ShowPly(view_ply_ - 1);
        break;
    case kForward:
        ShowPly(view_ply_ + 1);
        break;
    case kLast:
        ShowPly(game_.PlyCount());
        break;
    case kFlip:
        is_flipped_ = !is_flipped_;
        view_.SetFlipped(is_flipped_);
        break;
    case kClearLog:
        view_.log().Clear();
        break;
    case kWindowSize50:
    case kWindowSize75:
    case kWindowSize100:
    case kWindowSize125:
    case kWindowSize150:
        if (services_.choose_window_size)
        {
            services_.choose_window_size(kWindowSizes[static_cast<std::size_t>(command - kWindowSize50)]);
        }
        break;
    case kWindowLarger:
    case kWindowSmaller:
        if (services_.window_sizes && services_.choose_window_size)
        {
            const WindowSizeMenu sizes = services_.window_sizes();
            if (const int percent = command == kWindowLarger ? sizes.larger : sizes.smaller; percent != 0)
            {
                services_.choose_window_size(percent);
            }
        }
        break;
    case kNewGame:
        OpenNewGame();
        break;
    case kStopGame:
        StopGame();
        break;
    case kTakeBack:
        TakeBack();
        break;
    case kAnalyse:
        SetAnalysing(analysis_ == nullptr);
        break;
    case kEngineManager:
        OpenEngineManager();
        break;
    case kNewMatch:
        OpenCompetition(false);
        break;
    case kNewTournament:
        OpenCompetition(true);
        break;
    case kStopMatch:
    case kStopTournament:
        if (competition_)
        {
            competition_->Stop();
        }
        break;
    case kShowStandings:
        OpenStandings();
        break;
    case kAbout:
        OpenAbout();
        break;
    case kOpenPgn: {
        reckless::view::FileRequest request;
        request.title    = "Open a PGN file";
        request.patterns = {"*.pgn"};
        services_.choose_file(request, [this](const std::string &path) {
            if (!path.empty())
            {
                OpenPgn(path);
            }
        });
        break;
    }
    case kSavePgn: {
        reckless::view::FileRequest request;
        request.title     = "Save the game as PGN";
        request.patterns  = {"*.pgn"};
        request.save      = true;
        request.file_name = "game.pgn";
        services_.choose_file(request, [this](const std::string &path) {
            if (!path.empty())
            {
                SavePgn(path);
            }
        });
        break;
    }
    case kCopyFen:
        if (!services_.write_clipboard(game_.PositionAt(view_ply_).ToFen()))
        {
            error_ = "The clipboard would not take the FEN";
        }
        Refresh();
        break;
    case kCopyPgn:
        if (!services_.write_clipboard(chess::WritePgn(CurrentPgn())))
        {
            error_ = "The clipboard would not take the PGN";
        }
        Refresh();
        break;
    case kPasteFen:
        if (!Paste(services_.read_clipboard()))
        {
            error_ = "The clipboard holds no FEN and no PGN game";
            Refresh();
        }
        break;
    case kQuit:
        if (services_.quit)
        {
            services_.quit();
        }
        break;
    default:
        if (command > kWatchGameBase && competition_)
        {
            watched_                  = command - kWatchGameBase;
            is_following_competition_ = true;
            PollCompetition();
        }
        break;
    }
}

void MainController::BoardMove(chess::Move move)
{
    if (IsPlaying())
    {
        if (IsPersonToMove() && play_->SubmitHumanMove(move))
        {
            // Shown at once; the game's own record replaces it as the runner takes the move.
            game_.Play(move);
            evals_.resize(static_cast<std::size_t>(game_.PlyCount()));
            view_ply_ = game_.PlyCount();
            Refresh();
        }
        return;
    }
    // No game is played: the move takes the game shown on from the position shown, cutting what followed it.
    chess::Game next(game_.StartPosition());
    for (int ply = 0; ply < view_ply_; ++ply)
    {
        next.Play(game_.Moves()[static_cast<std::size_t>(ply)]);
    }
    if (!next.Play(move))
    {
        return;
    }
    game_ = std::move(next);
    evals_.resize(static_cast<std::size_t>(game_.PlyCount()));
    view_ply_ = game_.PlyCount();
    view_.board().SetBanner({}, {});
    Refresh();
}

void MainController::StartGame(PlaySettings settings)
{
    if (play_)
    {
        RetirePlay();
    }
    is_following_competition_ = false;
    file_name_.clear();
    trackers_.clear();
    chess::Game start;
    if (!settings.start_fen.empty())
    {
        if (auto position = chess::Position::FromFen(settings.start_fen, chess::FenMode::kTolerant))
        {
            start = chess::Game(*position);
        }
    }
    for (const std::string &move : settings.start_moves)
    {
        start.PlayUci(move);
    }
    std::vector<std::optional<double>> kept(
        evals_.begin(),
        evals_.begin() + std::min<std::ptrdiff_t>(start.PlyCount(), static_cast<std::ptrdiff_t>(evals_.size())));
    game_  = start;
    evals_ = std::move(kept);
    evals_.resize(static_cast<std::size_t>(game_.PlyCount()));
    view_ply_ = game_.PlyCount();

    info_               = GameInfo{};
    info_.white         = settings.white.Name();
    info_.black         = settings.black.Name();
    info_.white_details = DetailsOf(true, settings.white);
    info_.black_details = DetailsOf(false, settings.black);
    info_.event         = settings.event;
    info_.time_control  = TimeControlText(settings.time_control);
    for (const SideSpec *side : {&settings.white, &settings.black})
    {
        if (!side->is_human && !side->engine.book_path.empty())
        {
            info_.book += (info_.book.empty() ? "" : ", ") +
                          std::filesystem::path(side->engine.book_path).filename().string() + " (" +
                          side->engine.DisplayName() + ")";
        }
    }
    // The person plays from the bottom of the board.
    if (settings.white.is_human != settings.black.is_human)
    {
        is_flipped_ = settings.black.is_human;
    }
    view_.board().SetBanner({}, {});
    error_.clear();
    play_         = std::make_unique<PlaySession>(std::move(settings));
    play_version_ = -1;
    snapshot_     = play_->Snapshot();
    Refresh();
}

void MainController::RetirePlay()
{
    play_->Stop();
    retiring_.push_back(std::move(play_));
    snapshot_             = PlaySnapshot{};
    snapshot_.is_finished = true;
}

void MainController::StopGame()
{
    if (!IsPlaying())
    {
        return;
    }
    RetirePlay();
    for (auto &[name, tracker] : trackers_)
    {
        tracker.End();
    }
    view_.board().SetBanner("Stopped", "Game abandoned");
    Refresh();
}

void MainController::TakeBack()
{
    if (!IsPlaying())
    {
        if (game_.PlyCount() > 0)
        {
            game_.Undo();
            evals_.resize(static_cast<std::size_t>(game_.PlyCount()));
            view_ply_ = game_.PlyCount();
            view_.board().SetBanner({}, {});
            Refresh();
        }
        return;
    }
    const PlaySettings &settings = play_->settings();
    if (!settings.white.is_human && !settings.black.is_human)
    {
        return;
    }
    const PlaySnapshot snapshot = play_->Snapshot();
    const chess::Color mover    = snapshot.game.CurrentPosition().SideToMove();
    const bool         is_both  = settings.white.is_human && settings.black.is_human;
    const chess::Color person   = settings.white.is_human ? chess::kWhite : chess::kBlack;
    // The person's last move goes, and the engine's reply to it when it has made one.
    const int undo = is_both ? 1 : (mover == person ? 2 : 1);
    const int keep = snapshot.game.PlyCount() - undo;
    // The opening given before play began stays; the moves played, a take-back before this one's among them, may go.
    const std::size_t opening = settings.opening_length.value_or(settings.start_moves.size());
    if (keep < static_cast<int>(opening) || keep < 0)
    {
        return;
    }
    PlaySettings resumed   = settings;
    resumed.opening_length = opening;
    const auto moves       = snapshot.game.UciMoves();
    resumed.start_moves.assign(moves.begin(), moves.begin() + keep);
    // Both clocks as they were recorded at the position the game goes back to, and every reading before it kept for
    // the take-back after this one.
    const auto kept_clocks = static_cast<std::size_t>(keep) + 1;
    if (!settings.time_control.sessions.empty() && snapshot.clocks.size() >= kept_clocks)
    {
        const ClockReading &at = snapshot.clocks[static_cast<std::size_t>(keep)];
        resumed.white_resumed =
            ResumedTimeControl(settings.time_control, at.white_remaining, at.white_moves_to_go, at.white_increment);
        resumed.black_resumed =
            ResumedTimeControl(settings.time_control, at.black_remaining, at.black_moves_to_go, at.black_increment);
        resumed.resumed_clocks.assign(snapshot.clocks.begin(),
                                      snapshot.clocks.begin() + static_cast<std::ptrdiff_t>(kept_clocks));
    }
    const GameInfo info = info_;
    const bool     flip = is_flipped_;
    StartGame(std::move(resumed));
    info_       = info;
    is_flipped_ = flip;
    Refresh();
}

void MainController::SetAnalysing(bool is_on)
{
    if (!is_on)
    {
        analysis_.reset();
        view_.SetAnalysing(false);
        Refresh();
        return;
    }
    if (analysis_)
    {
        return;
    }
    std::vector<EngineEntry> engines;
    for (const EngineEntry &entry : catalogue_.engines)
    {
        if (entry.is_analysing)
        {
            engines.push_back(entry);
        }
    }
    if (engines.empty())
    {
        engines = catalogue_.engines;
    }
    if (engines.empty())
    {
        error_ = "No engine to analyse with: add one under Engines > Manage engines";
        view_.SetAnalysing(false);
        Refresh();
        return;
    }
    error_.clear();
    analysis_ = std::make_unique<AnalysisSession>(engines, kAnalysisLines, reaper_);
    analysis_->SetPosition(game_, view_ply_);
    view_.SetAnalysing(true);
    Refresh();
}

void MainController::ShowPgnGame(const chess::PgnGame &game)
{
    ShowGame(game.ToGame(), InfoFromPgn(game));
    source_pgn_ = game;
}

void MainController::OpenPgn(const std::string &path)
{
    const auto text = match::ReadTextFile(path);
    if (!text)
    {
        error_ = "The file could not be read: " + text.ErrorMessage();
        Refresh();
        return;
    }
    std::vector<std::string>    errors;
    std::vector<chess::PgnGame> games = chess::ReadPgn(*text, &errors);
    if (games.empty())
    {
        error_ = "The file holds no game that could be read" + (errors.empty() ? "" : ": " + errors.front());
        Refresh();
        return;
    }
    error_.clear();
    const std::string name = std::filesystem::path(path).filename().string();
    if (games.size() == 1)
    {
        ShowPgnGame(games.front());
        file_name_ = name;
        return;
    }
    auto dialog      = std::make_unique<GamePickerDialog>(std::move(games), name, errors.size());
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_open  = [this, name](const chess::PgnGame &game) {
        const chess::PgnGame picked = game; // the dialog goes with the close
        CloseDialog();
        ShowPgnGame(picked);
        file_name_ = name;
    };
    OpenDialog(std::move(dialog));
}

chess::PgnGame MainController::CurrentPgn() const
{
    return PgnOf(game_, info_, source_pgn_, TodayInPgn());
}

bool MainController::SavePgn(const std::string &path)
{
    const std::string text = chess::WritePgn(CurrentPgn());
    std::ofstream     file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file)
    {
        error_ = "The game could not be written to " + path;
        Refresh();
        return false;
    }
    error_.clear();
    file_name_ = std::filesystem::path(path).filename().string();
    Refresh();
    return true;
}

std::string MainController::WindowTitle() const
{
    std::string title = info_.white + " vs " + info_.black;
    if (!file_name_.empty())
    {
        title += " - " + file_name_;
    }
    return title + " - Reckless UCI Host";
}

bool MainController::Paste(const std::string &text)
{
    PastedGames pasted = ReadPasted(text);
    if (pasted.position)
    {
        error_.clear();
        ShowGame(chess::Game(*pasted.position), GameInfo{});
        return true;
    }
    if (pasted.games.empty())
    {
        return false;
    }
    error_.clear();
    if (pasted.games.size() == 1)
    {
        ShowPgnGame(pasted.games.front());
        return true;
    }
    auto dialog      = std::make_unique<GamePickerDialog>(std::move(pasted.games), "the clipboard", pasted.skipped);
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_open  = [this](const chess::PgnGame &game) {
        const chess::PgnGame picked = game;
        CloseDialog();
        ShowPgnGame(picked);
    };
    OpenDialog(std::move(dialog));
    return true;
}

void MainController::OpenCompetition(bool is_tournament)
{
    auto dialog      = std::make_unique<CompetitionDialog>(is_tournament, catalogue_.engines);
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_start = [this, is_tournament](const cli::TournamentOptions &options) {
        match::TournamentSettings settings = options.settings;
        CloseDialog();
        StartCompetition(std::move(settings), !is_tournament);
    };
    OpenDialog(std::move(dialog));
}

void MainController::OpenStandings()
{
    if (!competition_)
    {
        return;
    }
    auto dialog      = std::make_unique<StandingsDialog>([this] {
        return competition_ ? std::make_pair(competition_->EngineNames(), competition_->Results())
                            : std::make_pair(std::vector<std::string>{}, std::vector<match::GameResultEntry>{});
    });
    dialog->on_close = [this] { CloseDialog(); };
    OpenDialog(std::move(dialog));
}

void MainController::OpenAbout()
{
    AboutInfo info;
    info.version = VersionText();
    if (!files_.bundled_engines.empty())
    {
        // Every engine the install carries whose program is there, whatever this machine's processor runs.
        constexpr CpuLevels kEveryLevel{true, true, true};
        for (const BundledEngine &engine : ReadBundledEngines(files_.bundled_engines, kEveryLevel))
        {
            info.engines.push_back({engine.name, engine.version, engine.licence});
        }
    }
    auto dialog      = std::make_unique<AboutDialog>(std::move(info), view_.board().pieces());
    dialog->open_url = services_.open_url;
    dialog->on_close = [this] { CloseDialog(); };
    OpenDialog(std::move(dialog));
}

void MainController::RetireCompetition()
{
    if (!competition_)
    {
        return;
    }
    competition_->Stop();
    std::shared_ptr<CompetitionSession> retiring(competition_.release());
    reaper_.Later([retiring]() mutable { retiring.reset(); });
    is_following_competition_ = false;
}

void MainController::StartCompetition(match::TournamentSettings settings, bool is_match)
{
    if (play_)
    {
        RetirePlay();
    }
    RetireCompetition();
    competition_ = std::make_unique<CompetitionSession>(std::move(settings), is_match);
    if (!competition_->start_error().empty())
    {
        error_ = (is_match ? "The match could not start: " : "The tournament could not start: ") +
                 competition_->start_error();
        Refresh();
        return;
    }
    error_.clear();
    is_following_competition_ = true;
    file_name_.clear();
    watched_       = 0;
    watched_shown_ = 0;
    competition_trackers_.clear();
    view_.board().SetBanner({}, {});
    PollCompetition();
}

void MainController::PollCompetition()
{
    if (!competition_)
    {
        return;
    }
    const bool  is_changed = competition_->Poll();
    const auto &live       = competition_->live();
    int         shown      = 0;
    if (watched_ != 0 && live.count(watched_) != 0)
    {
        shown = watched_;
    }
    else if (!live.empty())
    {
        shown = live.begin()->first;
    }
    for (const CompetitionLogLine &line : competition_->DrainLog())
    {
        if (line.game_number != 0 && (!is_following_competition_ || line.game_number != shown))
        {
            continue;
        }
        const bool is_to_engine = line.direction == uci::LineDirection::kToEngine;
        view_.log().Add(line.engine,
                        is_to_engine                                        ? LogDirection::kToEngine
                        : line.direction == uci::LineDirection::kFromEngine ? LogDirection::kFromEngine
                                                                            : LogDirection::kError,
                        line.text);
        if (line.game_number != 0)
        {
            competition_trackers_.try_emplace(line.engine, line.engine)
                .first->second.Feed(is_to_engine, line.text, line.at);
        }
    }
    if (!is_following_competition_ || (!is_changed && shown == watched_shown_))
    {
        return;
    }
    if (shown != watched_shown_)
    {
        competition_trackers_.clear();
        watched_shown_ = shown;
    }
    const int total    = competition_->total_games();
    info_              = GameInfo{};
    info_.score        = competition_->ScoreText();
    info_.time_control = TimeControlText(competition_->settings().time_control);
    const char *kind   = competition_->is_match() ? "Engine match" : "Engine tournament";
    if (shown != 0)
    {
        const LiveGame &game = live.at(shown);
        info_.white          = game.white_name;
        info_.black          = game.black_name;
        info_.white_details  = "White Â· engine";
        info_.black_details  = "Black Â· engine";
        info_.event          = std::string(kind) + ", game " + std::to_string(shown) + " of " + std::to_string(total);
        const bool was_following = view_ply_ >= game_.PlyCount();
        game_                    = game.game ? *game.game : chess::Game{};
        evals_.assign(static_cast<std::size_t>(game_.PlyCount()), std::nullopt);
        for (std::size_t ply = 0; ply < game.notes.size() && ply < evals_.size(); ++ply)
        {
            if (const auto &score = game.notes[ply].score)
            {
                evals_[ply] = WhitePawns(*score, game_.PositionAt(static_cast<int>(ply)).SideToMove());
            }
        }
        if (was_following || view_ply_ > game_.PlyCount())
        {
            view_ply_ = game_.PlyCount();
        }
        source_pgn_.reset();
    }
    else if (!competition_->finished().empty())
    {
        const auto &record = competition_->finished().back();
        info_.white        = record->white_name;
        info_.black        = record->black_name;
        info_.event        = std::string(kind) + (competition_->IsFinished() ? ", finished" : "");
        game_              = record->game;
        evals_.assign(static_cast<std::size_t>(game_.PlyCount()), std::nullopt);
        view_ply_   = game_.PlyCount();
        source_pgn_ = record->ToPgn();
        if (competition_->IsFinished())
        {
            view_.board().SetBanner("Finished", info_.score.empty() ? std::string(kind) + " over" : info_.score);
        }
    }
    Refresh();
}

void MainController::OpenEngineManager()
{
    auto dialog      = std::make_unique<EngineManagerDialog>(catalogue_);
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_save  = [this](const EngineCatalogue &edited) {
        catalogue_ = edited;
        if (!files_.engines.empty() && !catalogue_.Save(files_.engines))
        {
            error_ = "The engine list could not be written to " + files_.engines.string();
        }
        else
        {
            error_.clear();
        }
        CloseDialog();
        Refresh();
    };
    OpenDialog(std::move(dialog));
}

void MainController::OpenNewGame()
{
    auto dialog      = std::make_unique<NewGameDialog>(catalogue_.engines, game_.PlyCount() > 0 || view_ply_ > 0 ||
                                                                               !game_.StartsFromStandardPosition());
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_start = [this](const PlaySettings &chosen, bool from_shown) {
        PlaySettings settings = chosen;
        if (from_shown)
        {
            if (!game_.StartsFromStandardPosition())
            {
                settings.start_fen = game_.StartPosition().ToFen();
            }
            const auto moves = game_.UciMoves();
            settings.start_moves.assign(moves.begin(), moves.begin() + view_ply_);
        }
        CloseDialog();
        StartGame(std::move(settings));
    };
    OpenDialog(std::move(dialog));
}

void MainController::OpenDialog(std::unique_ptr<DialogFrame> dialog)
{
    if (dialog_)
    {
        CloseDialog();
    }
    dialog_ = std::move(dialog);
    view_.SetOverlay(dialog_.get());
    dialog_->GrabKeyboardFocus();
}

void MainController::CloseDialog()
{
    if (!dialog_)
    {
        return;
    }
    view_.SetOverlay(nullptr);
    closed_dialogs_.push_back(std::move(dialog_));
    view_.GrabKeyboardFocus();
}

void MainController::SetUpdateChecker(std::unique_ptr<reckless::update::Checker>  checker,
                                      std::function<void(const std::string &url)> open_url)
{
    updates_     = std::move(checker);
    open_url_    = std::move(open_url);
    menus_shown_ = std::nullopt; // the bar set again, with its Updates or without
    RefreshMenuBar();
}

void MainController::Frame(double seconds)
{
    if (updates_)
    {
        // About once a second, as the check asks: it starts one when one is due, the window being on a screen.
        update_poll_ += seconds;
        if (update_poll_ >= 1.0)
        {
            update_poll_ = 0.0;
            updates_->Poll(true);
        }
    }
    closed_dialogs_.clear();
    if (dialog_)
    {
        dialog_->Frame();
    }
    PollPlay();
    PollCompetition();
    if (analysis_)
    {
        analysis_->SetPosition(game_, view_ply_);
        analysis_->Poll(Clock::now());
        for (const AnalysisLogLine &line : analysis_->DrainLog())
        {
            view_.log().Add(line.engine,
                            line.direction == uci::LineDirection::kToEngine     ? LogDirection::kToEngine
                            : line.direction == uci::LineDirection::kFromEngine ? LogDirection::kFromEngine
                                                                                : LogDirection::kError,
                            line.text);
        }
    }
    reaper_.Collect();
    retiring_.erase(std::remove_if(retiring_.begin(), retiring_.end(),
                                   [](const std::unique_ptr<PlaySession> &session) { return session->IsFinished(); }),
                    retiring_.end());
    view_.log().Fit(); // the face measured by the window, which a first layout may come before
    RefreshMenuBar();
    RefreshStrips();
    RefreshSearches();
}

void MainController::PollPlay()
{
    if (!play_)
    {
        return;
    }
    for (const PlayLogLine &line : play_->DrainLog())
    {
        const bool is_to_engine = line.direction == uci::LineDirection::kToEngine;
        view_.log().Add(line.engine,
                        is_to_engine                                        ? LogDirection::kToEngine
                        : line.direction == uci::LineDirection::kFromEngine ? LogDirection::kFromEngine
                                                                            : LogDirection::kError,
                        line.text);
        auto tracker = trackers_.try_emplace(line.engine, line.engine).first;
        tracker->second.Feed(is_to_engine, line.text, line.at);
    }
    const int version = play_->Version();
    if (version == play_version_)
    {
        return;
    }
    play_version_            = version;
    const bool was_following = view_ply_ >= game_.PlyCount();
    snapshot_                = play_->Snapshot();
    game_                    = snapshot_.game;
    evals_.resize(static_cast<std::size_t>(game_.PlyCount()));
    for (std::size_t ply = 0; ply < snapshot_.notes.size() && ply < evals_.size(); ++ply)
    {
        if (const auto &score = snapshot_.notes[ply].score)
        {
            evals_[ply] = WhitePawns(*score, game_.PositionAt(static_cast<int>(ply)).SideToMove());
        }
    }
    if (was_following || view_ply_ > game_.PlyCount())
    {
        view_ply_ = game_.PlyCount();
    }
    if (snapshot_.is_finished)
    {
        for (auto &[name, tracker] : trackers_)
        {
            tracker.End();
        }
        const auto [title, detail] = BannerOf(game_.GetOutcome());
        view_.board().SetBanner(title, detail);
        if (const auto record = play_->Record())
        {
            source_pgn_ = record->ToPgn(); // the game as played: its clocks, scores and depths as comments
        }
    }
    Refresh();
}

void MainController::RefreshStrips()
{
    PlayerStripState white;
    white.is_white = true;
    white.name     = info_.white;
    white.details  = info_.white_details.empty() ? "White" : info_.white_details;
    PlayerStripState black;
    black.is_white = false;
    black.name     = info_.black;
    black.details  = info_.black_details.empty() ? "Black" : info_.black_details;
    if (play_ && play_->settings().time_control.IsTimed() && !play_->settings().time_control.sessions.empty())
    {
        const chess::Color mover   = snapshot_.game.CurrentPosition().SideToMove();
        const bool         running = IsPlaying();
        const auto elapsed    = std::chrono::duration_cast<chess::Milliseconds>(Clock::now() - snapshot_.turn_started);
        auto       white_left = snapshot_.white_remaining;
        auto       black_left = snapshot_.black_remaining;
        if (running)
        {
            (mover == chess::kWhite ? white_left : black_left) -= elapsed;
        }
        white.clock         = ClockText(white_left.count());
        black.clock         = ClockText(black_left.count());
        white.is_running    = running && mover == chess::kWhite;
        black.is_running    = running && mover == chess::kBlack;
        const auto &outcome = snapshot_.game.GetOutcome();
        white.has_lost      = !running && outcome.result == chess::GameResult::kBlackWins;
        black.has_lost      = !running && outcome.result == chess::GameResult::kWhiteWins;
    }
    view_.SetPlayers(white, black);
}

void MainController::RefreshSearches()
{
    const auto               now         = Clock::now();
    bool                     graph_dirty = false;
    const chess::Position   &shown       = game_.PositionAt(view_ply_);
    std::vector<EngineBlock> blocks;
    std::vector<BoardArrow>  arrows;
    std::vector<std::string> status;
    status.push_back(shown.SideToMove() == chess::kWhite ? "White to move" : "Black to move");
    if (analysis_)
    {
        std::string names;
        for (const std::string &name : analysis_->Names())
        {
            names += (names.empty() ? "" : ", ") + name;
        }
        status.push_back("Analysing with " + names);
        for (const SearchTracker *tracker : analysis_->Trackers())
        {
            blocks.push_back(tracker->Block(now, kAnalysisLines));
            if (tracker->position() != shown)
            {
                continue;
            }
            if (arrows.empty())
            {
                if (const auto best = tracker->BestMove())
                {
                    arrows.push_back({best->From(), best->To(), theme::kAmber.WithAlpha(0.85f)});
                }
            }
            // The first engine's word on the position shown is the graph's for it.
            if (const auto pawns = tracker->BestWhitePawns();
                pawns && view_ply_ > 0 && blocks.size() == 1 && view_ply_ <= static_cast<int>(evals_.size()))
            {
                auto &slot = evals_[static_cast<std::size_t>(view_ply_ - 1)];
                if (slot != pawns)
                {
                    slot        = pawns;
                    graph_dirty = true;
                }
            }
        }
        for (const std::string &failure : analysis_->Failures())
        {
            status.push_back(failure);
        }
    }
    for (const SideSpec *side : play_
                                    ? std::vector<const SideSpec *>{&play_->settings().white, &play_->settings().black}
                                    : std::vector<const SideSpec *>{})
    {
        if (side->is_human)
        {
            continue;
        }
        const auto found = trackers_.find(side->engine.DisplayName());
        if (found == trackers_.end())
        {
            continue;
        }
        const SearchTracker &tracker = found->second;
        blocks.push_back(tracker.Block(now, 3));
        if (tracker.IsSearching())
        {
            status.push_back(tracker.name() + (tracker.IsPondering() ? " pondering" : " searching"));
        }
        if (arrows.empty() && tracker.IsSearching() && !tracker.IsPondering() && tracker.position() == shown)
        {
            if (const auto best = tracker.BestMove())
            {
                arrows.push_back({best->From(), best->To(), theme::kAmber.WithAlpha(0.85f)});
            }
        }
    }
    if (IsPersonToMove())
    {
        status.push_back("Your move");
    }
    if (game_.IsOver() && view_ply_ == game_.PlyCount())
    {
        status.push_back(chess::ResultText(game_.GetOutcome().result) + "  " + game_.GetOutcome().description);
    }
    if (!error_.empty())
    {
        status.push_back(error_);
    }
    if (updates_)
    {
        if (const auto release = updates_->available())
        {
            status.push_back("UPDATE: version " + reckless::update::VersionText(release->version) +
                             " is out (Help > Updates)");
        }
    }
    view_.SetAnalysis(std::move(blocks));
    view_.SetArrows(std::move(arrows));
    view_.SetStatus(std::move(status));
    if (graph_dirty)
    {
        view_.SetEval(evals_, view_ply_, EvalCaption());
    }
}

void MainController::RefreshMenuBar()
{
    MenuState state = Menus();
    if (menus_shown_ && *menus_shown_ == state)
    {
        return;
    }
    menu_bar_ = BuildMenuBar(state);
    if (updates_)
    {
        // Help > Updates: the toolkit's rows, each choosing through its own action.
        reckless::app::MenuEntry &help = menu_bar_.menus.back();
        help.submenu.push_back(reckless::app::MenuEntry::Separator());
        help.submenu.push_back(reckless::app::UpdatesMenuEntry(*updates_, open_url_));
    }
    menus_shown_ = std::move(state);
    services_.set_menu_bar(menu_bar_, [this](int id) { Command(id); });
}

MenuState MainController::Menus() const
{
    MenuState state;
    state.is_dialog_open   = dialog_ != nullptr;
    state.updates_revision = updates_ ? updates_->revision() : 0;
    if (services_.window_sizes)
    {
        state.window_sizes = services_.window_sizes();
    }
    state.can_step_back    = view_ply_ > 0;
    state.can_step_forward = view_ply_ < game_.PlyCount();
    state.is_game_running  = IsPlaying();
    state.has_engines      = !catalogue_.engines.empty();
    if (competition_)
    {
        const bool running          = !competition_->IsFinished();
        state.is_match_running      = running && competition_->is_match();
        state.is_tournament_running = running && !competition_->is_match();
        state.has_standings         = !competition_->is_match() && !competition_->Results().empty();
        for (const auto &[number, game] : competition_->live())
        {
            state.watchable_games.emplace_back(number, "Game " + std::to_string(number) + ": " + game.white_name +
                                                           " - " + game.black_name);
        }
    }
    if (IsPlaying())
    {
        const PlaySettings &settings = play_->settings();
        state.can_take_back =
            (settings.white.is_human || settings.black.is_human) &&
            game_.PlyCount() > static_cast<int>(settings.opening_length.value_or(settings.start_moves.size()));
    }
    else
    {
        state.can_take_back = game_.PlyCount() > 0;
    }
    return state;
}

void MainController::Refresh()
{
    const chess::Position     &position = game_.PositionAt(view_ply_);
    std::optional<chess::Move> last;
    if (view_ply_ > 0)
    {
        last = game_.Moves()[static_cast<std::size_t>(view_ply_ - 1)];
    }
    view_.SetBoard(position, last, IsPersonToMove() || (!IsPlaying() && position.GenerateLegalMoves().size() > 0));
    view_.SetFlipped(is_flipped_);
    const chess::Position &start = game_.StartPosition();
    view_.SetMoves(game_.SanMoves(), start.FullmoveNumber(), start.SideToMove() == chess::kBlack, view_ply_);
    RefreshStrips();

    std::vector<CardLine> card;
    if (!info_.event.empty())
    {
        card.push_back({"Event", info_.event});
    }
    if (!info_.opening.empty())
    {
        card.push_back({"Opening", info_.opening});
    }
    if (!info_.time_control.empty())
    {
        card.push_back({"Time control", info_.time_control});
    }
    if (!info_.book.empty())
    {
        card.push_back({"Book", info_.book});
    }
    if (!info_.score.empty())
    {
        card.push_back({competition_ && !competition_->is_match() ? "Standing" : "Match score", info_.score});
    }
    card.push_back({"FEN", position.ToFen(), true});
    view_.SetCard(std::move(card));

    view_.SetEval(evals_, view_ply_, EvalCaption());
    RefreshSearches();
}

std::string MainController::EvalCaption() const
{
    if (view_ply_ <= 0)
    {
        return {};
    }
    const chess::Position &before  = game_.PositionAt(view_ply_ - 1);
    std::string            caption = std::to_string(before.FullmoveNumber()) +
                                     (before.SideToMove() == chess::kWhite ? "." : "...") + game_.San(view_ply_ - 1);
    if (view_ply_ <= static_cast<int>(evals_.size()) && evals_[static_cast<std::size_t>(view_ply_ - 1)])
    {
        char text[32];
        std::snprintf(text, sizeof text, "  %+.2f", *evals_[static_cast<std::size_t>(view_ply_ - 1)]);
        caption += text;
    }
    return caption;
}

} // namespace uci_host::app
