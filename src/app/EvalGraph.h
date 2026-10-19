#pragma once
/// @file EvalGraph.h The evaluation by move: White's score in pawns along the game, the area to zero filled, the
/// position shown marked, a press choosing a move.

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
     * The scores and the position shown.
     * @param scores White's score in pawns after each ply, from the first move; none where nothing is known. A mate
     *        counts as the scale's edge.
     * @param current_ply The position shown: 0 for the start, n after the n-th move.
     * @param caption What the bottom right says: the move shown and its score.
     */
    void SetScores(std::vector<std::optional<double>> scores, int current_ply, std::string caption);

    /** @return The scores. */
    const std::vector<std::optional<double>> &scores() const
    {
        return scores_;
    }

    /** @return The score at the top of the scale, in pawns: one, or more for a larger score, at most ten. */
    double Scale() const;

    /**
     * The ply nearest a point.
     * @param x The point's x, in the view's coordinates.
     * @return The ply, from 1; none with no scores.
     */
    std::optional<int> PlyAt(float x) const;

    /**
     * The scale, the zero line, the filled area, the line and the mark.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Chooses the move under the press.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

  private:
    /** @return The plot's area: inside the card, clear of the scale's figures and the captions. */
    reckless::gfx::RectF PlotArea() const;

    std::vector<std::optional<double>> scores_;        ///< White's score after each ply.
    int                                current_ply_{}; ///< The position shown.
    std::string                        caption_;       ///< The bottom right's text.
};

} // namespace uci_host::app
