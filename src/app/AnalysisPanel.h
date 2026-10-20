#pragma once
/// @file AnalysisPanel.h The analysis: a block per engine - its name, a chip saying what it is doing, its search's
/// figures - and its lines, the best first, each a depth, a score and the line in SAN.

#include "View.h"

#include <string>
#include <vector>

namespace amphitheater::app
{

/** One line of an engine's search. */
struct AnalysisLine
{
    std::string depth;    ///< "24/38": the depth and the selective depth.
    std::string score;    ///< "+0.42", "#5", from White's side.
    int         sign = 0; ///< Whose the score favours: 1 White, -1 Black, 0 neither.
    std::string pv;       ///< "13.Ne2 Ne8 14.f4 a5".
};

/** One engine's block. */
struct EngineBlock
{
    std::string               name;               ///< "Pawnstar 2.1".
    std::string               state;              ///< The chip: "thinking 4.8 s", "pondering 13.Ne2"; empty for none.
    bool                      is_thinking = true; ///< The chip lit in the accent; else in the muted ink.
    std::string               stats;              ///< "depth 24/38 · 11.2 Mn · 2.31 Mn/s · hash 34 %".
    std::vector<AnalysisLine> lines;              ///< Its lines, best first.
};

/** The engines' blocks, one under another. */
class AnalysisPanel final : public reckless::view::View
{
  public:
    static constexpr int kHeaderHeight = 34; ///< A block's header, in design pixels.
    static constexpr int kLineHeight   = 27; ///< A line, in design pixels.
    static constexpr int kGap          = 10; ///< Between blocks, in design pixels.

    AnalysisPanel();

    /** @param blocks The blocks, in order; repaints. */
    void SetBlocks(std::vector<EngineBlock> blocks);

    /** @return The blocks. */
    const std::vector<EngineBlock> &blocks() const
    {
        return blocks_;
    }

    /** @param text What the panel says with no block: how to start an analysis. */
    void SetEmptyText(std::string text);

    /**
     * The blocks; the empty text where there are none.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    std::vector<EngineBlock> blocks_;     ///< The blocks.
    std::string              empty_text_; ///< What it says with none.
};

} // namespace amphitheater::app
