// The dialogs' controls as a person drives them: a drop-down's choice told, a check box ticked by a release over it, a
// caption that turns to an error, and a dialog's Escape and Return.

#include "app/Controls.h"

#include <gtest/gtest.h>

namespace uci_host::app
{

TEST(Controls, ADropDownTellsAChoiceThatMovesAndNothingElse)
{
    DropDown         list;
    std::vector<int> told;
    list.on_change = [&](int index) { told.push_back(index); };
    EXPECT_EQ(list.current(), -1);
    EXPECT_EQ(list.Text(), "");
    list.SetChoices({"One", "Two", "Three"}, 7);
    EXPECT_EQ(list.current(), 2) << "held to the choices";
    list.Choose(0);
    list.Choose(0);
    list.Choose(9);
    EXPECT_EQ(told, std::vector<int>{0});
    EXPECT_EQ(list.Text(), "One");
    reckless::view::MouseEvent press;
    list.MouseDown(press); // in no window, no menu opens
    EXPECT_EQ(list.current(), 0);
}

TEST(Controls, ACheckBoxTicksOnAReleaseOverItAndACaptionTurnsToAnError)
{
    CheckBox          box("Ponder");
    std::vector<bool> told;
    box.on_change = [&](bool is_checked) { told.push_back(is_checked); };
    box.SetBounds({0, 0, 200, 30});
    reckless::view::MouseEvent release;
    release.position = {10.0f, 10.0f};
    box.MouseUp(release);
    EXPECT_TRUE(box.checked());
    release.position = {300.0f, 10.0f};
    box.MouseUp(release);
    EXPECT_TRUE(box.checked()) << "a release off the box does nothing";
    box.SetChecked(false);
    EXPECT_EQ(told, std::vector<bool>{true}) << "SetChecked tells nobody";

    Caption caption("Note", true);
    caption.SetText("Wrong");
    caption.SetError(true);
    EXPECT_EQ(caption.text(), "Wrong");
    EXPECT_FALSE(caption.intercepts_self());
}

TEST(Controls, ADialogTakesEscapeAndReturnAndLaysItsButtonsOutAtItsFoot)
{
    DialogFrame dialog("Title", 400, 300);
    dialog.SetBounds({0, 0, 1000, 800});
    int   accepted   = 0;
    int   cancelled  = 0;
    auto &ok         = dialog.AddButton("OK", [&] { ++accepted; });
    dialog.on_accept = [&] { ++accepted; };
    dialog.on_cancel = [&] { ++cancelled; };
    EXPECT_TRUE(dialog.CardBounds().Contains(ok.bounds()));
    EXPECT_GT(ok.bounds().y(), dialog.ContentArea().bottom() - 1);
    EXPECT_TRUE(dialog.KeyPressed({reckless::view::Key::kReturn, 0, {}}));
    EXPECT_TRUE(dialog.KeyPressed({reckless::view::Key::kEscape, 0, {}}));
    EXPECT_FALSE(dialog.KeyPressed({reckless::view::Key::kLeft, 0, {}}));
    EXPECT_EQ(accepted, 1);
    EXPECT_EQ(cancelled, 1);
    dialog.SetCardHeight(500);
    EXPECT_EQ(dialog.CardBounds().height(), 500);
    EXPECT_TRUE(dialog.WantsKeyboardFocus());
}

} // namespace uci_host::app
