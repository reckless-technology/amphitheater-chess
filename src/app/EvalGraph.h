#pragma once
/// @file EvalGraph.h The evaluation by move: each engine's score along the game as a series of its own, every one from
/// White's point of view (+ White is better), on one scale with the zero line - in a game between engines White's
/// engine's scores at its moves and Black's at its, side by side in two colours - with a legend naming each series in
/// its colour, the position shown marked, and a press choosing a move.

#include "Evaluations.h"

#include "Color.h"
#include "View.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** The graph of the evaluation. */
class EvalGraph final : public reckless::view::View
{
  public:
    EvalGraph();

    std::function<void(int ply)> on_select; ///< A press on the graph: the position nearest it, as a ply.

    /**
     * A series' colour, readable on the dark panel: the accent amber, then a cool blue, a violet and the green.
     * @param index The series' place, from 0.
     * @return The colour.
     */
    static reckless::gfx::Color SeriesColour(std::size_t index);

    /**
     * The series and the position shown.
     * @param series The series, each from White's point of view, the first drawn in amber and the second in blue.
     * @param ply_count The game's length in plies: the graph's width.
     * @param current_ply The position shown: 0 for the start, n after the n-th move.
     * @param caption What the bottom right says: the move shown and its score.
     */
    void SetSeries(std::vector<EvalSeries> series, int ply_count, int current_ply, std::string caption);

    /** @return The series. */
    const std::vector<EvalSeries> &series() const
    {
        return series_;
    }

    /** @return The bottom right's text: the move shown and its score. */
    const std::string &caption() const
    {
        return caption_;
    }

    /** @return The game's length in plies. */
    int ply_count() const
    {
        return ply_count_;
    }

    /**
     * @return The score at the top of the scale, in pawns: one, or more for a larger score, at most ten; a mate sets
     *         no scale, being held at its edge.
     */
    double Scale() const;

    /**
     * The ply nearest a point.
     * @param x The point's x, in the view's coordinates.
     * @return The ply, from 1; none with no moves.
     */
    std::optional<int> PlyAt(float x) const;

    /**
     * The scale, the zero line, each series' area and line, the mates at the edge, the legend and the mark.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Chooses the move under the press.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

  private:
    /** @return The plot's area: inside the card, clear of the scale's figures, the legend and the captions. */
    reckless::gfx::RectF PlotArea() const;

    /** @return Whether a legend is drawn: whenever there is a named series. */
    bool HasLegend() const;

    std::vector<EvalSeries> series_;        ///< The series.
    int                     ply_count_{};   ///< The game's length.
    int                     current_ply_{}; ///< The position shown.
    std::string             caption_;       ///< The bottom right's text.
};

} // namespace uci_host::app
