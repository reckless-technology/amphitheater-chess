#pragma once
/// @file EngineLog.h The engine log: every line exchanged with the engines, in the monospaced face - what the host
/// wrote in the accent, what an engine wrote in the log's ink, an engine's errors in the error colour - kept to its
/// last few thousand lines, each wrapped to the log's width, and following the newest while it is scrolled to the
/// bottom.

#include "ListView.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace uci_host::app
{

/** Which way a logged line went. */
enum class LogDirection
{
    kToEngine,   ///< The host's command: "> Pawnstar  go ...".
    kFromEngine, ///< The engine's line: "< Pawnstar  info ...".
    kError,      ///< The engine's standard error, or the host's own word on an engine.
};

/**
 * Where a line breaks to fit a width: at the last space that fits, else wherever the width runs out, each row after the
 * first indented.
 * @param text The line, UTF-8.
 * @param columns The characters a row holds; 0 or less for no wrapping.
 * @param indent The characters every row after the first is indented by; held to half the row.
 * @return Each row's first byte and its length in bytes, in order; one row for an empty line.
 */
std::vector<std::pair<std::size_t, std::size_t>> WrapLine(std::string_view text, int columns, int indent);

/** The log's lines as the list shows them: a row for each part of a line wrapped to the log's width. */
class EngineLogModel final : public reckless::view::ListModel
{
  public:
    static constexpr std::size_t kMostLines = 4000; ///< Lines kept; the oldest go first.

    /**
     * A line, the oldest dropped past the most kept.
     * @param engine The engine's name.
     * @param direction Which way it went.
     * @param line The line.
     */
    void Add(std::string_view engine, LogDirection direction, std::string_view line);

    /** Every line dropped. */
    void Clear();

    /**
     * The width the lines wrap to, every line wrapped again when it moves.
     * @param columns The characters a row holds; 0 for no wrapping, each line one row cut with an ellipsis.
     * @return Whether the rows changed.
     */
    bool SetWrapColumns(int columns);

    /** @return The characters a row holds; 0 for no wrapping. */
    int wrap_columns() const
    {
        return wrap_columns_;
    }

    /** @return How many lines are kept. */
    std::size_t LineCount() const
    {
        return lines_.size();
    }

    /**
     * A line's text as shown, whole.
     * @param line The line, from the oldest kept.
     * @return "> Pawnstar  go wtime ...".
     */
    const std::string &Line(std::size_t line) const
    {
        return lines_[line].text;
    }

    std::size_t                         RowCount() const override;
    std::string                         CellText(std::size_t row, std::size_t column) const override;
    std::optional<reckless::gfx::Color> CellColour(std::size_t row, std::size_t column) const override;

  private:
    /** A kept line. */
    struct Entry
    {
        LogDirection  direction; ///< Which way it went.
        std::string   text;      ///< As shown.
        std::uint64_t serial;    ///< Counts every line ever added: its place once older lines have gone.
        int           indent;    ///< The characters before the line itself: "> Pawnstar  ".
    };

    /** A row: a part of a line. */
    struct Row
    {
        std::uint64_t serial;          ///< The line's.
        std::size_t   start;           ///< The part's first byte.
        std::size_t   length;          ///< Its length in bytes.
        bool          is_continuation; ///< Not the line's first part: indented.
    };

    /**
     * A line's rows, wrapped at the width in force, added after the rest.
     * @param entry The line.
     */
    void AppendRows(const Entry &entry);

    /**
     * The line a row is part of.
     * @param row The row.
     * @return The line.
     */
    const Entry &EntryOf(const Row &row) const
    {
        return lines_[static_cast<std::size_t>(row.serial - lines_.front().serial)];
    }

    std::deque<Entry> lines_;            ///< The lines, oldest first.
    std::deque<Row>   rows_;             ///< Their rows, in order.
    std::uint64_t     next_serial_  = 0; ///< The next line's serial.
    int               wrap_columns_ = 0; ///< The characters a row holds; 0 for no wrapping.
};

/** The log: the toolkit's list over the model, following the newest line, its lines wrapped to its width. */
class EngineLog
{
  public:
    EngineLog();

    /** Not copyable: the list holds the model by address. */
    EngineLog(const EngineLog &) = delete;
    /** Not assignable: the list holds the model by address. @return Never. */
    EngineLog &operator=(const EngineLog &) = delete;

    /**
     * A line, shown at once.
     * @param engine The engine's name.
     * @param direction Which way it went.
     * @param line The line.
     */
    void Add(std::string_view engine, LogDirection direction, std::string_view line);

    /** Every line dropped. */
    void Clear();

    /**
     * Whether long lines wrap (the default) or are cut with an ellipsis.
     * @param is_wrapping Wrap them.
     */
    void SetWrapping(bool is_wrapping);

    /** @return Whether long lines wrap. */
    bool is_wrapping() const
    {
        return is_wrapping_;
    }

    /**
     * The lines wrapped again to the list's width when it, or the width of the face as the window measures it, has
     * moved: after the list is laid out, and once a frame, since the window that measures the face may come later.
     */
    void Fit();

    /**
     * The log's height in a room: as many whole rows as fit, so a log following its newest line never shows a part
     * of a row at its top.
     * @param room The height there is, in pixels.
     * @return The height to give the log: at most the room, and at least one row.
     */
    int FittedHeight(int room) const;

    /** @return The list, for the window to add and lay out. */
    reckless::view::ListView &view()
    {
        return list_;
    }

    /** @return The lines. */
    const EngineLogModel &model() const
    {
        return model_;
    }

  private:
    EngineLogModel           model_;              ///< The lines.
    reckless::view::ListView list_;               ///< The list.
    bool                     is_wrapping_ = true; ///< Long lines wrap.
};

} // namespace uci_host::app
