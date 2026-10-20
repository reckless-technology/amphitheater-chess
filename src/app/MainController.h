#pragma once
/// @file MainController.h What the main window does: it holds the game shown and the position the board shows in it,
/// plays games on the board (PlaySession), answers the menus, the plates, the keys, the board and the lists, opens the
/// dialogs, and sets what the view shows once a frame.

#include "AnalysisSession.h"
#include "CompetitionSession.h"
#include "EngineCatalogue.h"
#include "GameAnalysisDialog.h"
#include "MainView.h"
#include "Menus.h"
#include "PlaySession.h"
#include "SearchTracker.h"
#include "UpdateCheck.h"
#include "WindowSizes.h"
#include "chess/Game.h"
#include "chess/GameTree.h"
#include "chess/PolyglotBook.h"

#include "chess/Pgn.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

class DialogFrame;

/** What the game card says of a game beside its moves. */
struct GameInfo
{
    std::string white = "White"; ///< White's name.
    std::string black = "Black"; ///< Black's name.
    std::string white_details;   ///< White's strip's second line, after the side.
    std::string black_details;   ///< Black's, likewise.
    std::string event;           ///< The event, as PGN names it.
    std::string opening;         ///< The opening's name.
    std::string time_control;    ///< The time control, as a person reads it.
    std::string book;            ///< The books in play.
    std::string score;           ///< A match's or a tournament's score so far.
};

/** What the controller asks of the window and the platform: the clipboard, the file dialog, the app's end, the menu
    bar, the window's sizes. Each left empty is the window's own: the clipboard and the dialog through the view's host,
   no quit, and a menu bar drawn across the window's top. */
struct ControllerServices
{
    std::function<std::string()>                 read_clipboard;  ///< The clipboard's text.
    std::function<bool(const std::string &text)> write_clipboard; ///< Put text on the clipboard.
    /** The file dialog: `picked` runs with the path chosen, or an empty one. */
    std::function<void(const reckless::view::FileRequest &request, std::function<void(const std::string &)> picked)>
                          choose_file;
    std::function<void()> quit; ///< File > Quit.
    /** Opens a page in the person's browser (Help > About's website); left empty, the link does nothing. */
    std::function<void(const std::string &url)> open_url;
    /** The menu bar shown, as the platform shows it (AppPlatform::SetMenuBar): `chosen` runs with each entry chosen,
        by its click or its shortcut. The controller sets it again whenever what the menus can do changes. */
    std::function<void(const reckless::app::MenuBar &bar, std::function<void(int id)> chosen)> set_menu_bar;
    /** What the View menu shows of the window's sizes (WindowSizes::Menu); empty for no window to size - a suite, the
        render tool - where every size is dim. */
    std::function<WindowSizeMenu()> window_sizes;
    /** A size from the View menu chosen, one of kWindowSizes (WindowSizes::Choose). */
    std::function<void(int percent)> choose_window_size;
};

/** Where the controller keeps what it keeps. */
struct ControllerFiles
{
    std::filesystem::path engines = EngineCatalogue::DefaultFile(); ///< The engine list; empty to keep none.
    /** The engines the install carries (BundledEngines.h), seeded into the list as the controller starts; empty for
        none, as for a build tree's host. */
    std::filesystem::path bundled_engines;
    /** The window's layout (LayoutSettings.h): read as the controller starts, written when a person folds, opens or
        resizes the engine log; empty to keep none - the suites' and the render tool's - where the app names
        LayoutSettings::DefaultFile(). */
    std::filesystem::path layout;
};

/** The main window's controller. */
class MainController
{
  public:
    using Clock = std::chrono::steady_clock; ///< The clock the window's times are on.

    static constexpr int kAnalysisLines = 3; ///< The lines each analysing engine is asked for (MultiPV).

    /**
     * @param view The view it drives; outlives the controller.
     * @param files Where it keeps the engine list.
     * @param services The clipboard, the file dialog and the app's end; the window's own where left empty.
     */
    explicit MainController(MainView &view, ControllerFiles files = {}, ControllerServices services = {});

