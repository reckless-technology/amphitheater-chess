#pragma once
/// @file MainController.h What the main window does: it holds the game shown and the position the board shows in it,
/// plays games on the board (PlaySession), answers the menus, the plates, the keys, the board and the lists, opens the
/// dialogs, and sets what the view shows once a frame.

#include "AnalysisSession.h"
#include "EngineCatalogue.h"
#include "MainView.h"
#include "Menus.h"
#include "PlaySession.h"
#include "SearchTracker.h"
#include "chess/Game.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
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
};

/** Where the controller keeps what it keeps. */
struct ControllerFiles
{
    std::filesystem::path engines = EngineCatalogue::DefaultFile(); ///< The engine list; empty to keep none.
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
     */
    explicit MainController(MainView &view, ControllerFiles files = {});

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
     * that takes the game shown on from the position shown (cutting what followed it).
     * @param move The move, legal in the position shown.
     */
    void BoardMove(chess::Move move);

    /**
     * Play a game on the board, a game on the board already being stopped.
     * @param settings How to play it.
     */
    void StartGame(PlaySettings settings);

    /**
     * One display frame: the game on the board polled, the engines' lines read, the clocks and the analysis moved on.
     * @param seconds Since the last.
     */
    void Frame(double seconds);

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

    /** @return The last thing that went wrong, as the status bar says it; empty for nothing. */
    const std::string &last_error() const
    {
        return error_;
    }

  private:
    /** The view brought to the game, the position shown and the players. */
    void Refresh();

    /** @return The graph's caption: the move shown and White's score after it, "12...b4  +0.42". */
    std::string EvalCaption() const;

    /** The strips' names, details and clocks. */
    void RefreshStrips();

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

    /** Game > Take back: the person's last move and the engine's reply, or the last move with no game played. */
    void TakeBack();

    /** Game > Stop the game: abandoned, kept on the board. */
    void StopGame();

    /** The game on the board set aside to finish its thread in the background. */
    void RetirePlay();

    /** @return Whether the board waits for the person's move at the position shown. */
    bool IsPersonToMove() const;

    MainView       &view_;      ///< The view.
    EngineReaper    reaper_;    ///< Engines being quit off the window's thread; outlives the sessions below.
    ControllerFiles files_;     ///< Where it keeps things.
    EngineCatalogue catalogue_; ///< The configured engines.
    chess::Game     game_;      ///< The game shown.
    GameInfo        info_;      ///< What the card says of it.
    std::vector<std::optional<double>>        evals_;              ///< White's score after each ply, as known.
    int                                       view_ply_   = 0;     ///< The position shown.
    bool                                      is_flipped_ = false; ///< Black at the bottom.
    std::unique_ptr<PlaySession>              play_;               ///< The game on the board.
    int                                       play_version_ = -1;  ///< Its snapshot's version last taken in.
    PlaySnapshot                              snapshot_;           ///< Its snapshot as last taken in.
    std::vector<std::unique_ptr<PlaySession>> retiring_;           ///< Stopped games finishing their threads.
    std::map<std::string, SearchTracker>      trackers_;           ///< The playing engines' searches, by name.
    std::unique_ptr<AnalysisSession>          analysis_;           ///< The engines analysing the position shown.
    std::unique_ptr<DialogFrame>              dialog_;             ///< The dialog over the window.
    std::vector<std::unique_ptr<DialogFrame>> closed_dialogs_;     ///< Closed, to be destroyed next frame.
    std::string                               error_;              ///< The last thing that went wrong.
};

} // namespace uci_host::app
