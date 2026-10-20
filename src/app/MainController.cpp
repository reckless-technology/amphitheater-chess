#include "MainController.h"

#include "AboutDialog.h"
#include "BundledEngines.h"
#include "Commands.h"
#include "CompetitionDialog.h"
#include "Controls.h"
#include "EngineManagerDialog.h"
#include "GameFiles.h"
#include "GamePickerDialog.h"
#include "LayoutSettings.h"
#include "NewGameDialog.h"
#include "PositionEditor.h"
#include "StandingsDialog.h"
#include "Theme.h"
#include "match/Openings.h"
#include "version/Version.h"

#include "AppUpdates.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace amphitheater::app
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
    const LayoutSettings layout = LayoutSettings::Load(files_.layout);
    view_.SetLogLayout(layout.log_height, layout.is_log_open);
    view_.on_log_layout = [this] {
        SaveLayout();
        RefreshMenuBar();
    };
    view_.on_command    = [this](int command) { Command(command); };
    view_.on_select_ply = [this](int ply) { ShowPly(ply); };
    view_.on_variation  = [this](int id) {
        // The variation entered at its first move.
        ShowLineThrough(id, tree_.node(id).ply);
    };
    view_.on_move = [this](chess::Move move) { BoardMove(move); };
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
    game_series_.clear();
    analysis_series_.clear(); // another game: what an analysis said was of the last one
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
    case kPromoteVariation:
        if (const chess::GameTree::NodeId variation = ShownVariation();
            variation != chess::GameTree::kNone && !IsPlaying())
        {
            tree_.Promote(variation);
            ShowLine(line_, view_ply_);
        }
        break;
    case kDeleteVariation:
        if (const chess::GameTree::NodeId variation = ShownVariation();
            variation != chess::GameTree::kNone && !IsPlaying())
        {
            // The line it left shown again, at the position the variation left it.
            const chess::GameTree::NodeId parent = tree_.node(variation).parent;
            const int                     ply    = tree_.node(parent).ply;
            tree_.Remove(variation);
            ShowLine(tree_.LineThrough(parent), ply);
        }
        break;
    case kShowLog:
        view_.ToggleLog();
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
    case kSetUpPosition:
        OpenPositionEditor();
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

void MainController::ShowLineThrough(chess::GameTree::NodeId id, int ply)
{
    if (IsPlaying())
    {
        return;
    }
    ShowLine(tree_.LineThrough(id), ply);
}

chess::GameTree::NodeId MainController::ShownVariation() const
{
    for (int index = std::min(view_ply_, static_cast<int>(line_.size())) - 1; index >= 0; --index)
    {
        if (!tree_.IsMainContinuation(line_[static_cast<std::size_t>(index)]))
        {
            return line_[static_cast<std::size_t>(index)];
        }
    }
    for (std::size_t index = static_cast<std::size_t>(std::max(0, view_ply_)); index < line_.size(); ++index)
    {
        if (!tree_.IsMainContinuation(line_[index]))
        {
            return line_[index];
        }
    }
    return chess::GameTree::kNone;
}

bool MainController::LineMatchesGame() const
{
    if (static_cast<int>(line_.size()) != game_.PlyCount())
    {
        return false;
    }
    for (std::size_t index = 0; index < line_.size(); ++index)
    {
        if (!(tree_.node(line_[index]).move == game_.Moves()[index]))
        {
            return false;
        }
    }
    return tree_.start().ToFen() == game_.StartPosition().ToFen();
}

void MainController::MirrorGame()
{
    // A record's PGN where it is the game's - its comments, its NAGs and its variations kept - else the moves alone.
    tree_ = chess::GameTree::FromGame(game_);
    if (source_pgn_)
    {
        const chess::Game read = source_pgn_->ToGame();
        if (read.UciMoves() == game_.UciMoves() && read.StartPosition().ToFen() == game_.StartPosition().ToFen())
        {
            tree_ = chess::GameTree::FromPgn(*source_pgn_);
        }
    }
    line_         = tree_.MainLine();
    main_outcome_ = game_.GetOutcome();
}

void MainController::ShowLine(std::vector<chess::GameTree::NodeId> line, int ply)
{
    std::size_t shared = 0;
    while (shared < line.size() && shared < line_.size() && line[shared] == line_[shared])
    {
        ++shared;
    }
    line_ = std::move(line);
    game_ = tree_.GameOf(line_);
    // The main line to its end keeps how it ended where its record or its play says, not the rules alone.
    const std::vector<chess::GameTree::NodeId> main = tree_.MainLine();
    if (line_ == main && main_outcome_.IsOver() && !game_.IsOver())
    {
        game_.SetOutcome(main_outcome_);
    }
    // The scores of the moves both lines share stay; a score of a move the line no longer holds goes.
    for (auto *list : {&game_series_, &analysis_series_})
    {
        for (EvalSeries &series : *list)
        {
            series.points.resize(std::min(series.points.size(), shared));
        }
    }
    FitSeries();
    view_ply_ = std::clamp(ply, 0, game_.PlyCount());
    view_.board().SetBanner({}, {});
    Refresh();
}