    /** Stops a game on the board and waits for the games still finishing. */
    ~MainController();

    MainController(const MainController &)            = delete;
    MainController &operator=(const MainController &) = delete;

    /**
     * Show a game to read and analyse, at its last position; a game on the board is stopped first.
     * @param game The game.
     * @param info What the card says of it.
     */
    void ShowGame(chess::Game game, GameInfo info);

    /**
     * Do a command.
     * @param command A Command.
     */
    void Command(int command);

    /**
     * Show a position of the game.
     * @param ply 0 for the start, up to the game's length.
     */
    void ShowPly(int ply);

    /**
     * A person's move on the board: the person's move in a game on the board, or, with no game being played, a move
     * from the position shown - the next move of the line shown, or a variation leaving it there (one already there
     * entered, a new one added) - and the line through it shown.
     * @param move The move, legal in the position shown.
     */
    void BoardMove(chess::Move move);

    /**
     * Show a line of the game: the one through a node, at a position of it.
     * @param id The node; the line runs on past it along its main continuations.
     * @param ply The position to show on it.
     */
    void ShowLineThrough(chess::GameTree::NodeId id, int ply);

    /** @return The game with its variations. */
    const chess::GameTree &tree() const
    {
        return tree_;
    }

    /** @return The line shown, as the tree's nodes: game() is its moves. */
    const std::vector<chess::GameTree::NodeId> &line() const
    {
        return line_;
    }

    /**
     * @return The variation the position shown is in: the last move of the line shown at or before it that is not its
     *         line's main continuation, else the first after it; none on the main line.
     */
    chess::GameTree::NodeId ShownVariation() const;

    /**
     * Play a game on the board, a game on the board already being stopped.
     * @param settings How to play it.
     */
    void StartGame(PlaySettings settings);

    /**
     * One display frame: the game on the board polled, the engines' lines read, the clocks and the analysis moved on,
     * the update check polled about once a second.
     * @param seconds Since the last.
     */
    void Frame(double seconds);

    /**
     * The update check the window runs: its menu under Help > Updates, the status bar saying when a newer release is
     * out. None (a launch's check, a suite, checks switched off by the environment) for no check and no menu.
     * @param checker The check, or null.
     * @param open_url Opens the newer release's page in the person's browser.
     */
    void SetUpdateChecker(std::unique_ptr<reckless::update::Checker>  checker,
                          std::function<void(const std::string &url)> open_url);

    /** @return The update check, or null. */
    reckless::update::Checker *update_checker()
    {
        return updates_.get();
    }

    /** @return The game shown. */
    const chess::Game &game() const
    {
        return game_;
    }

    /** @return The position the board shows: 0 for the start, n after the n-th move. */
    int view_ply() const
    {
        return view_ply_;
    }

    /** @return Whether Black is at the bottom. */
    bool flipped() const
    {
        return is_flipped_;
    }

    /** @return Whether a game is being played on the board. */
    bool IsPlaying() const;

    /** @return The game on the board, while one is played (or has just finished); null otherwise. */
    PlaySession *play()
    {
        return play_.get();
    }

    /**
     * Show a game read from PGN, keeping its tags, comments and variations for a save.
     * @param game The game.
     */
    void ShowPgnGame(const chess::PgnGame &game);

    /**
     * Read a PGN file: its one game shown, or a list to pick one from when it holds more.
     * @param path The file.
     */
    void OpenPgn(const std::string &path);

    /**
     * Write the game shown to a PGN file.
     * @param path The file.
     * @return Whether it was written.
     */
    bool SavePgn(const std::string &path);

    /** @return The game shown as PGN, as Save PGN writes it. */
    chess::PgnGame CurrentPgn() const;

    /**
     * The window's title, following the game shown: "White vs Black - Reckless Amphitheater", the file's name before
     * the app's while the game shown is one opened from a file or saved to one.
     * @return The title.
     */
    std::string WindowTitle() const;

