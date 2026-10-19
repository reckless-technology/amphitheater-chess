#include "MainController.h"

#include "Commands.h"
#include "Controls.h"
#include "EngineManagerDialog.h"
#include "GameFiles.h"
#include "GamePickerDialog.h"
#include "NewGameDialog.h"
#include "Theme.h"
#include "match/Openings.h"

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
    }
    else
    {
        error_ = "The engine list could not be read: " + error;
    }
    view_.on_command    = [this](int command) { Command(command); };
    view_.on_select_ply = [this](int ply) { ShowPly(ply); };
    view_.on_move       = [this](chess::Move move) { BoardMove(move); };
    view_.SetMenus(BuildMenus([this] { return Menus(); }));
    Refresh();
}

MainController::~MainController()
{
    analysis_.reset();
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
    trackers_.clear();
    game_ = std::move(game);
    info_ = std::move(info);
    source_pgn_.reset();
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
    if (keep < static_cast<int>(settings.start_moves.size()) || keep < 0)
    {
        return;
    }
    PlaySettings resumed = settings;
    const auto   moves   = snapshot.game.UciMoves();
    resumed.start_moves.assign(moves.begin(), moves.begin() + keep);
    if (!settings.time_control.sessions.empty())
    {
        resumed.white_resumed = ResumedTimeControl(settings.time_control, snapshot.white_remaining,
                                                   snapshot.white_moves_to_go, snapshot.white_increment);
        resumed.black_resumed = ResumedTimeControl(settings.time_control, snapshot.black_remaining,
                                                   snapshot.black_moves_to_go, snapshot.black_increment);
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
    if (games.size() == 1)
    {
        ShowPgnGame(games.front());
        return;
    }
    auto dialog = std::make_unique<GamePickerDialog>(std::move(games), std::filesystem::path(path).filename().string(),
                                                     errors.size());
    dialog->on_close = [this] { CloseDialog(); };
    dialog->on_open  = [this](const chess::PgnGame &game) {
        const chess::PgnGame picked = game; // the dialog goes with the close
        CloseDialog();
        ShowPgnGame(picked);
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
    Refresh();
    return true;
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

void MainController::Frame(double seconds)
{
    static_cast<void>(seconds);
    closed_dialogs_.clear();
    if (dialog_)
    {
        dialog_->Frame();
    }
    PollPlay();
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
    view_.SetAnalysis(std::move(blocks));
    view_.SetArrows(std::move(arrows));
    view_.SetStatus(std::move(status));
    if (graph_dirty)
    {
        view_.SetEval(evals_, view_ply_, EvalCaption());
    }
}

MenuState MainController::Menus() const
{
    MenuState state;
    state.can_step_back    = view_ply_ > 0;
    state.can_step_forward = view_ply_ < game_.PlyCount();
    state.is_game_running  = IsPlaying();
    state.has_engines      = !catalogue_.engines.empty();
    if (IsPlaying())
    {
        const PlaySettings &settings = play_->settings();
        state.can_take_back          = (settings.white.is_human || settings.black.is_human) &&
                                       game_.PlyCount() > static_cast<int>(settings.start_moves.size());
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