std::vector<VariationRow> MainController::LineVariations() const
{
    std::vector<VariationRow> rows;
    for (std::size_t index = 0; index < line_.size(); ++index)
    {
        const chess::GameTree::NodeId parent = index == 0 ? chess::GameTree::kRoot : line_[index - 1];
        for (const chess::GameTree::NodeId other : tree_.node(parent).children)
        {
            if (other == line_[index])
            {
                continue;
            }
            // Its first moves, numbered from the position it leaves: "(12...a5 13.Nd5 Nxd5 ...)".
            const std::vector<chess::GameTree::NodeId> through = tree_.LineThrough(other);
            const std::size_t                          from    = index;
            constexpr std::size_t                      kShown  = 6;
            std::string                                text;
            for (std::size_t at = from; at < through.size() && at < from + kShown; ++at)
            {
                const chess::Position &before = tree_.node(tree_.node(through[at]).parent).position;
                if (before.SideToMove() == chess::kWhite)
                {
                    text += (text.empty() ? "" : " ") + std::to_string(before.FullmoveNumber()) + ".";
                }
                else if (at == from)
                {
                    text += std::to_string(before.FullmoveNumber()) + "...";
                }
                else
                {
                    text += ' ';
                }
                text += tree_.node(through[at]).san;
            }
            if (through.size() > from + kShown)
            {
                text += " \xe2\x80\xa6";
            }
            rows.push_back({static_cast<int>(index),
                            (tree_.IsMainContinuation(other) ? "main line: (" : "(") + text + ")", other});
        }
    }
    return rows;
}