    /**
     * Take a FEN or a PGN as pasted: the position, or the game, or a list of games to pick from.
     * @param text The text.
     * @return Whether it was a position or held a game.
     */
    bool Paste(const std::string &text);

    /**
     * Play a match or a tournament, the board following its games; one under way is stopped first.
     * @param settings How to play it.
     * @param is_match Whether it is a match.
     */
    void StartCompetition(match::TournamentSettings settings, bool is_match);

    /** @return The match or the tournament under way or last played, or null. */
    CompetitionSession *competition()
    {
        return competition_.get();
    }

    /** @return The number of the competition's game the board follows, or 0. */
    int watched_game() const
    {
        return watched_shown_;
    }

    /** @return The configured engines. */
    EngineCatalogue &catalogue()
    {
        return catalogue_;
    }

    /** @return The analysis under way, or null. */
    AnalysisSession *analysis()
    {
        return analysis_.get();
    }

    /**
     * Analyse, or stop analysing: the engines marked for analysis in the list - every configured engine where none is
     * marked - thinking about the position shown.
     * @param is_on Whether to analyse.
     */
    void SetAnalysing(bool is_on);

    /** @return The dialog open over the window, or null. */
    DialogFrame *dialog()
    {
        return dialog_.get();
    }

    /** @return What the menus show as possible now. */
    MenuState Menus() const;

    /** @return The menu bar as the controller last set it. */
    const reckless::app::MenuBar &menu_bar() const
    {
        return menu_bar_;
    }

    /** @return The window's own menu bar - its model, drawn across the window's top - where the platform's is not
        used; null where it is. */
    reckless::app::MenuBarController *own_menus()
    {
        return own_menus_.get();
    }

    /**
     * Analyse the line shown: an engine over every position of it, each position's score written on the move that led
     * to it, each move that cost its mover marked, the engine's line added where a move was a mistake or a blunder.
     * @param choice The engine, its search and what to write.
     */
    void StartGameAnalysis(const GameAnalysisChoice &choice);

    /** @return Whether the game analysis is under way. */
    bool IsAnalysingGame() const
    {
        return review_ != nullptr;
    }

    /** @return The last thing that went wrong, as the status bar says it; empty for nothing. */
    const std::string &last_error() const
    {
        return error_;
    }

  private:
    /** The view brought to the game, the position shown and the players. */
    void Refresh();

    /** @return Whether the line shown is the game's moves: else the game was set from outside the tree. */
    bool LineMatchesGame() const;

    /** The tree made again from the game shown - its record's PGN, comments and all, where that is its record - and
        its main line shown. */
    void MirrorGame();

    /**
     * Show a line of the tree: the game its moves, at a position of it, the scores of the moves it shares with the
     * line shown before kept and the rest let go.
     * @param line The line.
     * @param ply The position.
     */
    void ShowLine(std::vector<chess::GameTree::NodeId> line, int ply);

    /** @return The variations leaving the line shown, for the moves list. */
    std::vector<VariationRow> LineVariations() const;

    /** @return The graph's caption: the move shown and White's score after it, "12...b4  +0.42". */
    std::string EvalCaption() const;

    /** Every series held to the game's length: a move cut takes its scores with it, a new move has none yet. */
    void FitSeries();

    /**
     * @return The series the graph draws: the analysing engines' while Analyse runs, else the game's engines' (or,
     *         with none, what an analysis left).
     */
    const std::vector<EvalSeries> &GraphSeries() const;

    /** The graph set from the series, the position shown and the caption. */
    void ShowEval();

    /** The strips' names, details and clocks. */
    void RefreshStrips();

    /** The menu bar set again when what the menus can do has changed since it was last set. */
    void RefreshMenuBar();

    /** The analysis panel, the arrow and the status bar, from the engines' searches. */
    void RefreshSearches();

    /** The game on the board's progress taken in. */
    void PollPlay();

    /**
     * A dialog over the window, replacing any open.
     * @param dialog The dialog.
     */
    void OpenDialog(std::unique_ptr<DialogFrame> dialog);

    /** The open dialog closed, its destruction left to the next frame (it may be in its own event). */
    void CloseDialog();

