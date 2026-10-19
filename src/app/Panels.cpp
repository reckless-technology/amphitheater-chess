#include "Panels.h"

#include "Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace uci_host::app
{

namespace
{

using reckless::gfx::Justify;
using reckless::gfx::RectF;

/** The rounded card every strip and card is drawn on: the panel's colour inside a rule. */
void DrawCard(reckless::gfx::Canvas &c, RectF bounds, float radius)
{
    c.SetColor(theme::kPanel);
    c.FillRoundedRect(bounds.Reduced(0.5f), radius);
    c.SetColor(theme::kRule);
    c.DrawRoundedRect(bounds.Reduced(0.5f), radius, 1.0f);
}

} // namespace

std::string ClockText(long long milliseconds)
{
    const long long total = std::max(0LL, milliseconds);
    char            text[32];
    if (total >= 3600000)
    {
        const long long seconds = total / 1000;
        std::snprintf(text, sizeof text, "%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
    else
    {
        const long long tenths = total / 100;
        std::snprintf(text, sizeof text, "%lld:%02lld.%lld", tenths / 600, tenths / 10 % 60, tenths % 10);
    }
    return text;
}

SectionLabel::SectionLabel(std::string text)
{
    for (char &character : text)
    {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    text_ = std::move(text);
    SetInterceptsMouse(false, false);
}

void SectionLabel::Paint(reckless::gfx::Canvas &c)
{
    c.SetFont(fonts::Label());
    c.SetColor(theme::kInk);
    c.DrawText(text_, LocalBounds().ToFloat(), Justify::kCentredLeft, true);
}

PlayerStrip::PlayerStrip()
{
    SetInterceptsMouse(false, false);
}

void PlayerStrip::SetState(PlayerStripState state)
{
    const bool same = state.is_white == state_.is_white && state.name == state_.name &&
                      state.details == state_.details && state.clock == state_.clock &&
                      state.is_running == state_.is_running && state.has_lost == state_.has_lost;
    if (!same)
    {
        state_ = std::move(state);
        Repaint();
    }
}

RectF PlayerStrip::ClockBounds() const
{
    const float h          = static_cast<float>(height());
    const float face       = std::min(h - 12.0f, 38.0f);
    const float face_width = 128.0f;
    return {static_cast<float>(width()) - face_width - 10.0f, (h - face) / 2.0f, face_width, face};
}

void PlayerStrip::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    DrawCard(c, bounds, 6.0f);
    const float h = bounds.height();

    const RectF dot{12.0f, h / 2.0f - 7.0f, 14.0f, 14.0f};
    c.SetColor(state_.is_white ? theme::kWhiteSide : theme::kBlackSide);
    c.FillEllipse(dot);
    c.SetColor(reckless::gfx::Color(0xff000000u));
    c.DrawEllipse(dot, 1.0f);

    const RectF clock = ClockBounds();
    const float left  = dot.right() + 12.0f;
    const float right = state_.clock.empty() ? bounds.right() - 10.0f : clock.x() - 10.0f;
    c.SetFont(fonts::Body().Bold());
    c.SetColor(theme::kInk);
    c.DrawText(state_.name, RectF{left, 4.0f, right - left, h * 0.5f - 2.0f}, Justify::kBottomLeft, true);
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    c.DrawText(state_.details, RectF{left, h * 0.5f + 1.0f, right - left, h * 0.5f - 5.0f}, Justify::kTopLeft, true);

    if (!state_.clock.empty())
    {
        c.SetColor(state_.is_running ? theme::kAmber : theme::kBoardEdge);
        c.FillRoundedRect(clock, 4.0f);
        c.SetFont(fonts::Clock());
        c.SetColor(state_.is_running ? reckless::gfx::Color(0xff17140fu)
                                     : (state_.has_lost ? theme::kMinus : theme::kMuted));
        c.DrawText(state_.clock, clock.Reduced(8.0f, 0.0f), Justify::kCentredRight, false);
    }
}

GameCard::GameCard()
{
    SetInterceptsMouse(false, false);
}

void GameCard::SetLines(std::vector<CardLine> lines)
{
    lines_ = std::move(lines);
    Repaint();
}

namespace
{

constexpr float kCardPadding  = 12.0f; ///< Inside the card's sides.
constexpr float kCardTop      = 10.0f; ///< Above the first line and below the last.
constexpr float kCardLine     = 22.0f; ///< A value's line.
constexpr float kCardSpacing  = 3.0f;  ///< Between one label's value and the next.
constexpr float kCardLabelGap = 16.0f; ///< Between the labels and the values.

/** @return A value's face: the interface's, or the monospaced family for a FEN. */
reckless::gfx::Font ValueFont(bool is_mono)
{
    return is_mono ? fonts::Mono().WithHeight(15.0f) : fonts::Small().WithHeight(17.0f);
}

} // namespace

float GameCard::Measure(const reckless::gfx::Font &font, std::string_view text) const
{
    if (const reckless::view::Host *h = host())
    {
        return h->TextWidth(font, text);
    }
    return 0.5f * font.height * static_cast<float>(text.size());
}

GameCard::Columns GameCard::ColumnsAt(float width) const
{
    float label_width = 0.0f;
    for (const CardLine &line : lines_)
    {
        label_width = std::max(label_width, Measure(fonts::Small(), line.label));
    }
    const float value_x = kCardPadding + label_width + kCardLabelGap;
    return {label_width, value_x, std::max(40.0f, width - value_x - kCardPadding)};
}

std::vector<std::string> GameCard::ValueLines(const CardLine &line, float width) const
{
    const reckless::gfx::Font font = ValueFont(line.is_mono);
    std::vector<std::string>  lines;
    std::string               current;
    // Words added while the line fits; a word wider than a line alone broken where the line runs out.
    std::size_t at = 0;
    while (at < line.value.size())
    {
        const std::size_t space = line.value.find(' ', at);
        const std::string word  = line.value.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at                      = space == std::string::npos ? line.value.size() : space + 1;
        const std::string tried = current.empty() ? word : current + " " + word;
        if (Measure(font, tried) <= width)
        {
            current = tried;
            continue;
        }
        if (!current.empty())
        {
            lines.push_back(current);
        }
        current = word;
        while (current.size() > 1 && Measure(font, current) > width)
        {
            std::size_t fits = current.size() - 1;
            while (fits > 1 && Measure(font, std::string_view(current).substr(0, fits)) > width)
            {
                --fits;
            }
            lines.push_back(current.substr(0, fits));
            current.erase(0, fits);
        }
    }
    if (!current.empty() || lines.empty())
    {
        lines.push_back(current);
    }
    return lines;
}

int GameCard::PreferredHeight(int width) const
{
    const Columns columns = ColumnsAt(static_cast<float>(width));
    float         height  = 2.0f * kCardTop;
    for (const CardLine &line : lines_)
    {
        height += kCardLine * static_cast<float>(ValueLines(line, columns.value_width).size()) + kCardSpacing;
    }
    return static_cast<int>(std::ceil(height));
}

void GameCard::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    c.SetColor(theme::kPanel);
    c.FillRect(bounds);
    c.SetColor(theme::kRule);
    c.DrawRect(bounds, 1.0f);

    const Columns columns = ColumnsAt(bounds.width());
    float         y       = kCardTop;
    for (const CardLine &line : lines_)
    {
        c.SetFont(fonts::Small());
        c.SetColor(theme::kMuted);
        c.DrawText(line.label, RectF{kCardPadding, y, columns.label_width, kCardLine}, Justify::kCentredLeft, false);
        c.SetFont(ValueFont(line.is_mono));
        c.SetColor(theme::kInk);
        for (const std::string &part : ValueLines(line, columns.value_width))
        {
            c.DrawText(part, RectF{columns.value_x, y, columns.value_width + 2.0f, kCardLine}, Justify::kCentredLeft,
                       false);
            y += kCardLine;
        }
        y += kCardSpacing;
    }
}

StatusBar::StatusBar()
{
    SetOpaque(true);
    SetInterceptsMouse(false, false);
}

void StatusBar::SetItems(std::vector<std::string> items)
{
    if (items != items_)
    {
        items_ = std::move(items);
        Repaint();
    }
}

void StatusBar::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    c.FillAll(theme::kTitleBar);
    c.SetColor(theme::kRule);
    c.DrawHorizontalLine(0, 0.0f, bounds.width());
    c.SetFont(fonts::Small().WithHeight(15.0f));
    c.SetColor(theme::kMuted);
    float x = 14.0f;
    for (const std::string &item : items_)
    {
        const float width = c.TextWidth(c.CurrentFont(), item);
        c.DrawText(item, RectF{x, 0.0f, std::max(0.0f, bounds.right() - x), bounds.height()}, Justify::kCentredLeft,
                   true);
        x += width + 22.0f;
        if (x > bounds.right())
        {
            break;
        }
    }
}

} // namespace uci_host::app
