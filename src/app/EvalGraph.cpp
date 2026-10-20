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

/** The legend's band above the plot, in design pixels. */
constexpr float kLegendHeight = 18.0f;

} // namespace

EvalGraph::EvalGraph()
{
    cursor = reckless::view::Cursor::kPointingHand;
}

reckless::gfx::Color EvalGraph::SeriesColour(std::size_t index)
{
    switch (index % 4)
    {
    case 0:
        return theme::kAmber;
    case 1:
        return theme::kSeriesBlue;
    case 2:
        return theme::kSeriesViolet;
    default:
        return theme::kPlus;
    }
}

void EvalGraph::SetSeries(std::vector<EvalSeries> series, int ply_count, int current_ply, std::string caption)
{
    series_      = std::move(series);
    ply_count_   = std::max(0, ply_count);
    current_ply_ = current_ply;
    caption_     = std::move(caption);
    Repaint();
}

double EvalGraph::Scale() const
{
    double largest = 1.0;
    for (const EvalSeries &one : series_)
    {
        for (const auto &point : one.points)
        {
            if (point && !point->mate)
            {
                largest = std::max(largest, std::min(kMostScale, std::ceil(std::abs(point->pawns))));
            }
        }
    }
    return largest;
}

bool EvalGraph::HasLegend() const
{
    return std::any_of(series_.begin(), series_.end(), [](const EvalSeries &one) { return !one.name.empty(); });
}

RectF EvalGraph::PlotArea() const
{
    const RectF bounds = LocalBounds().ToFloat();
    const float top    = 14.0f + (HasLegend() ? kLegendHeight : 0.0f);
    return RectF::LeftTopRightBottom(42.0f, top, bounds.right() - 12.0f, bounds.bottom() - 26.0f);
}

std::optional<int> EvalGraph::PlyAt(float x) const
{
    if (ply_count_ <= 0)
    {
        return std::nullopt;
    }
    const RectF area = PlotArea();
    if (ply_count_ == 1)
    {
        return 1;
    }
    const float t     = std::clamp((x - area.x()) / std::max(1.0f, area.width()), 0.0f, 1.0f);
    const int   index = static_cast<int>(std::lround(t * static_cast<float>(ply_count_ - 1)));
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
    const std::size_t count = static_cast<std::size_t>(ply_count_);
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

    // Each series: the area to zero (fainter where two or more share the graph) and the line through its own points,
    // so two engines' series interleave ply by ply.
    const float fill_alpha = series_.size() > 1 ? 0.09f : 0.18f;
    for (std::size_t which = 0; which < series_.size(); ++which)
    {
        const EvalSeries          &one    = series_[which];
        const reckless::gfx::Color colour = SeriesColour(which);
        reckless::gfx::Path        line;
        reckless::gfx::Path        fill;
        bool                       started = false;
        float                      last_x  = area.x();
        for (std::size_t index = 0; index < count && index < one.points.size(); ++index)
        {
            if (!one.points[index])
            {
                continue;
            }
            const PointF point{x_of(index), y_of(one.points[index]->pawns)};
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
        if (!started)
        {
            continue;
        }
        fill.LineTo(last_x, y_of(0.0));
        fill.Close();
        c.SetColor(colour.WithAlpha(fill_alpha));
        c.FillPath(fill);
        c.SetColor(colour);
        c.StrokePath(line, reckless::gfx::Stroke(2.0f, reckless::gfx::Stroke::Join::kCurved,
                                                 reckless::gfx::Stroke::Cap::kRounded));
        // A mate is held at the scale's edge and marked there by a small triangle pointing off the scale.
        for (std::size_t index = 0; index < count && index < one.points.size(); ++index)
        {
            const auto &point = one.points[index];
            if (!point || !point->mate)
            {
                continue;
            }
            const float         x    = x_of(index);
            const float         edge = y_of(point->pawns);
            const float         out  = point->pawns > 0.0 ? -1.0f : 1.0f;
            reckless::gfx::Path mark;
            mark.MoveTo(x, edge + out * 6.0f);
            mark.LineTo(x - 4.5f, edge - out * 1.5f);
            mark.LineTo(x + 4.5f, edge - out * 1.5f);
            mark.Close();
            c.FillPath(mark);
        }
    }

    // The position shown, on every series with a point there.
    if (current_ply_ >= 1 && static_cast<std::size_t>(current_ply_) <= count)
    {
        const std::size_t index = static_cast<std::size_t>(current_ply_ - 1);
        for (std::size_t which = 0; which < series_.size(); ++which)
        {
            const EvalSeries &one = series_[which];
            if (index < one.points.size() && one.points[index])
            {
                const PointF mark{x_of(index), y_of(one.points[index]->pawns)};
                c.SetColor(theme::kPanel);
                c.FillEllipse(RectF{mark.x - 6.0f, mark.y - 6.0f, 12.0f, 12.0f});
                c.SetColor(SeriesColour(which));
                c.FillEllipse(RectF{mark.x - 4.5f, mark.y - 4.5f, 9.0f, 9.0f});
            }
        }
    }

    // The legend: each series' name in its colour, after a stroke of it, along the top.
    if (HasLegend())
    {
        const auto  font  = fonts::Small().WithHeight(14.0f);
        const float share = (bounds.width() - area.x() - 12.0f) / static_cast<float>(series_.size());
        float       x     = area.x();
        c.SetFont(font);
        for (std::size_t which = 0; which < series_.size(); ++which)
        {
            const reckless::gfx::Color colour = SeriesColour(which);
            const float                middle = 6.0f + kLegendHeight / 2.0f;
            c.SetColor(colour);
            c.DrawLine(PointF{x, middle}, PointF{x + 14.0f, middle}, 2.5f);
            const float room = std::min(share - 34.0f, c.TextWidth(font, series_[which].name) + 2.0f);
            c.DrawText(series_[which].name, RectF{x + 19.0f, 6.0f, std::max(0.0f, room), kLegendHeight},
                       Justify::kCentredLeft, true);
            x += std::min(share, room + 33.0f);
        }
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