    /** Game > New game. */
    void OpenNewGame();

    /** Engines > Manage engines. */
    void OpenEngineManager();

    /**
     * Match > New match, or Tournament > New tournament.
     * @param is_tournament Which.
     */
    void OpenCompetition(bool is_tournament);

    /** Tournament > Standings and crosstable. */
    void OpenStandings();

    /** Help > About: the app's name, version and terms, and the engines the install carries. */
    void OpenAbout();

    /** Edit > Set up position: the position shown on a board to edit, shown in its place when it is set up. */
    void OpenPositionEditor();

    /** Game > Analyse the game: its dialog. */
    void OpenGameAnalysis();

    /** Engines > Test suite: an EPD file chosen, then the suite's dialog over it. */
    void ChooseTestSuite();

  public:
    /**
     * Open a test suite's dialog over a file: its positions listed, ready to run.
     * @param path The EPD file.
     * @return Whether the file was read and held a position.
     */
    bool OpenTestSuite(const std::string &path);

    /**
     * Open a Polyglot opening book in place of any open: its moves for the position shown listed under the card and
     * drawn faintly on the board, and kept in the layout file for the next launch.
     * @param path The book's file, or kBuiltInBook for the book the window carries.
     * @return Whether it was read; the book open before stays open where it was not.
     */
    bool OpenBook(const std::string &path);

    /** @return The book open: its file, kBuiltInBook, or empty for none. */
    const std::string &book_path() const
    {
        return book_path_;
    }

    /** File > Close book: the book's list and arrows taken away. */
    void CloseBook();

    /** @return The book's moves for the position shown, the heaviest first; none with no book open. */
    const std::vector<BookMove> &book_moves() const
    {
        return book_moves_;
    }

  private:
    /**
     * The book's moves for the position shown, read again where it changed, and handed to the view.
     * @param shown The position shown.
     */
    void RefreshBook(const chess::Position &shown);

    /** @return The book's moves as faint arrows, each as clear as its share; the engines' arrows go over them. */
    std::vector<BoardArrow> BookArrows() const;

    /** The game analysis's positions taken in, and its end, once a frame. */
    void PollGameAnalysis();

    /**
     * One position's analysis written into the game: its score on the move that led to it, the move judged against
     * the position before it, and the engine's line beside a mistake or a blunder.
     * @param analysis The position's analysis.
     */
    void ApplyGameAnalysis(const match::PositionAnalysis &analysis);

    /** The game analysis set aside to finish its search in the background: the game it analyses changed. */
    void RetireGameAnalysis();

    /** The engine log's layout as the view has it, and the pieces' set, kept in the layout file, where there is one. */
    void SaveLayout();

    /**
     * The board drawn with a set of pieces: View > Pieces.
     * @param id The set's id, one of kPieceSets.
     */
    void UsePieces(const std::string &id);

  public:
    /** @return The pieces' set the board is drawn with: its id. */
    const std::string &piece_set() const
    {
        return piece_set_;
    }

  private:
    /** The competition's progress taken in, and the game the board follows shown. */
    void PollCompetition();

    /** The competition set aside to finish its workers in the background. */
    void RetireCompetition();

    /** Game > Take back: the person's last move and the engine's reply, or the last move with no game played. */
    void TakeBack();

    /** Game > Stop the game: abandoned, kept on the board. */
    void StopGame();

    /** The game on the board set aside to finish its thread in the background. */
    void RetirePlay();

    /** @return Whether the board waits for the person's move at the position shown. */
    bool IsPersonToMove() const;

    std::shared_ptr<reckless::app::MenuBarController> own_menus_;   ///< The window's own bar, where no platform's.
    std::optional<MenuState>                          menus_shown_; ///< What the menu bar was last set for.
    reckless::app::MenuBar                            menu_bar_;    ///< The menu bar as last set.
    std::unique_ptr<reckless::update::Checker>        updates_;     ///< The update check, or none.
    std::function<void(const std::string &url)>       open_url_;    ///< Opens a release's page.
    double                                            update_poll_ = 0.0; ///< Seconds since it was last polled.

