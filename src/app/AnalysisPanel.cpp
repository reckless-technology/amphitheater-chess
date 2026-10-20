#include "AnalysisPanel.h"

#include "Theme.h"

#include <algorithm>

namespace amphitheater::app
{

namespace
{

using reckless::gfx::Justify;
using reckless::gfx::RectF;

} // namespace

AnalysisPanel::AnalysisPanel()
{
    SetInterceptsMouse(false, false);
}

void AnalysisPanel::SetBlocks(std::vector<EngineBlock> blocks)
{
    blocks_ = std::move(blocks);
    Repaint();
}

void AnalysisPanel::SetEmptyText(std::string text)
{
    empty_text_ = std::move(text);
    Repaint();
}

void AnalysisPanel::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    if (blocks_.empty())
    {
        c.SetColor(theme::kPanel);
        c.FillRect(bounds);
        c.SetColor(theme::kRule);
        c.DrawRect(bounds, 1.0f);
        c.SetFont(fonts::Small());
        c.SetColor(theme::kMuted);
        c.DrawText(empty_text_, bounds.Reduced(14.0f, 0.0f), Justify::kCentredLeft, true);
        return;
    }
    float y = 0.0f;
    for (const EngineBlock &block : blocks_)
    {
        if (y >= bounds.bottom())
        {
            break;
        }
        const float block_height =
            static_cast<float>(kHeaderHeight + kLineHeight * static_cast<int>(block.lines.size()));
        const RectF frame{0.0f, y, bounds.width(), std::min(block_height, bounds.bottom() - y)};
        c.SetColor(theme::kPanel);
        c.FillRect(frame);

        // The header: the name, the chip, the figures at the right.
        const RectF header{0.0f, y, bounds.width(), static_cast<float>(kHeaderHeight)};
        c.SetFont(fonts::Body().Bold());
        c.SetColor(theme::kInk);
        const float name_width = c.TextWidth(c.CurrentFont(), block.name);
        c.DrawText(block.name, RectF{12.0f, y, name_width + 2.0f, header.height()}, Justify::kCentredLeft, false);
        float chip_right = 12.0f + name_width;
        if (!block.state.empty())
        {
            const auto  chip_font = fonts::Small().WithHeight(15.0f);
            const float chip_w    = c.TextWidth(chip_font, block.state) + 18.0f;
            const RectF chip{chip_right + 14.0f, y + (header.height() - 22.0f) / 2.0f, chip_w, 22.0f};
            const auto  colour = block.is_thinking ? theme::kAmber : theme::kMuted;
            c.SetColor(colour);
            c.DrawRoundedRect(chip, 11.0f, 1.0f);
            c.SetFont(chip_font);
            c.DrawText(block.state, chip, Justify::kCentred, false);
            chip_right = chip.right();
        }
        c.SetFont(fonts::Small());
        c.SetColor(theme::kMuted);
        c.DrawText(block.stats, RectF{chip_right + 14.0f, y, bounds.width() - chip_right - 26.0f, header.height()},
                   Justify::kCentredRight, true);
        c.SetColor(theme::kRule);
        c.DrawHorizontalLine(static_cast<int>(y + header.height()) - 1, 0.0f, bounds.width());

        // The lines: the best lit, the rest striped.
        float row_y = y + header.height();
        for (std::size_t index = 0; index < block.lines.size() && row_y < bounds.bottom(); ++index)
        {
            const AnalysisLine &line = block.lines[index];
            const RectF         row{0.0f, row_y, bounds.width(), static_cast<float>(kLineHeight)};
            if (index == 0)
            {
                c.SetColor(theme::kSelect);
                c.FillRect(row);
            }
            else if (index % 2 == 1)
            {
                c.SetColor(theme::kRow);
                c.FillRect(row);
            }
            c.SetFont(fonts::Body().WithHeight(18.0f));
            c.SetColor(theme::kMuted);
            c.DrawText(line.depth, RectF{0.0f, row_y, 74.0f, row.height()}, Justify::kCentredRight, false);
            c.SetColor(line.sign > 0 ? theme::kPlus : line.sign < 0 ? theme::kMinus : theme::kInk);
            c.DrawText(line.score, RectF{78.0f, row_y, 74.0f, row.height()}, Justify::kCentredRight, false);
            c.SetColor(theme::kInk);
            c.DrawText(line.pv, RectF{168.0f, row_y, bounds.width() - 180.0f, row.height()}, Justify::kCentredLeft,
                       true);
            row_y += row.height();
        }
        c.SetColor(theme::kRule);
        c.DrawRect(frame, 1.0f);
        y += block_height + static_cast<float>(kGap);
    }
}

} // namespace amphitheater::app
