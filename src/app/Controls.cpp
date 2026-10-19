#include "Controls.h"

#include "Theme.h"

#include "PanelStyle.h"
#include "Path.h"

#include <algorithm>

namespace uci_host::app
{

namespace
{

using reckless::gfx::Justify;
using reckless::gfx::RectF;

constexpr int kTitleHeight  = 48; ///< The card's title band.
constexpr int kFooterHeight = 58; ///< The card's button band.
constexpr int kPadding      = 20; ///< Inside the card.

} // namespace

DropDown::DropDown()
{
    cursor = reckless::view::Cursor::kPointingHand;
}

void DropDown::SetChoices(std::vector<std::string> choices, int current)
{
    choices_ = std::move(choices);
    current_ = choices_.empty() ? -1 : std::clamp(current, 0, static_cast<int>(choices_.size()) - 1);
    Repaint();
}

void DropDown::Choose(int index)
{
    if (index < 0 || index >= static_cast<int>(choices_.size()) || index == current_)
    {
        return;
    }
    current_ = index;
    Repaint();
    if (on_change)
    {
        on_change(index);
    }
}

std::string DropDown::Text() const
{
    return current_ >= 0 ? choices_[static_cast<std::size_t>(current_)] : std::string();
}

void DropDown::Paint(reckless::gfx::Canvas &c)
{
    const RectF bounds = LocalBounds().ToFloat().Reduced(0.5f);
    c.SetColor(IsEnabled() ? theme::kPlate : theme::kPanel);
    c.FillRoundedRect(bounds, 4.0f);
    c.SetColor(menu_ ? theme::kAmber : theme::kAmberDim);
    c.DrawRoundedRect(bounds, 4.0f, 1.0f);
    c.SetFont(fonts::Small().WithHeight(17.0f));
    c.SetColor(IsEnabled() ? theme::kInk : theme::kFaint);
    c.DrawText(Text(), RectF{10.0f, 0.0f, bounds.width() - 34.0f, bounds.height()}, Justify::kCentredLeft, true);
    reckless::gfx::Path arrow;
    const float         x = bounds.right() - 14.0f;
    const float         y = bounds.CentreY();
    arrow.AddTriangle({x - 5.0f, y - 2.5f}, {x + 5.0f, y - 2.5f}, {x, y + 3.5f});
    c.FillPath(arrow);
}

void DropDown::MouseDown(const reckless::view::MouseEvent &e)
{
    static_cast<void>(e);
    if (!IsEnabled() || choices_.empty())
    {
        return;
    }
    std::vector<reckless::view::MenuItem> items;
    for (std::size_t index = 0; index < choices_.size(); ++index)
    {
        reckless::view::MenuItem item;
        item.text   = choices_[index];
        item.id     = static_cast<int>(index) + 1;
        item.ticked = static_cast<int>(index) == current_;
        items.push_back(std::move(item));
    }
    auto style      = reckless::panel::MenuStyle(theme::Lists());
    style.min_width = width();
    menu_           = reckless::view::MenuSession::Open(*this, std::move(items), style, {0, height()}, [this](int id) {
        menu_.reset();
        Repaint();
        if (id > 0)
        {
            Choose(id - 1);
        }
    });
    Repaint();
}

CheckBox::CheckBox(std::string text) : text_(std::move(text))
{
    cursor = reckless::view::Cursor::kPointingHand;
}

void CheckBox::SetChecked(bool is_checked)
{
    if (is_checked != is_checked_)
    {
        is_checked_ = is_checked;
        Repaint();
    }
}

void CheckBox::Toggle()
{
    is_checked_ = !is_checked_;
    Repaint();
    if (on_change)
    {
        on_change(is_checked_);
    }
}

void CheckBox::Paint(reckless::gfx::Canvas &c)
{
    const float h = static_cast<float>(height());
    const RectF box{1.0f, (h - 18.0f) / 2.0f, 18.0f, 18.0f};
    c.SetColor(theme::kPlate);
    c.FillRoundedRect(box, 3.0f);
    c.SetColor(is_checked_ ? theme::kAmber : theme::kAmberDim);
    c.DrawRoundedRect(box, 3.0f, 1.0f);
    if (is_checked_)
    {
        reckless::gfx::Path tick;
        tick.MoveTo(box.x() + 4.0f, box.CentreY());
        tick.LineTo(box.x() + 7.5f, box.bottom() - 4.5f);
        tick.LineTo(box.right() - 4.0f, box.y() + 4.5f);
        c.SetColor(theme::kAmber);
        c.StrokePath(tick, reckless::gfx::Stroke(2.2f, reckless::gfx::Stroke::Join::kCurved,
                                                 reckless::gfx::Stroke::Cap::kRounded));
    }
    c.SetFont(fonts::Small().WithHeight(17.0f));
    c.SetColor(IsEnabled() ? theme::kInk : theme::kFaint);
    c.DrawText(text_, RectF{box.right() + 9.0f, 0.0f, static_cast<float>(width()) - box.right() - 9.0f, h},
               Justify::kCentredLeft, true);
}

void CheckBox::MouseUp(const reckless::view::MouseEvent &e)
{
    if (IsEnabled() && LocalBounds().ToFloat().Contains(e.position))
    {
        Toggle();
    }
}

Caption::Caption(std::string text, bool is_muted) : text_(std::move(text)), is_muted_(is_muted)
{
    SetInterceptsMouse(false, false);
}

void Caption::SetText(std::string text)
{
    if (text != text_)
    {
        text_ = std::move(text);
        Repaint();
    }
}

void Caption::SetError(bool is_error)
{
    if (is_error != is_error_)
    {
        is_error_ = is_error;
        Repaint();
    }
}

void Caption::Paint(reckless::gfx::Canvas &c)
{
    c.SetFont(fonts::Small().WithHeight(17.0f));
    c.SetColor(is_error_ ? theme::kMinus : is_muted_ ? theme::kMuted : theme::kInk);
    const int lines = std::max(1, height() / 22);
    if (lines == 1)
    {
        c.DrawText(text_, LocalBounds().ToFloat(), Justify::kCentredLeft, true);
    }
    else
    {
        c.DrawFittedText(text_, LocalBounds(), Justify::kTopLeft, lines, 1.0f);
    }
}

reckless::view::TextField::Style FieldStyle()
{
    reckless::view::TextField::Style style;
    style.font             = fonts::Small().WithHeight(17.0f);
    style.text             = theme::kInk;
    style.background       = theme::kBoardEdge;
    style.outline          = theme::kRule;
    style.focused_outline  = theme::kAmber;
    style.highlight        = theme::kSelect;
    style.highlighted_text = theme::kInk;
    style.caret            = theme::kAmber;
    style.justify          = Justify::kCentredLeft;
    style.left_indent      = 8;
    return style;
}

std::unique_ptr<reckless::view::Button> MakePlate(std::string text, std::function<void()> clicked, bool is_toggle)
{
    return std::make_unique<reckless::view::Button>(std::move(text), theme::PlatePainter(), std::move(clicked),
                                                    is_toggle);
}

DialogFrame::DialogFrame(std::string title, int card_width, int card_height)
    : title_(std::move(title)), card_width_(card_width), card_height_(card_height)
{
    SetOpaque(false);
}

reckless::view::Button &DialogFrame::AddButton(std::string text, std::function<void()> clicked, int width)
{
    auto button = MakePlate(std::move(text), std::move(clicked));
    AddChild(*button);
    buttons_.emplace_back(width, std::move(button));
    Resized();
    return *buttons_.back().second;
}

reckless::gfx::RectI DialogFrame::CardBounds() const
{
    const int w = std::min(card_width_, width() - 40);
    const int h = std::min(card_height_, height() - 40);
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

reckless::gfx::RectI DialogFrame::ContentArea() const
{
    const auto card = CardBounds();
    return {card.x() + kPadding, card.y() + kTitleHeight, card.width() - 2 * kPadding,
            card.height() - kTitleHeight - kFooterHeight};
}

void DialogFrame::SetCardHeight(int height)
{
    card_height_ = height;
    Resized();
    Repaint();
}

void DialogFrame::Resized()
{
    const auto card = CardBounds();
    int        x    = card.right() - kPadding;
    for (auto &[button_width, button] : buttons_)
    {
        x -= button_width;
        button->SetBounds({x, card.bottom() - kFooterHeight + 13, button_width, 32});
        x -= 10;
    }
    LayOutContent(ContentArea());
}

void DialogFrame::Paint(reckless::gfx::Canvas &c)
{
    c.FillAll(reckless::gfx::Color(0x99000000u));
    const RectF card = CardBounds().ToFloat();
    c.SetColor(theme::kWindow);
    c.FillRoundedRect(card, 8.0f);
    c.SetColor(theme::kRule);
    c.DrawRoundedRect(card.Reduced(0.5f), 8.0f, 1.0f);
    c.SetFont(fonts::Body().Bold());
    c.SetColor(theme::kInk);
    c.DrawText(title_, RectF{card.x() + kPadding, card.y(), card.width() - 2 * kPadding, kTitleHeight - 4.0f},
               Justify::kCentredLeft, true);
    c.SetColor(theme::kRule);
    c.DrawHorizontalLine(static_cast<int>(card.y()) + kTitleHeight - 6, card.x() + kPadding, card.right() - kPadding);
}

bool DialogFrame::KeyPressed(const reckless::view::KeyEvent &key)
{
    if (key.key == reckless::view::Key::kEscape && on_cancel)
    {
        on_cancel();
        return true;
    }
    if (key.key == reckless::view::Key::kReturn && on_accept)
    {
        on_accept();
        return true;
    }
    return false;
}

} // namespace uci_host::app
