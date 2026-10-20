#pragma once
/// @file GameTree.h A game with its variations: every line played from a starting position, as a tree of moves. Each
/// move keeps what PGN writes around it - its comments and its NAGs - and the position after it; a move's first child
/// is its line's main continuation and the others are the variations that leave the line there. The tree is read from
/// and written to PgnGame, so a game opened from a file keeps its variations through an edit and a save, and a move
/// played away from the line shown adds a variation where the host once cut the game short.
///
/// Nodes are named by index and never move: a node taken off is only unlinked from its parent, so every id held stays
/// valid (and unreachable) until the tree is replaced.

#include "Game.h"
#include "Move.h"
#include "Pgn.h"
#include "Position.h"

#include <cstddef>
#include <string>
#include <vector>

namespace amphitheater::chess
{

/** A game with its variations. */
class GameTree
{
  public:
    using NodeId = int; ///< A node's index.

    static constexpr NodeId kRoot = 0;  ///< The position before the first move.
    static constexpr NodeId kNone = -1; ///< No node.

    /** One move of the tree, or the root before every move. */
    struct Node
    {
        Move                move = Move::None(); ///< The move; none at the root.
        std::string         san;                 ///< The move in SAN; empty at the root.
        Position            position;            ///< The position after the move; the start at the root.
        std::vector<int>    nags;                ///< Its numeric annotation glyphs.
        std::string         comment_before;      ///< A comment before it, at the start of a line.
        std::string         comment;             ///< A comment after it.
        NodeId              parent = kNone;      ///< The node it follows; none at the root.
        std::vector<NodeId> children;            ///< What may follow it: the main continuation first.
        int                 ply = 0;             ///< Its depth: 1 for a first move, 0 at the root.
    };

    /** A tree of the starting position and no moves. */
    GameTree();

    /** @param start The position before the first move. */
    explicit GameTree(const Position &start);

    /**
     * A PGN game's every line: its main line, and every variation where it leaves its line.
     * @param game The game, as the PGN reader gives it (every move legal).
     * @return The tree.
     */
    static GameTree FromPgn(const PgnGame &game);

    /**
     * A game's moves as the tree's one line.
     * @param game The game.
     * @return The tree.
     */
    static GameTree FromGame(const Game &game);

    /** @return The position before the first move. */
    const Position &start() const
    {
        return nodes_[kRoot].position;
    }

    /**
     * @param id A node.
     * @return The node.
     */
    const Node &node(NodeId id) const
    {
        return nodes_[static_cast<std::size_t>(id)];
    }

    /**
     * The line through a node: the moves from the first to the node, then on along each move's main continuation to
     * the line's end.
     * @param id The node; the root for the main line.
     * @return The line's nodes, the first move first; empty for a tree of no moves.
     */
    std::vector<NodeId> LineThrough(NodeId id) const;

    /** @return The main line's nodes. */
    std::vector<NodeId> MainLine() const
    {
        return LineThrough(kRoot);
    }

    /**
     * A line's moves as a game.
     * @param line The line's nodes, each the child of the one before (the first a child of the root).
     * @return The game.
     */
    Game GameOf(const std::vector<NodeId> &line) const;

    /**
     * Play a move after a node: the child that already holds it, or a new child - the main continuation where the node
     * had none, else a variation after the others.
     * @param parent The node it follows.
     * @param move The move.
     * @return The child, or kNone where the move is not legal there.
     */
    NodeId Add(NodeId parent, Move move);

    /**
     * @param id A node.
     * @return Whether it is its parent's main continuation (the root counts as one).
     */
    bool IsMainContinuation(NodeId id) const;

    /**
     * Make a variation its parent's main continuation, the line it was the main continuation of becoming its first
     * variation; the others keep their order.
     * @param id The variation's first node.
     */
    void Promote(NodeId id);

    /**
     * Take a node and everything after it off the tree.
     * @param id The node; not the root.
     */
    void Remove(NodeId id);

    /** @return The main line with its variations, as PgnGame holds them. */
    PgnLine ToPgnLine() const;

    /**
     * Keep the comments and the NAGs of a PGN move on a node.
     * @param id The node.
     * @param move The PGN move.
     */
    void Annotate(NodeId id, const PgnMove &move);

  private:
    /**
     * A PGN line's moves added after a node, each with its variations.
     * @param parent The node the line follows.
     * @param line The line.
     */
    void AddPgnLine(NodeId parent, const PgnLine &line);

    /**
     * A line as PGN writes it, from a node's child on along the main continuations, each move with the variations
     * that leave it there.
     * @param parent The node the line follows.
     * @param head The line's first node: the parent's main continuation, or one of its variations, whose fellow
     *             variations the line one level up already holds.
     * @return The line.
     */
    PgnLine PgnLineFrom(NodeId parent, NodeId head) const;

    std::vector<Node> nodes_; ///< Every node, the root first; a node removed stays, unlinked.
};

} // namespace amphitheater::chess
