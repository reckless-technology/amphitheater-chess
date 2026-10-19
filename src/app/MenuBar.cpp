#include "MenuBar.h"

#include "Theme.h"

#include "PanelStyle.h"

namespace uci_host::app
{

namespace
{

using reckless::gfx::RectF;

constexpr float kLeft    = 8.0f;  ///< The first title's left edge.
constexpr float kPadding = 11.0f; ///< Each side of a title's text.
constexpr float kGap     = 2.0f;  ///< Between titles.

/** @return The titles' font. */
reckless::gfx::Font TitleFont()
{
    return fonts::Body().WithHeight(18.5f);
}

} // namespace

MenuBar::MenuBar()
{
    SetOpaque(true);
}

void MenuBar::SetEntries(std::vector<MenuBarEntry> entries)
{
    entries_ = std::move(entries);
    Repaint();
}

RectF MenuBar::TitleBounds(std::size_t index) const
{
    const reckless::view::Host *host = this->host();
    const auto                  font = TitleFont();
    float                       x    = kLeft;
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const std::string &title = entries_[i].title;
        const float        text =
            host != nullptr ? host->TextWidth(font, title) : 0.5f * font.height * static_cast<float>(title.size());
        const RectF bounds{x, 3.0f, text + 2.0f * kPadding, static_cast<float>(height()) - 6.0f};
        if (i == index)
        {
            return bounds;
        }
        x = bounds.right() + kGap;
    }
    return {};
}

int MenuBar::EntryAt(float x) const
{
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const RectF bounds = TitleBounds(i);
        if (x >= bounds.x() && x < bounds.right())
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MenuBar::Open(std::size_t index)
{
    if (index >= entries_.size())
    {
        return;
    }
    session_.reset();
    open_             = static_cast<int>(index);
    const RectF title = TitleBounds(index);
    auto        items = entries_[index].items ? entries_[index].items() : std::vector<reckless::view::MenuItem>();
    const auto  style = reckless::panel::MenuStyle(theme::Lists());
    session_          = reckless::view::MenuSession::Open(*this, std::move(items), style,
                                                          {static_cast<int>(title.x()), height()}, [this](int id) {
                                                     open_ = -1;
                                                     session_.reset(); // its owner may destroy it here
                                                     Repaint();
                                                     if (id != 0 && on_command)
                                                     {
                                                         on_command(id);
                                                     }
                                                          });
    if (!session_)
    {
        open_ = -1; // no window to show it in
    }
    Repaint();
}

void MenuBar::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat();
    c.FillAll(theme::kPanel);
    c.SetColor(theme::kRule);
    c.DrawHorizontalLine(height() - 1, 0.0f, bounds.width());
    c.SetFont(TitleFont());
    float x = kLeft;
    for (std::size_t i = 0; i < entries_.size(); ++i)
    {
        const float text = c.TextWidth(c.CurrentFont(), entries_[i].title);
        const RectF title{x, 3.0f, text + 2.0f * kPadding, bounds.height() - 6.0f};
        if (static_cast<int>(i) == open_ || static_cast<int>(i) == hover_)
        {
            c.SetColor(static_cast<int>(i) == open_ ? theme::kSelect : theme::kRow);
            c.FillRoundedRect(title, 4.0f);
        }
        c.SetColor(theme::kInk);
        c.DrawText(entries_[i].title, title, reckless::gfx::Justify::kCentred, false);
        x = title.right() + kGap;
    }
}

void MenuBar::MouseDown(const reckless::view::MouseEvent &e)
{
    const int index = EntryAt(e.position.x);
    if (index >= 0)
    {
        Open(static_cast<std::size_t>(index));
    }
}

void MenuBar::MouseMove(const reckless::view::MouseEvent &e)
{
    const int index = EntryAt(e.position.x);
    if (index != hover_)
    {
        hover_ = index;
        Repaint();
    }
}

void MenuBar::MouseExit(const reckless::view::MouseEvent &e)
{
    static_cast<void>(e);
    if (hover_ != -1)
    {
        hover_ = -1;
        Repaint();
    }
}

} // namespace uci_host::app