void MainController::BoardMove(chess::Move move)
{
    if (IsPlaying())
    {
        if (IsPersonToMove() && play_->SubmitHumanMove(move))
        {
            // Shown at once; the game's own record replaces it as the runner takes the move.
            game_.Play(move);
            FitSeries();
            view_ply_ = game_.PlyCount();
            Refresh();
        }
        return;
    }
    // No game is played: the move goes on from the position shown - the line's next move, or a variation leaving the
    // line there, one already there entered and a new one added - and the line through it is shown.
    if (view_ply_ < static_cast<int>(line_.size()) &&
        tree_.node(line_[static_cast<std::size_t>(view_ply_)]).move == move)
    {
        ShowPly(view_ply_ + 1); // the line's own next move: on along the line shown
        return;
    }
    const chess::GameTree::NodeId parent =
        view_ply_ > 0 ? line_[static_cast<std::size_t>(view_ply_ - 1)] : chess::GameTree::kRoot;
    const chess::GameTree::NodeId child = tree_.Add(parent, move);
    if (child == chess::GameTree::kNone)
    {
        return;
    }
    ShowLine(tree_.LineThrough(child), view_ply_ + 1);
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
    // A game resumed from the one shown (a take-back) keeps the scores its opening moves had.
    game_ = start;
    FitSeries();
    opening_series_ = game_series_;
    view_ply_       = game_.PlyCount();

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
        // The line's last move taken off the game, and the line shown up to the move before.
        if (!line_.empty())
        {
            std::vector<chess::GameTree::NodeId> shorter = line_;
            tree_.Remove(shorter.back());
            shorter.pop_back();
            const int ply = static_cast<int>(shorter.size());
            ShowLine(std::move(shorter), ply);
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
    MirrorGame();                     // its variations, comments and NAGs
    game_series_ = SeriesOfPgn(game); // the scores its comments give, from White's side
    Refresh();
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
    // The tags and the result the main line's, the movetext every line of the tree with what was written round it.
    chess::Game main = tree_.GameOf(tree_.MainLine());
    if (main_outcome_.IsOver() && !main.IsOver() && main.PlyCount() == static_cast<int>(tree_.MainLine().size()))
    {
        main.SetOutcome(main_outcome_);
    }
    chess::PgnGame pgn = PgnOf(main, info_, source_pgn_, TodayInPgn());
    pgn.start          = tree_.start();
    pgn.moves          = tree_.ToPgnLine();
    return pgn;
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
    return title + " - Reckless Amphitheater";
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

void MainController::OpenPositionEditor()
{
    auto dialog =
        std::make_unique<PositionEditorDialog>(view_.board().pieces(), game_.PositionAt(view_ply_), is_flipped_);
    dialog->on_close  = [this] { CloseDialog(); };
    dialog->on_set_up = [this](const chess::Position &position) {
        CloseDialog();
        error_.clear();
        ShowGame(chess::Game(position), GameInfo{});
    };
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
    auto dialog      = std::make_unique<AboutDialog>(std::move(info));
    dialog->open_url = services_.open_url;
    dialog->on_close = [this] { CloseDialog(); };
    OpenDialog(std::move(dialog));
}

void MainController::SaveLayout()
{
    if (files_.layout.empty())
    {
        return;
    }
    LayoutSettings layout;
    layout.log_height  = view_.log_height();
    layout.is_log_open = view_.is_log_open();
    if (!layout.Save(files_.layout))
    {
        error_ = "The window's layout could not be written to " + files_.layout.string();
    }
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
    if (shown != watched_shown_)
    {
        competition_trackers_.clear(); // before the new game's lines are fed, so none of them is lost
    }
    for (const CompetitionLogLine &line : competition_->DrainLog())
    {
        if (line.game_number != 0 && (!is_following_competition_ || line.game_number != shown))
        {
            continue;
        }
        // Each side's lines under its own name - with its side after it when two copies of one engine play - and
        // into its own side's tracker, so one engine's positions never read the other's scores.
        std::string label = line.engine;
        if (const auto game = live.find(line.game_number); game != live.end())
        {
            const LiveGame &one = game->second;
            label = EngineLabel(line.engine, line.side == chess::kWhite ? one.black_name : one.white_name, line.side);
        }
        const bool is_to_engine = line.direction == uci::LineDirection::kToEngine;
        view_.log().Add(label,
                        is_to_engine                                        ? LogDirection::kToEngine
                        : line.direction == uci::LineDirection::kFromEngine ? LogDirection::kFromEngine
                                                                            : LogDirection::kError,
                        line.text);
        if (line.game_number != 0)
        {
            competition_trackers_.try_emplace(line.side, label).first->second.Feed(is_to_engine, line.text, line.at);
        }
    }
    if (!is_following_competition_ || (!is_changed && shown == watched_shown_))
    {
        return;
    }
    watched_shown_     = shown;
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
        info_.white_details  = "White \xc2\xb7 engine";
        info_.black_details  = "Black \xc2\xb7 engine";
        info_.event          = std::string(kind) + ", game " + std::to_string(shown) + " of " + std::to_string(total);
        const bool was_following = view_ply_ >= game_.PlyCount();
        game_                    = game.game ? *game.game : chess::Game{};
        game_series_             = SeriesOfNotes(game.notes, static_cast<std::size_t>(game_.PlyCount()),
                                                 {EngineLabel(game.white_name, game.black_name, chess::kWhite),
                                                  EngineLabel(game.black_name, game.white_name, chess::kBlack)},
                                                 {true, true});
        FitSeries();
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
        game_series_       = SeriesOfNotes(record->notes, static_cast<std::size_t>(game_.PlyCount()),
                                           {EngineLabel(record->white_name, record->black_name, chess::kWhite),
                                            EngineLabel(record->black_name, record->white_name, chess::kBlack)},
                                           {true, true});
        FitSeries();
        view_ply_   = game_.PlyCount();
        source_pgn_ = record->ToPgn();
        MirrorGame(); // the record's comments on the tree's moves
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
        // Each side's lines into its own tracker: two copies of one engine never share the positions they search.
        auto tracker = trackers_.try_emplace(line.side, line.engine).first;
        tracker->second.Feed(is_to_engine, line.text, line.at);
    }
    const int version = play_->Version();
    if (version == play_version_)
    {
        return;
    }
    play_version_                = version;
    const bool was_following     = view_ply_ >= game_.PlyCount();
    snapshot_                    = play_->Snapshot();
    game_                        = snapshot_.game;
    const PlaySettings &settings = play_->settings();
    game_series_ = SeriesOfNotes(snapshot_.notes, static_cast<std::size_t>(game_.PlyCount()),
                                 {EngineLabel(settings.white.Name(), settings.black.Name(), chess::kWhite),
                                  EngineLabel(settings.black.Name(), settings.white.Name(), chess::kBlack)},
                                 {!settings.white.is_human, !settings.black.is_human});
    // A resumed game's opening moves keep the scores they had before it was resumed.
    for (EvalSeries &series : game_series_)
    {
        for (const EvalSeries &before : opening_series_)
        {
            if (before.name != series.name)
            {
                continue;
            }
            for (std::size_t ply = 0; ply < before.points.size() && ply < series.points.size(); ++ply)
            {
                if (!series.points[ply])
                {
                    series.points[ply] = before.points[ply];
                }
            }
        }
    }
    FitSeries();
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
            MirrorGame();
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
            blocks.push_back(tracker->Block(now));
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
            // Each engine's word on the position shown is its own series' point for it, from White's side.
            const auto point = tracker->BestEvalPoint();
            if (!point || view_ply_ <= 0)
            {
                continue;
            }
            auto series = std::find_if(analysis_series_.begin(), analysis_series_.end(),
                                       [&](const EvalSeries &one) { return one.name == tracker->name(); });
            if (series == analysis_series_.end())
            {
                analysis_series_.push_back({tracker->name(), {}});
                series = analysis_series_.end() - 1;
            }
            series->points.resize(static_cast<std::size_t>(game_.PlyCount()));
            auto &slot = series->points[static_cast<std::size_t>(view_ply_ - 1)];
            if (slot != point)
            {
                slot        = point;
                graph_dirty = true;
            }
        }
        // The series in the engines' order, whichever answered first, so each engine keeps its place and its colour; a
        // series from an earlier analysis whose engine is not in this one keeps its place after them.
        const std::vector<std::string> engines = analysis_->Names();
        const auto                     order   = [&engines](const EvalSeries &series) {
            return static_cast<std::size_t>(std::find(engines.begin(), engines.end(), series.name) - engines.begin());
        };
        if (!std::is_sorted(analysis_series_.begin(), analysis_series_.end(),
                            [&](const EvalSeries &a, const EvalSeries &b) { return order(a) < order(b); }))
        {
            std::stable_sort(analysis_series_.begin(), analysis_series_.end(),
                             [&](const EvalSeries &a, const EvalSeries &b) { return order(a) < order(b); });
            graph_dirty = true;
        }
        for (const std::string &failure : analysis_->Failures())
        {
            status.push_back(failure);
        }
    }
    // The game's engines, White's first: a game on the board, or the competition's game the board follows.
    std::vector<const SearchTracker *> playing;
    if (play_)
    {
        for (const auto &[side, tracker] : trackers_)
        {
            const SideSpec &spec = side == chess::kWhite ? play_->settings().white : play_->settings().black;
            if (!spec.is_human)
            {
                playing.push_back(&tracker);
            }
        }
    }
    else if (is_following_competition_)
    {
        for (const auto &[side, tracker] : competition_trackers_)
        {
            playing.push_back(&tracker);
        }
    }
    for (const SearchTracker *found : playing)
    {
        const SearchTracker &tracker = *found;
        blocks.push_back(tracker.Block(now));
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
        ShowEval();
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
    state.is_log_open      = view_.is_log_open();
    state.is_in_variation  = !IsPlaying() && ShownVariation() != chess::GameTree::kNone;
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
    if (!LineMatchesGame())
    {
        MirrorGame(); // the game set from outside the tree: a game played, followed or opened
    }
    const chess::Position     &position = game_.PositionAt(view_ply_);
    std::optional<chess::Move> last;
    if (view_ply_ > 0)
    {
        last = game_.Moves()[static_cast<std::size_t>(view_ply_ - 1)];
    }
    view_.SetBoard(position, last, IsPersonToMove() || (!IsPlaying() && position.GenerateLegalMoves().size() > 0));
    view_.SetFlipped(is_flipped_);
    const chess::Position &start = game_.StartPosition();
    view_.SetMoves(game_.SanMoves(), start.FullmoveNumber(), start.SideToMove() == chess::kBlack, view_ply_,
                   LineVariations());
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

    ShowEval();
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
    // The first series with a score for the move: in a game between engines, the mover's engine's.
    const std::size_t index = static_cast<std::size_t>(view_ply_ - 1);
    for (const EvalSeries &series : GraphSeries())
    {
        if (index < series.points.size() && series.points[index])
        {
            caption += "  " + EvalText(*series.points[index]);
            break;
        }
    }
    return caption;
}

void MainController::FitSeries()
{
    const std::size_t count = static_cast<std::size_t>(game_.PlyCount());
    for (auto *list : {&game_series_, &analysis_series_})
    {
        for (EvalSeries &series : *list)
        {
            series.points.resize(count);
        }
    }
}

const std::vector<EvalSeries> &MainController::GraphSeries() const
{
    if (analysis_ || game_series_.empty())
    {
        return analysis_series_;
    }
    return game_series_;
}

void MainController::ShowEval()
{
    FitSeries();
    view_.SetEval(GraphSeries(), game_.PlyCount(), view_ply_, EvalCaption());
}

} // namespace amphitheater::app
