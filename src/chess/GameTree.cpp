#include "GameTree.h"

#include "Notation.h"

#include <algorithm>

namespace amphitheater::chess
{

GameTree::GameTree() : GameTree(Position::Start())
{
}

GameTree::GameTree(const Position &start)
{
    Node root;
    root.position = start;
    nodes_.push_back(std::move(root));
}

GameTree GameTree::FromPgn(const PgnGame &game)
{
    GameTree tree(game.start);
    tree.AddPgnLine(kRoot, game.moves);
    return tree;
}

GameTree GameTree::FromGame(const Game &game)
{
    GameTree tree(game.StartPosition());
    NodeId   parent = kRoot;
    for (const Move move : game.Moves())
    {
        parent = tree.Add(parent, move);
        if (parent == kNone)
        {
            break;
        }
    }
    return tree;
}

std::vector<GameTree::NodeId> GameTree::LineThrough(NodeId id) const
{
    std::vector<NodeId> line;
    for (NodeId at = id; at != kRoot && at != kNone; at = node(at).parent)
    {
        line.push_back(at);
    }
    std::reverse(line.begin(), line.end());
    for (NodeId at = id; !node(at).children.empty();)
    {
        at = node(at).children.front();
        line.push_back(at);
    }
    return line;
}

Game GameTree::GameOf(const std::vector<NodeId> &line) const
{
    Game game(start());
    for (const NodeId id : line)
    {
        if (!game.Play(node(id).move))
        {
            break;
        }
    }
    return game;
}

GameTree::NodeId GameTree::Add(NodeId parent, Move move)
{
    for (const NodeId child : node(parent).children)
    {
        if (node(child).move == move)
        {
            return child;
        }
    }
    const Position &before = node(parent).position;
    const auto      legal  = before.GenerateLegalMoves();
    if (std::find(legal.begin(), legal.end(), move) == legal.end())
    {
        return kNone;
    }
    Node child;
    child.move     = move;
    child.san      = MoveToSan(before, move);
    child.position = before.MakeMove(move);
    child.parent   = parent;
    child.ply      = node(parent).ply + 1;
    const auto id  = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(std::move(child));
    nodes_[static_cast<std::size_t>(parent)].children.push_back(id);
    return id;
}

bool GameTree::IsMainContinuation(NodeId id) const
{
    if (id == kRoot)
    {
        return true;
    }
    const std::vector<NodeId> &siblings = node(node(id).parent).children;
    return !siblings.empty() && siblings.front() == id;
}

void GameTree::Promote(NodeId id)
{
    if (id == kRoot)
    {
        return;
    }
    std::vector<NodeId> &siblings = nodes_[static_cast<std::size_t>(node(id).parent)].children;
    const auto           at       = std::find(siblings.begin(), siblings.end(), id);
    if (at != siblings.end())
    {
        std::rotate(siblings.begin(), at, at + 1);
    }
}

void GameTree::Remove(NodeId id)
{
    if (id == kRoot)
    {
        return;
    }
    std::vector<NodeId> &siblings = nodes_[static_cast<std::size_t>(node(id).parent)].children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
}

void GameTree::Annotate(NodeId id, const PgnMove &move)
{
    Node &target          = nodes_[static_cast<std::size_t>(id)];
    target.nags           = move.nags;
    target.comment_before = move.comment_before;
    target.comment        = move.comment;
}

void GameTree::AddPgnLine(NodeId parent, const PgnLine &line)
{
    NodeId at = parent;
    for (const PgnMove &move : line)
    {
        const NodeId added = Add(at, move.move);
        if (added == kNone)
        {
            return; // the reader replays every move, so this does not happen
        }
        Annotate(added, move);
        // A move's variations are alternatives to it, from the position before it: more children of its parent.
        for (const PgnLine &variation : move.variations)
        {
            AddPgnLine(at, variation);
        }
        at = added;
    }
}

PgnLine GameTree::ToPgnLine() const
{
    return node(kRoot).children.empty() ? PgnLine{} : PgnLineFrom(kRoot, node(kRoot).children.front());
}

PgnLine GameTree::PgnLineFrom(NodeId parent, NodeId head) const
{
    PgnLine line;
    NodeId  before = parent;
    NodeId  at     = head;
    while (at != kNone)
    {
        const Node &current = node(at);
        PgnMove     move;
        move.move           = current.move;
        move.san            = current.san;
        move.nags           = current.nags;
        move.comment_before = current.comment_before;
        move.comment        = current.comment;
        // The variations leaving the line here, where the move is its parent's main continuation; a variation's own
        // first move has its fellows written by the line one level up.
        if (IsMainContinuation(at))
        {
            for (const NodeId other : node(before).children)
            {
                if (other != at)
                {
                    move.variations.push_back(PgnLineFrom(before, other));
                }
            }
        }
        line.push_back(std::move(move));
        before = at;
        at     = current.children.empty() ? kNone : current.children.front();
    }
    return line;
}

} // namespace amphitheater::chess
