#pragma once
/// @file EngineLog.h The engine log: every line exchanged with the engines, in the monospaced face - what the host
/// wrote in the accent, what an engine wrote in the log's ink, an engine's errors in the error colour - kept to its
/// last few thousand lines and following the newest while it is scrolled to the bottom.

#include "ListView.h"

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace uci_host::app
{

/** Which way a logged line went. */
enum class LogDirection
{
    kToEngine,   ///< The host's command: "> Pawnstar  go ...".
    kFromEngine, ///< The engine's line: "< Pawnstar  info ...".
    kError,      ///< The engine's standard error, or the host's own word on an engine.
};

/** The log's lines as the list shows them. */
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
    void Clear()
    {
        lines_.clear();
    }

    /**
     * A line's text as shown.
     * @param row The line, from the oldest kept.
     * @return "> Pawnstar  go wtime ...".
     */
    const std::string &Line(std::size_t row) const
    {
        return lines_[row].text;
    }

    std::size_t                         RowCount() const override;
    std::string                         CellText(std::size_t row, std::size_t column) const override;
    std::optional<reckless::gfx::Color> CellColour(std::size_t row, std::size_t column) const override;

  private:
    /** A kept line. */
    struct Entry
    {
        LogDirection direction; ///< Which way it went.
        std::string  text;      ///< As shown.
    };

    std::deque<Entry> lines_; ///< The lines, oldest first.
};

/** The log: the toolkit's list over the model, following the newest line. */
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
    EngineLogModel           model_; ///< The lines.
    reckless::view::ListView list_;  ///< The list.
};

} // namespace uci_host::app
