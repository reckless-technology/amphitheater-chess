#pragma once
/// @file MainView.h The main window's content, as the approved mockup lays it out
/// (docs/design/main-window-mockup.html): the menu bar across the top where it is drawn (Linux, and a window drawn to
/// an image), none where the platform shows the bar itself (macOS, Windows); the board column - a player's strip above
/// and below the board, the navigation plates under it - and beside it the moves and the game's card; across the foot
/// the analysis with the evaluation graph, the engine log under its header and the status bar. The board grows with the
/// window; the foot keeps its height but for the log's, which a person sets by dragging its header, or folds away to
/// the header alone by clicking it.
///
/// The view shows what it is given and tells what a person does: the controllers (GameController and the rest) hold
/// the game, the engines and the files, and set what it shows each frame.

#include "AnalysisPanel.h"
#include "BoardView.h"
#include "EngineLog.h"
#include "EvalGraph.h"
#include "MovesList.h"
#include "Panels.h"
#include "PieceSet.h"

#include "Button.h"
#include "MenuBarView.h"
#include "View.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** The main window's views, laid out to the window's size. */
class MainView final : public reckless::view::View
{
  public:
    static constexpr int kDesignWidth  = 1380; ///< The window's width when first opened, in design pixels.
    static constexpr int kDesignHeight = 1120; ///< Its height.
    static constexpr int kLayoutWidth  = 1240; ///< The narrowest the layout is laid out at; a narrower window zooms.
    static constexpr int kLayoutHeight = 1000; ///< The shortest the layout is laid out at; a shorter window zooms.
    /** The narrowest the window may be made: the View menu's smallest size, half the design's. */
    static constexpr int kMinimumWidth  = kDesignWidth / 2;
    static constexpr int kMinimumHeight = kDesignHeight / 2; ///< The shortest: half the design's, likewise.

    static constexpr int kMenuHeight       = 34;  ///< The menu bar.
    static constexpr int kStatusHeight     = 28;  ///< The status bar.
    static constexpr int kMargin           = 18;  ///< Round the body.
    static constexpr int kGap              = 16;  ///< Between the columns and the rows.
    static constexpr int kStripHeight      = 54;  ///< A player's strip.
    static constexpr int kNavHeight        = 32;  ///< The navigation plates.
    static constexpr int kLabelHeight      = 28;  ///< A section's label.
    static constexpr int kAnalysisHeight   = 192; ///< The analysis panel and the graph, under their labels.
    static constexpr int kLogDefaultHeight = 161; ///< The engine log as it first opens: seven rows.
    static constexpr int kLogLeastHeight   = 46;  ///< The shortest a drag makes the log: two rows.
    static constexpr int kLogHeaderGap     = 8;   ///< Between the analysis and the log's header.
    /** The smallest a board square becomes as the log is made taller, in design pixels. */
    static constexpr int kLeastSquare = 40;
    static constexpr int kMovesWidth  = 250; ///< The moves' column at least: a number and two moves in SAN.
    static constexpr int kSideWidth   = 380; ///< The players' column at least: the strips, the card, the plates.
    static constexpr int kGraphWidth  = 320; ///< The evaluation graph's column.

    /** @param pieces The pieces the board draws with; outlives the view. */
    explicit MainView(const PieceSet &pieces);

    std::function<void(int command)>      on_command;    ///< A menu item, a plate or a key: a Command.
    std::function<void(chess::Move move)> on_move;       ///< A person's move on the board.
    std::function<void(int ply)>          on_select_ply; ///< A move pressed in the list or on the graph.
    /** The engine log folded, opened or made another height by a person: the layout to keep (log_height(),
        is_log_open()). */
    std::function<void()> on_log_layout;

    // ---- What it shows ----------------------------------------------------------------------------------------------

    /**
     * The board.
     * @param position The position shown.
     * @param last_move The move that led to it.
     * @param is_interactive Whether a person may move the side to move.
     */
    void SetBoard(const chess::Position &position, std::optional<chess::Move> last_move, bool is_interactive);

    /** @param is_flipped Whether Black is at the bottom: the strips change places with the board. */
    void SetFlipped(bool is_flipped);

    /** @param arrows The arrows over the board. */
    void SetArrows(std::vector<BoardArrow> arrows);

    /**
     * The players' strips.
     * @param white White's strip.
     * @param black Black's strip.
     */
    void SetPlayers(PlayerStripState white, PlayerStripState black);

    /**
     * The moves.
     * @param sans Every move in SAN.
     * @param first_number The first move's number.
     * @param is_black_first Whether the first move is Black's.
     * @param current_ply The position the board shows.
     */
    void SetMoves(std::vector<std::string> sans, int first_number, bool is_black_first, int current_ply);

    /** @param ply The position the board shows, lit in the list. */
    void SetCurrentPly(int ply);

    /** @param lines The game's card. */
    void SetCard(std::vector<CardLine> lines);

    /** @param blocks The engines' analysis. */
    void SetAnalysis(std::vector<EngineBlock> blocks);

    /** @param is_analysing Whether the Analyse plate is lit. */
    void SetAnalysing(bool is_analysing);

    /**
     * The evaluation graph.
     * @param series Each engine's scores along the game, from White's point of view.
     * @param ply_count The game's length in plies.
     * @param current_ply The position shown.
     * @param caption The move shown and its score.
     */
    void SetEval(std::vector<EvalSeries> series, int ply_count, int current_ply, std::string caption);

