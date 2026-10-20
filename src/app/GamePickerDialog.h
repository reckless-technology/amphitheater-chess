#pragma once
/// @file GamePickerDialog.h File > Open PGN with more than one game in the file: the games in a list - their number,
/// players, result, event, date and length - one picked to show on the board.

#include "Controls.h"
#include "chess/Pgn.h"

#include "ListView.h"

#include <functional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** The games of a PGN file, as the list shows them. */
class GamesModel final : public reckless::view::ListModel
{
  public:
    /** @param games The games; outlive the model. */
    explicit GamesModel(const std::vector<chess::PgnGame> &games) : games_(games)
    {
    }

    std::size_t RowCount() const override;
    std::string CellText(std::size_t row, std::size_t column) const override;

  private:
    const std::vector<chess::PgnGame> &games_; ///< The games.
};

/** The dialog that picks a game from a file of many. */
class GamePickerDialog final : public DialogFrame
{
  public:
    /**
     * @param games The file's games.
     * @param file_name The file's name, for the title.
     * @param skipped How many games could not be read, said under the list.
     */
    GamePickerDialog(std::vector<chess::PgnGame> games, const std::string &file_name, std::size_t skipped);

    std::function<void(const chess::PgnGame &game)> on_open;  ///< A game picked.
    std::function<void()>                           on_close; ///< Cancelled.

    /** @return The games. */
    const std::vector<chess::PgnGame> &games() const
    {
        return games_;
    }

    /** @return The list. */
    reckless::view::ListView &list()
    {
        return list_;
    }

    /**
     * Open a game: told, by its index.
     * @param index The game, from 0.
     */
    void Open(std::size_t index);

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    std::vector<chess::PgnGame> games_; ///< The games.
    GamesModel                  model_; ///< The list's rows.
    reckless::view::ListView    list_;  ///< The games' list.
    Caption                     note_;  ///< How many there are, and how many were skipped.
};

} // namespace amphitheater::app
