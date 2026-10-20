#pragma once
/// @file UciProtocol.h The Universal Chess Interface as text: the engine's lines parsed into structures, the host's
/// commands formatted from them.
///
/// The parsers never fail on what an engine writes: a field they cannot read is left unset and the line's other
/// fields still read, since an engine that prints one malformed number should not lose its whole line.

#include "chess/ChessTypes.h"
#include "chess/Result.h"
#include "chess/TimeControl.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amphitheater::uci
{

/**
 * Split a line into its whitespace-separated words.
 * @param line The line.
 * @return The words.
 */
std::vector<std::string_view> SplitWords(std::string_view line);

/** The kinds of engine option UCI defines. */
enum class OptionType
{
    kCheck,  ///< A boolean, "true" or "false".
    kSpin,   ///< An integer between a minimum and a maximum.
    kCombo,  ///< One of a list of strings.
    kButton, ///< An action with no value.
    kString, ///< Any text.
};

/**
 * An option type's UCI name.
 * @param type The type.
 * @return "check", "spin", "combo", "button" or "string".
 */
std::string OptionTypeName(OptionType type);

/** An option an engine declares with "option name ... type ...". */
struct EngineOption
{
    std::string                 name;                       ///< Its name, which may hold spaces.
    OptionType                  type = OptionType::kString; ///< Its type.
    std::string                 default_value; ///< Its default ("<empty>" read as empty), empty for a button.
    std::optional<std::int64_t> minimum;       ///< A spin's minimum.
    std::optional<std::int64_t> maximum;       ///< A spin's maximum.
    std::vector<std::string>    choices;       ///< A combo's values.

    /**
     * Whether a value suits the option: "true" or "false" for a check, an integer in range for a spin, one of the
     * choices (in any case) for a combo, nothing for a button, anything for a string.
     * @param value The value.
     * @return Why not, or nothing when it suits.
     */
    std::optional<std::string> Validate(std::string_view value) const;
};

/**
 * Read an "option" line.
 * @param line The line, starting with "option".
 * @return The option, or why the line is not one (no name, no type, a type UCI does not define).
 */
Result<EngineOption> ParseOptionLine(std::string_view line);

/**
 * A score in an "info" line. As UCI defines it, an engine's score is from the point of view of the side to move in
 * the position it searches: positive when that side is better, whichever colour it is. Everything the host shows or
 * records turns it to White's point of view first (WhitesView).
 */
struct Score
{
    /** What the value counts. */
    enum class Kind
    {
        kCentipawns, ///< Hundredths of a pawn, from the engine's side.
        kMate,       ///< Moves to mate: positive when the engine mates, negative when it is mated.
    };

    Kind kind           = Kind::kCentipawns; ///< What the value counts.
    int  value          = 0;                 ///< The value.
    bool is_lower_bound = false;             ///< The true score is at least this ("lowerbound").
    bool is_upper_bound = false;             ///< The true score is at most this ("upperbound").

    /**
     * The score as a person reads it: "+0.35" in pawns, "#5" or "#-3" for a mate.
     * @return The text.
     */
    std::string ToString() const;

    bool operator==(const Score &) const = default; ///< Scores are equal when every field is.
};

/**
 * A score turned from the side to move's point of view, as an engine reports it, to White's: + when White is better,
 * - when Black is. With White to move the score is unchanged; with Black to move its value changes sign - a mate in n
 * for Black becomes mate in -n, Black mating - and its bounds swap, since a lower bound on Black's score is an upper
 * bound on White's. A mate in 0 (the side to move is mated, as some engines say of a mated position) keeps its value 0.
 * The turn is its own inverse: applied to a White's-view score with Black to move, it gives Black's view back.
 * @param score The score, from the side to move's point of view.
 * @param side_to_move The side to move in the position searched.
 * @return The score from White's point of view.
 */
Score WhitesView(const Score &score, chess::Color side_to_move);

/**
 * A score from White's point of view as a person reads it: "+0.35" in pawns, "#5" or "#-3" for a mate, each with
 * " lowerbound" or " upperbound" after it when it is a bound.
 * @param score The score, from the side to move's point of view.
 * @param side_to_move The side to move in the position searched.
 * @return The text.
 */
std::string WhiteScoreWords(const Score &score, chess::Color side_to_move);

/** Every field UCI defines for an "info" line; a field the line leaves out is unset. */
struct SearchInfo
{
    std::optional<int>          depth;               ///< "depth": the search depth in plies.
    std::optional<int>          selective_depth;     ///< "seldepth".
    std::optional<int>          multipv;             ///< "multipv": which line of a multi-line search.
    std::optional<Score>        score;               ///< "score cp|mate [lowerbound|upperbound]".
    std::optional<std::int64_t> nodes;               ///< "nodes".
    std::optional<std::int64_t> nodes_per_second;    ///< "nps".
    std::optional<int>          hash_full;           ///< "hashfull", in permille.
    std::optional<std::int64_t> tablebase_hits;      ///< "tbhits".
    std::optional<std::int64_t> time;                ///< "time", in milliseconds.
    std::optional<int>          cpu_load;            ///< "cpuload", in permille.
    std::vector<std::string>    pv;                  ///< "pv": the principal variation, in UCI notation.
    std::optional<std::string>  current_move;        ///< "currmove".
    std::optional<int>          current_move_number; ///< "currmovenumber".
    std::optional<std::string>  text;                ///< "string": the rest of the line, as written.
};

/**
 * Read an "info" line.
 * @param line The line, starting with "info".
 * @return Its fields; unknown keywords and unreadable values are skipped.
 */
SearchInfo ParseInfoLine(std::string_view line);

/** A "bestmove" line. */
struct BestMove
{
    std::string move;                  ///< The move as written: "(none)" or "0000" when the engine has none, empty
                                       ///< when the line names nothing.
    std::optional<std::string> ponder; ///< The move it would ponder on, if it named one.
};

/**
 * Read a "bestmove" line.
 * @param line The line.
 * @return The move (empty when the line names none), or nothing when the line does not start with "bestmove".
 */
std::optional<BestMove> ParseBestMoveLine(std::string_view line);

/** What "go" asks for. */
struct GoParameters
{
    std::optional<chess::Milliseconds> white_time;          ///< "wtime".
    std::optional<chess::Milliseconds> black_time;          ///< "btime".
    std::optional<chess::Milliseconds> white_increment;     ///< "winc".
    std::optional<chess::Milliseconds> black_increment;     ///< "binc".
    std::optional<int>                 moves_to_go;         ///< "movestogo".
    std::optional<int>                 depth;               ///< "depth".
    std::optional<std::int64_t>        nodes;               ///< "nodes".
    std::optional<int>                 mate;                ///< "mate": search for a mate in this many moves.
    std::optional<chess::Milliseconds> move_time;           ///< "movetime".
    bool                               is_infinite = false; ///< "infinite": until "stop".
    bool                               is_ponder   = false; ///< "ponder": think on the opponent's time.
    std::vector<std::string>           search_moves;        ///< "searchmoves": only these, in UCI notation.
};

/**
 * The "go" command.
 * @param parameters What to ask for; nothing set gives a bare "go".
 * @return The line.
 */
std::string FormatGo(const GoParameters &parameters);

/**
 * The "position" command.
 * @param fen The starting position's FEN, or empty for "startpos".
 * @param moves The moves since, in UCI notation.
 * @return The line.
 */
std::string FormatPosition(std::string_view fen, const std::vector<std::string> &moves);

/**
 * The "setoption" command.
 * @param name The option's name.
 * @param value Its value; empty for a button.
 * @return The line.
 */
std::string FormatSetOption(std::string_view name, std::string_view value);

} // namespace amphitheater::uci
