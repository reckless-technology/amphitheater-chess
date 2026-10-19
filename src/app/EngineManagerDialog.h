#pragma once
/// @file EngineManagerDialog.h Engines > Manage engines: the configured engines in a list, and the one selected in an
/// editor - its name, arguments, pondering, analysis, depth limit and own Polyglot book, and every UCI option it
/// declared at its handshake as a control of its kind (check, spin, combo, button, string). Adding an engine chooses
/// its program and runs its handshake on a worker thread, which fills its name, its author and its options.

#include "Controls.h"
#include "EngineCatalogue.h"

#include "ListView.h"

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** What an engine's handshake told: its name, its author, its options; or why it failed. */
struct Handshake
{
    bool                           is_ok = false; ///< Whether the engine started and answered.
    std::string                    error;         ///< Why not.
    std::string                    name;          ///< Its "id name".
    std::string                    author;        ///< Its "id author".
    std::vector<uci::EngineOption> options;       ///< The options it declared.
};

/**
 * Run an engine's handshake: start it, read what it declares, and quit it. Blocks: a worker thread's.
 * @param config How to start it.
 * @return What it told.
 */
Handshake RunHandshake(uci::EngineConfig config);

/** One option of an engine as a control of its kind, with its name beside it. */
class OptionRow final : public reckless::view::View
{
  public:
    /**
     * @param option The option as the engine declared it.
     * @param value Its value as configured, or its default.
     * @param is_set Whether it is configured (a button: pressed at every start).
     */
    OptionRow(const uci::EngineOption &option, const std::string &value, bool is_set);

    /** Told the option's new value, or nothing to configure it no longer (a button released, a value at default). */
    std::function<void(const std::string &name, std::optional<std::string> value)> on_change;

    /** @return The option. */
    const uci::EngineOption &option() const
    {
        return option_;
    }

    /** @return The check box, for a check. */
    CheckBox *check()
    {
        return check_.get();
    }

    /** @return The line, for a spin or a string. */
    reckless::view::TextField *field()
    {
        return field_.get();
    }

    /** @return The list, for a combo. */
    DropDown *combo()
    {
        return combo_.get();
    }

    /** @return The plate, for a button. */
    reckless::view::Button *button()
    {
        return button_.get();
    }

    /** @return The note beside a spin's line: its range, or why a value was refused. */
    const Caption &note() const
    {
        return note_;
    }

    /** Lays the name, the control and the note out. */
    void Resized() override;

  private:
    /**
     * A value from the control: refused with the reason in the note, or told.
     * @param value The value.
     */
    void Tell(const std::string &value);

    uci::EngineOption                          option_; ///< The option.
    Caption                                    name_;   ///< Its name.
    std::unique_ptr<CheckBox>                  check_;  ///< A check's box.
    std::unique_ptr<reckless::view::TextField> field_;  ///< A spin's or a string's line.
    std::unique_ptr<DropDown>                  combo_;  ///< A combo's list.
    std::unique_ptr<reckless::view::Button>    button_; ///< A button's plate.
    Caption                                    note_;   ///< A spin's range, or a refusal.
};

/** A pane of rows that the wheel scrolls. */
class ScrollPane final : public reckless::view::View
{
  public:
    /** @param rows The rows, in order, each its height; owned by the caller; replaces any shown. */
    void SetRows(std::vector<std::pair<reckless::view::View *, int>> rows);

    /** @param offset How far down the rows are scrolled, held to what the rows need. */
    void SetScroll(int offset);

    /** @return How far down the rows are scrolled. */
    int scroll() const
    {
        return scroll_;
    }

    /** Lays the rows out at the scroll. */
    void Resized() override;

    /**
     * A thin bar at the right while some rows are hidden.
     * @param c The canvas.
     */
    void PaintOverChildren(reckless::gfx::Canvas &c) override;

    /**
     * The wheel scrolls.
     * @param e The wheel.
     * @return Whether it scrolled.
     */
    bool MouseWheel(const reckless::view::MouseEvent &e) override;

  private:
    /** @return The rows' whole height. */
    int ContentHeight() const;

    std::vector<std::pair<reckless::view::View *, int>> rows_;       ///< The rows and their heights.
    int                                                 scroll_ = 0; ///< The scroll.
};

/** The engine manager. */
class EngineManagerDialog final : public DialogFrame
{
  public:
    /** @param catalogue The engines as configured; the dialog edits a copy. */
    explicit EngineManagerDialog(EngineCatalogue catalogue);

    std::function<void(const EngineCatalogue &catalogue)> on_save;  ///< Save pressed: the engines as edited.
    std::function<void()>                                 on_close; ///< Cancelled.

