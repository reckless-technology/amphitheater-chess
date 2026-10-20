#pragma once
/// @file Controls.h The controls the window's dialogs are made of, in the window's look: a drop-down of named choices,
/// a check box, a line of text to type into, a caption, and the dialog's frame - a veil over the window and a card in
/// its middle with a title and buttons at its foot.

#include "Button.h"
#include "Menu.h"
#include "TextField.h"
#include "View.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** A plate showing one of several named choices; a press opens a menu of them under it. */
class DropDown final : public reckless::view::View
{
  public:
    DropDown();

    std::function<void(int index)> on_change; ///< A person chose another.

    /**
     * The choices and the one shown; nothing is told.
     * @param choices The names.
     * @param current The one shown.
     */
    void SetChoices(std::vector<std::string> choices, int current);

    /**
     * Choose one, as a person does: told when it moves.
     * @param index The choice.
     */
    void Choose(int index);

    /** @return The choice shown, or -1 with none. */
    int current() const
    {
        return current_;
    }

    /** @return The choices. */
    const std::vector<std::string> &choices() const
    {
        return choices_;
    }

    /** @return The choice shown's name, or empty. */
    std::string Text() const;

    /**
     * The plate, the name and the arrow.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Opens the menu.
     * @param e The press.
     */
    void MouseDown(const reckless::view::MouseEvent &e) override;

  private:
    std::vector<std::string>                     choices_;      ///< The names.
    int                                          current_ = -1; ///< The one shown.
    std::unique_ptr<reckless::view::MenuSession> menu_;         ///< The open menu.
};

/** A box that is ticked or not, with its text beside it. */
class CheckBox final : public reckless::view::View
{
  public:
    /** @param text The text beside the box. */
    explicit CheckBox(std::string text);

    std::function<void(bool is_checked)> on_change; ///< A person ticked or cleared it.

    /** @param is_checked Whether it is ticked; nothing is told. */
    void SetChecked(bool is_checked);

    /** @return Whether it is ticked. */
    bool checked() const
    {
        return is_checked_;
    }

    /** Tick or clear it as a person does: told. */
    void Toggle();

    /**
     * The box and the text.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * A release over it ticks or clears it.
     * @param e The release.
     */
    void MouseUp(const reckless::view::MouseEvent &e) override;

  private:
    std::string text_;               ///< The text.
    bool        is_checked_ = false; ///< Ticked.
};

/** A line of text, as a caption beside a control or a note under it; it takes no mouse. */
class Caption final : public reckless::view::View
{
  public:
    /**
     * @param text The text.
     * @param is_muted Whether it is in the muted ink (a note) rather than the ink (a caption).
     */
    explicit Caption(std::string text = {}, bool is_muted = false);

    /** @param text The text; repaints. */
    void SetText(std::string text);

    /** @return The text. */
    const std::string &text() const
    {
        return text_;
    }

    /** @param is_error Whether it is an error's: in the error colour. */
    void SetError(bool is_error);

    /**
     * The text, at the left, wrapped onto the lines the height allows.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  private:
    std::string text_;             ///< The text.
    bool        is_muted_ = false; ///< In the muted ink.
    bool        is_error_ = false; ///< In the error colour.
};

/**
 * A number typed into a line: "1", "0.5", "-2.25".
 * @param text The line's text.
 * @return The number; none for anything else, a space before or after it included.
 */
std::optional<double> ParseDecimal(const std::string &text);

/** @return A line of text's look in the window: the panel's colour, the ink, the accent round it while typed into. */
reckless::view::TextField::Style FieldStyle();

/** @return A button in the window's look. */
std::unique_ptr<reckless::view::Button> MakePlate(std::string text, std::function<void()> clicked,
                                                  bool is_toggle = false);

/**
 * A dialog over the window: a veil over everything that takes every press, and a card in its middle with its title,
 * its content laid out by the dialog in ContentArea(), and its buttons at the foot, the affirmative at the right.
 * Escape cancels; Return accepts. It is added to the window by its owner and removed when its owner destroys it.
 */
class DialogFrame : public reckless::view::View
{
  public:
    /**
     * @param title The card's title.
     * @param card_width The card's width, in design pixels.
     * @param card_height The card's height, in design pixels.
     */
    DialogFrame(std::string title, int card_width, int card_height);

    std::function<void()> on_accept; ///< Return, or the affirmative button.
    std::function<void()> on_cancel; ///< Escape, or the negative button.

    /**
     * A button at the foot, laid out right to left in the order added.
     * @param text Its text.
     * @param clicked What it does.
     * @param width Its width, in design pixels.
     * @return The button, owned by the frame.
     */
    reckless::view::Button &AddButton(std::string text, std::function<void()> clicked, int width = 110);

    /** @return The card, in the frame's coordinates. */
    reckless::gfx::RectI CardBounds() const;

    /** @return Where the content goes: inside the card, under the title, over the buttons. */
    reckless::gfx::RectI ContentArea() const;

    /** Once a display frame, while the dialog is up: what it waits on, moved on (an engine's handshake). */
    virtual void Frame()
    {
    }

    /** @param height The card's height, in design pixels; lays it out again. */
    void SetCardHeight(int height);

    /** Lays the buttons out and asks the dialog to lay its content out (LayOutContent). */
    void Resized() override;

    /**
     * The veil, the card and the title.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /** @return True: the dialog takes the keys while it is up. */
    bool WantsKeyboardFocus() const override
    {
        return true;
    }

    /**
     * Escape cancels, Return accepts.
     * @param key The key.
     * @return Whether it was used.
     */
    bool KeyPressed(const reckless::view::KeyEvent &key) override;

  protected:
    /**
     * The dialog's content laid out.
     * @param area Where it goes.
     */
    virtual void LayOutContent(reckless::gfx::RectI area)
    {
        static_cast<void>(area);
    }

  private:
    std::string                                                          title_;       ///< The title.
    int                                                                  card_width_;  ///< The card's width.
    int                                                                  card_height_; ///< The card's height.
    std::vector<std::pair<int, std::unique_ptr<reckless::view::Button>>> buttons_;     ///< Width and button.
};

} // namespace amphitheater::app
