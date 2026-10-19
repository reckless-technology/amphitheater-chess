#pragma once

// A dialog's buttons found by their text and pressed as a person presses them: a press and a release over the button.

#include "Button.h"
#include "View.h"

#include <string_view>

namespace uci_host::app::test
{

/**
 * The button of a view, or of a view inside it, with this text.
 * @param view The view searched, with every view under it.
 * @param text The button's text.
 * @return The button; null where there is none.
 */
inline reckless::view::Button *ButtonOf(reckless::view::View &view, std::string_view text)
{
    if (auto *button = dynamic_cast<reckless::view::Button *>(&view); button != nullptr && button->text() == text)
    {
        return button;
    }
    for (reckless::view::View *child : view.children())
    {
        if (auto *found = ButtonOf(*child, text))
        {
            return found;
        }
    }
    return nullptr;
}

/**
 * A press and a release in the middle of a button.
 * @param button The button.
 */
inline void Press(reckless::view::Button &button)
{
    reckless::view::MouseEvent event;
    event.position = {static_cast<float>(button.width()) / 2.0f, static_cast<float>(button.height()) / 2.0f};
    event.down     = event.position;
    button.MouseDown(event);
    button.MouseUp(event);
}

} // namespace uci_host::app::test