    /** @return The engines as edited. */
    const EngineCatalogue &catalogue() const
    {
        return catalogue_;
    }

    /** @return The engine selected, or -1. */
    int selected() const
    {
        return selected_;
    }

    /**
     * Select an engine, its editor shown.
     * @param index The engine, from 0.
     */
    void Select(int index);

    /**
     * Add an engine by its program: its handshake runs on a worker thread, and the engine is added, selected, when it
     * answers (Frame).
     * @param path The program.
     */
    void AddEngine(const std::string &path);

    /** Run the selected engine's handshake again: its name, author and options read afresh. */
    void Detect();

    /** Remove the selected engine. */
    void RemoveSelected();

    /** @return Whether a handshake is under way. */
    bool IsDetecting() const
    {
        return handshake_.valid();
    }

    /**
     * The selected engine's option row by the option's name.
     * @param name The name.
     * @return The row, or null.
     */
    OptionRow *Row(const std::string &name);

    /** @return The name's line. */
    reckless::view::TextField &name_field()
    {
        return name_;
    }

    /** @return The depth limit's line. */
    reckless::view::TextField &depth_field()
    {
        return depth_;
    }

    /** @return The book depth's line. */
    reckless::view::TextField &book_depth_field()
    {
        return book_depth_;
    }

    /** @return The ponder box. */
    CheckBox &ponder()
    {
        return ponder_;
    }

    /** @return The analysis box. */
    CheckBox &analyse()
    {
        return analyse_;
    }

    /**
     * The book: a Polyglot file, or none.
     * @param path The file; empty for none.
     */
    void SetBook(const std::string &path);

    /** @return What the note at the foot says. */
    const std::string &note() const
    {
        return note_.text();
    }

    /** @return What the line under the program says of the selected engine: its author, and whether it is bundled. */
    const std::string &about() const
    {
        return author_.text();
    }

    /** Takes in a handshake that has answered. */
    void Frame() override;

    /** Save pressed: every line's value taken, the engines handed on. */
    void Save();

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    /** The list's rows. */
    class EnginesModel final : public reckless::view::ListModel
    {
      public:
        /** @param catalogue The engines; outlives the model. */
        explicit EnginesModel(const EngineCatalogue &catalogue) : catalogue_(catalogue)
        {
        }

        std::size_t RowCount() const override;
        std::string CellText(std::size_t row, std::size_t column) const override;

      private:
        const EngineCatalogue &catalogue_; ///< The engines.
    };

    /** The editor brought to the selected engine. */
    void ShowSelected();

    /** The editor's lines taken into the selected engine (the name, the arguments, the limits). */
    void TakeLines();

    /** @return The selected engine, or null. */
    EngineEntry *Selected();

    EngineCatalogue                         catalogue_;        ///< The engines as edited.
    EnginesModel                            model_;            ///< The list's rows.
    reckless::view::ListView                list_;             ///< The engines.
    std::unique_ptr<reckless::view::Button> add_;              ///< Add...
    std::unique_ptr<reckless::view::Button> remove_;           ///< Remove.
    std::unique_ptr<reckless::view::Button> detect_;           ///< Detect.
    Caption                                 program_;          ///< The program's path.
    Caption                                 author_;           ///< The engine's author.
    Caption                                 name_label_;       ///< "Name".
    reckless::view::TextField               name_;             ///< The name to show.
    Caption                                 args_label_;       ///< "Arguments".
    reckless::view::TextField               arguments_;        ///< The program's arguments.
    CheckBox                                ponder_;           ///< Ponder.
    CheckBox                                analyse_;          ///< Use for analysis.
    Caption                                 depth_label_;      ///< "Depth limit".
    reckless::view::TextField               depth_;            ///< The depth limit.
    Caption                                 book_label_;       ///< "Book".
    Caption                                 book_;             ///< The book's file.
    std::unique_ptr<reckless::view::Button> choose_book_;      ///< Choose...
    std::unique_ptr<reckless::view::Button> clear_book_;       ///< Clear.
    Caption                                 book_depth_label_; ///< "Book depth".
    reckless::view::TextField               book_depth_;       ///< Book moves before this ply.
    Caption                                 options_label_;    ///< "Options".
    ScrollPane                              options_;          ///< The option rows.
    std::vector<std::unique_ptr<OptionRow>> rows_;             ///< The selected engine's options.
    Caption                                 note_;             ///< What is going on, or what went wrong.
    int                                     selected_ = -1;    ///< The engine selected.
    std::future<Handshake>                  handshake_;        ///< A handshake under way.
    std::optional<int>                      detecting_;        ///< The engine it is for; none for a new one.
    std::string                             adding_path_;      ///< A new engine's program.
};

} // namespace uci_host::app
