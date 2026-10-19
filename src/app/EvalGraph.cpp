#include "EvalGraph.h"

#include "Theme.h"

#include "Path.h"

#include <algorithm>
#include <cmath>

namespace uci_host::app
{

namespace
{

using reckless::gfx::Justify;
using reckless::gfx::PointF;
using reckless::gfx::RectF;

/** The largest score the scale reaches, in pawns. */
constexpr double kMostScale = 10.0;

} // namespace

EvalGraph::EvalGraph()
{
    cursor = reckless::view::Cursor::kPointingHand;
}

void EvalGraph::SetScores(std::vector<std::optional<double>> scores, int current_ply, std::string caption)
{
    scores_      = std::move(scores);
    current_ply_ = current_ply;
    caption_     = std::move(caption);
    Repaint();
}

double EvalGraph::Scale() const
{
    double largest = 1.0;
    for (const auto &score : scores_)
    {
        if (score)
        {
            largest = std::max(largest, std::min(kMostScale, std::ceil(std::abs(*score))));
        }
    }
    return largest;
}

RectF EvalGraph::PlotArea() const
{
    const RectF bounds = LocalBounds().ToFloat();
    return RectF::LeftTopRightBottom(42.0f, 14.0f, bounds.right() - 12.0f, bounds.bottom() - 26.0f);
}

std::optional<int> EvalGraph::PlyAt(float x) const
{
    if (scores_.empty())
    {
        return std::nullopt;
    }
    const RectF area = PlotArea();
    if (scores_.size() == 1)
    {
        return 1;
    }
    const float t     = std::clamp((x - area.x()) / std::max(1.0f, area.width()), 0.0f, 1.0f);
    const int   index = static_cast<int>(std::lround(t * static_cast<float>(scores_.size() - 1)));
    return index + 1;
}

void EvalGraph::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    c.SetColor(theme::kPanel);
    c.FillRect(bounds);
    c.SetColor(theme::kRule);
    c.DrawRect(bounds, 1.0f);

    const RectF  area  = PlotArea();
    const double scale = Scale();
    const auto   y_of  = [&](double pawns) {
        const double held = std::clamp(pawns, -scale, scale);
        return area.y() + area.height() * static_cast<float>(0.5 - held / (2.0 * scale));
    };
    const std::size_t count = scores_.size();
    const auto        x_of  = [&](std::size_t index) {
        return count <= 1 ? area.x()
                          : area.x() + area.width() * static_cast<float>(index) / static_cast<float>(count - 1);
    };

    // The scale: the edges dashed, zero solid, their figures at the left.
    c.SetFont(fonts::Small().WithHeight(14.0f));
    c.SetColor(theme::kMuted);
    const std::string top = "+" + std::to_string(static_cast<int>(scale));
    c.DrawText(top, RectF{4.0f, y_of(scale) - 9.0f, 34.0f, 18.0f}, Justify::kCentredRight, false);
    c.DrawText("0", RectF{4.0f, y_of(0.0) - 9.0f, 34.0f, 18.0f}, Justify::kCentredRight, false);
    c.DrawText("-" + std::to_string(static_cast<int>(scale)), RectF{4.0f, y_of(-scale) - 9.0f, 34.0f, 18.0f},
               Justify::kCentredRight, false);
    reckless::gfx::Stroke dashed(1.0f);
    dashed.dashes = {3.0f, 4.0f};
    c.SetColor(theme::kRule);
    for (const double edge : {scale, -scale})
    {
        reckless::gfx::Path line;
        line.MoveTo(area.x(), y_of(edge));
        line.LineTo(area.right(), y_of(edge));
        c.StrokePath(line, dashed);
    }
    c.SetColor(reckless::gfx::Color(0xff6b665du));
    c.DrawLine(PointF{area.x(), y_of(0.0)}, PointF{area.right(), y_of(0.0)}, 1.0f);

    // The area to zero and the line, through every known score.
    reckless::gfx::Path line;
    reckless::gfx::Path fill;
    bool                started = false;
    float               last_x  = area.x();
    for (std::size_t index = 0; index < count; ++index)
    {
        if (!scores_[index])
        {
            continue;
        }
        const PointF point{x_of(index), y_of(*scores_[index])};
        if (!started)
        {
            line.MoveTo(point);
            fill.MoveTo(point.x, y_of(0.0));
            started = true;
        }
        else
        {
            line.LineTo(point);
        }
        fill.LineTo(point);
        last_x = point.x;
    }
    if (started)
    {
        fill.LineTo(last_x, y_of(0.0));
        fill.Close();
        c.SetColor(theme::kAmber.WithAlpha(0.18f));
        c.FillPath(fill);
        c.SetColor(theme::kAmber);
        c.StrokePath(line, reckless::gfx::Stroke(2.0f, reckless::gfx::Stroke::Join::kCurved,
                                                 reckless::gfx::Stroke::Cap::kRounded));
    }

    // The position shown.
    if (current_ply_ >= 1 && static_cast<std::size_t>(current_ply_) <= count && scores_[current_ply_ - 1])
    {
        const PointF mark{x_of(static_cast<std::size_t>(current_ply_ - 1)), y_of(*scores_[current_ply_ - 1])};
        c.SetColor(theme::kAmber);
        c.FillEllipse(RectF{mark.x - 4.5f, mark.y - 4.5f, 9.0f, 9.0f});
    }

    c.SetFont(fonts::Small().WithHeight(14.0f));
    c.SetColor(theme::kMuted);
    const RectF captions{area.x(), area.bottom() + 4.0f, area.width(), 20.0f};
    c.DrawText(count == 0 ? std::string() : "move 1", captions, Justify::kCentredLeft, false);
    c.DrawText(caption_, captions, Justify::kCentredRight, true);
}

void EvalGraph::MouseDown(const reckless::view::MouseEvent &e)
{
    if (const auto ply = PlyAt(e.position.x); ply && on_select)
    {
        on_select(*ply);
    }
}

} // namespace uci_host::app