    MainView                     &view_;       ///< The view.
    ControllerServices            services_;   ///< The clipboard, the file dialog, the app's end.
    std::optional<chess::PgnGame> source_pgn_; ///< The PGN the game shown came from (a file, a game played).
    std::string                   file_name_;  ///< The file the game shown was opened from or saved to; empty for none.
    EngineReaper                  reaper_; ///< Engines being quit off the window's thread; outlives the sessions below.
    ControllerFiles               files_;  ///< Where it keeps things.
    EngineCatalogue               catalogue_;         ///< The configured engines.
    std::map<std::string, PieceSet>      piece_sets_; ///< The sets loaded, by id: the board draws with one.
    std::string                          piece_set_{kDefaultPieceSet};    ///< The set the board is drawn with.
    std::optional<chess::PolyglotBook>   book_;                           ///< The opening book open, if any.
    std::string                          book_path_;                      ///< Its file, kept in the layout file.
    std::optional<chess::Position>       book_position_;                  ///< The position its moves were read for.
    std::vector<BookMove>                book_moves_;                     ///< Its moves there, the heaviest first.
    std::unique_ptr<match::GameAnalyser> review_;                         ///< The game analysis under way.
    std::vector<std::unique_ptr<match::GameAnalyser>>   retired_reviews_; ///< Analyses finishing in the background.
    GameAnalysisChoice                                  review_choice_;   ///< Its engine and what it writes.
    std::vector<chess::GameTree::NodeId>                review_line_;     ///< The line it analyses.
    std::vector<std::optional<match::PositionAnalysis>> review_done_;     ///< Each position's analysis, as it comes.
    std::array<int, 3>                   review_counts_{}; ///< The inaccuracies, mistakes and blunders it found.
    std::string                          review_summary_;  ///< What the last analysis found, for the status bar.
    chess::Game                          game_;            ///< The line shown, as a game.
    chess::GameTree                      tree_;            ///< The game shown with its variations.
    std::vector<chess::GameTree::NodeId> line_;            ///< The line shown, as the tree's nodes.
    chess::Outcome main_outcome_; ///< How the main line ended, by its record or its play where the rules do not say.
    GameInfo       info_;         ///< What the card says of it.
    std::vector<EvalSeries> game_series_;        ///< The game's engines' scores, from White's side, as known.
    std::vector<EvalSeries> analysis_series_;    ///< Each analysing engine's scores for the positions it analysed.
    std::vector<EvalSeries> opening_series_;     ///< The scores a game on the board was started with, from a take-back.
    int                     view_ply_   = 0;     ///< The position shown.
    bool                    is_flipped_ = false; ///< Black at the bottom.
    std::unique_ptr<PlaySession>              play_;                ///< The game on the board.
    int                                       play_version_ = -1;   ///< Its snapshot's version last taken in.
    PlaySnapshot                              snapshot_;            ///< Its snapshot as last taken in.
    std::vector<std::unique_ptr<PlaySession>> retiring_;            ///< Stopped games finishing their threads.
    std::map<int, SearchTracker>              trackers_;            ///< The playing engines' searches, by side.
    std::unique_ptr<AnalysisSession>          analysis_;            ///< The engines analysing the position shown.
    std::unique_ptr<CompetitionSession>       competition_;         ///< The match or the tournament.
    bool                         is_following_competition_ = false; ///< The board follows the competition's games.
    int                          watched_ = 0; ///< The game a person asked to follow; 0 for the lowest in progress.
    int                          watched_shown_ = 0;           ///< The game the board shows.
    std::map<int, SearchTracker> competition_trackers_;        ///< The followed game's engines' searches, by side.
    std::unique_ptr<DialogFrame> dialog_;                      ///< The dialog over the window.
    std::vector<std::unique_ptr<DialogFrame>> closed_dialogs_; ///< Closed, to be destroyed next frame.
    std::string                               error_;          ///< The last thing that went wrong.
};

} // namespace amphitheater::app