    /** @param items The status bar's phrases. */
    void SetStatus(std::vector<std::string> items);

    /**
     * The engine log's layout, as kept: laid out at once, telling no one.
     * @param height The log's height asked for, in design pixels: held between kLogLeastHeight and what leaves the
     *               board squares of kLeastSquare, and shown as the whole rows that fit.
     * @param is_open Whether the log shows; folded, only its header does, and the board has the room.
     */
    void SetLogLayout(int height, bool is_open);

    /** The engine log opened or folded, as its header's click or View > Engine log does, telling on_log_layout. */
    void ToggleLog();

    /** @return The engine log's height asked for, in design pixels, kept while it is folded. */
    int log_height() const
    {
        return log_height_;
    }

    /** @return Whether the engine log shows. */
    bool is_log_open() const
    {
        return is_log_open_;
    }

    /** @return The tallest the log may be made in the window's height now: the board left squares of kLeastSquare. */
    int LogMostHeight() const;

    /**
     * The drawn menu bar across the top, or none where the platform shows the bar itself: the layout then starts at
     * the top.
     * @param bar The bar, owned from here on; null for none.
     */
    void SetMenuBarView(std::unique_ptr<reckless::app::MenuBarView> bar);

    /**
     * A view over the whole window - a dialog - sized with it and in front of everything; null takes it away.
     * @param overlay The view, owned by the caller, which takes it away before it goes; or null.
     */
    void SetOverlay(reckless::view::View *overlay);

    /** @return The view over the window, or null. */
    reckless::view::View *overlay() const
    {
        return overlay_;
    }

    // ---- Its parts, for the controllers and the suites ------------------------------------------------------------

    /** @return The board. */
    BoardView &board()
    {
        return board_;
    }

    /** @return The strip above the board. */
    PlayerStrip &top_strip()
    {
        return top_strip_;
    }

    /** @return The strip below the board. */
    PlayerStrip &bottom_strip()
    {
        return bottom_strip_;
    }

    /** @return The moves. */
    MovesList &moves()
    {
        return moves_;
    }

    /** @return The game's card. */
    GameCard &card()
    {
        return card_;
    }

    /** @return The analysis. */
    AnalysisPanel &analysis()
    {
        return analysis_;
    }

    /** @return The evaluation graph. */
    EvalGraph &graph()
    {
        return graph_;
    }

    /** @return The engine log. */
    EngineLog &log()
    {
        return log_;
    }

    /** @return The engine log's header: its fold and its drag. */
    LogHeader &log_header()
    {
        return log_header_;
    }

    /** @return The status bar. */
    StatusBar &status()
    {
        return status_;
    }

    /** @return The drawn menu bar, or null where the platform shows the bar itself. */
    reckless::app::MenuBarView *menu_bar()
    {
        return menu_bar_.get();
    }

    /**
     * A navigation plate, by its command.
     * @param command kFirst, kBack, kForward, kLast, kFlip or kAnalyse.
     * @return The plate, or null.
     */
    reckless::view::Button *Plate(int command);

    // ---- The view ---------------------------------------------------------------------------------------------------

    /** Lays every part out to the window's size. */
    void Resized() override;

    /**
     * The window's colour.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * A press anywhere takes the keyboard off a list or a field, so the keys go to the menu bar's shortcuts (Menus.h),
     * which the platform offers every key no view takes.
     * @return True.
     */
    bool WantsKeyboardFocus() const override
    {
        return true;
    }

  private:
    /** A navigation plate and its command. */
    struct NavPlate
    {
        int                                     command; ///< What it does.
        int                                     width;   ///< Its width, in design pixels.
        std::unique_ptr<reckless::view::Button> button;  ///< The plate.
    };

    /**
     * Tell a command.
     * @param command The command.
     */
    void Tell(int command);

    std::unique_ptr<reckless::app::MenuBarView> menu_bar_;     ///< The drawn menus; null where the platform shows them.
    PlayerStrip                                 top_strip_;    ///< The strip above the board.
    BoardView                                   board_;        ///< The board.
    PlayerStrip                                 bottom_strip_; ///< The strip below the board.
    std::vector<NavPlate>                       nav_;          ///< The navigation plates.
    SectionLabel                                moves_label_;  ///< "MOVES".
    MovesList                                   moves_;        ///< The moves.
    SectionLabel                                card_label_;   ///< "GAME".
    GameCard                                    card_;         ///< The game's card.
    SectionLabel                                analysis_label_;                  ///< "ANALYSIS".
    AnalysisPanel                               analysis_;                        ///< The engines' lines.
    SectionLabel                                graph_label_;                     ///< "EVALUATION".
    EvalGraph                                   graph_;                           ///< The evaluation by move.
    LogHeader                                   log_header_;                      ///< The engine log's header.
    EngineLog                                   log_;                             ///< The engine log.
    int                                         log_height_  = kLogDefaultHeight; ///< The log's height asked for.
    bool                                        is_log_open_ = true;              ///< The log shows.
    int                                         drag_from_   = 0;   ///< The log's height as a drag of its header began.
    StatusBar                                   status_;            ///< The status bar.
    PlayerStripState                            white_;             ///< White's strip.
    reckless::view::View                       *overlay_ = nullptr; ///< The view over the window, if any.
    PlayerStripState                            black_;             ///< Black's strip.
};

} // namespace amphitheater::app
