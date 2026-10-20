#include "Showcase.h"

#include "Theme.h"
#include "chess/Game.h"
#include "chess/Notation.h"

#include <iterator>

namespace uci_host::app
{

void ShowSample(MainView &view)
{
    chess::Game game;
    for (const char *san : {"e4",  "c5",  "Nf3", "d6",  "d4",  "cxd4", "Nxd4",  "Nf6",  "Nc3", "a6", "Be3", "e5",
                            "Nb3", "Be6", "f3",  "Be7", "Qd2", "O-O",  "O-O-O", "Nbd7", "g4",  "b5", "g5",  "b4"})
    {
        game.PlaySan(san);
    }
    const int ply = game.PlyCount();
    view.SetBoard(game.CurrentPosition(), game.Moves().back(), false);
    view.SetMoves(game.SanMoves(), 1, false, ply);

    const auto from_uci = [&game](const char *text) {
        const auto move = chess::ParseUciMove(game.CurrentPosition(), text);
        return move ? *move : chess::Move::None();
    };
    const chess::Move best = from_uci("c3e2");
    view.SetArrows({{best.From(), best.To(), theme::kAmber.WithAlpha(0.85f)}});

    PlayerStripState white;
    white.is_white   = true;
    white.name       = "Pawnstar 2.1";
    white.details    = "White \xc2\xb7 engine \xc2\xb7 Threads 1, Hash 64 MB, book";
    white.clock      = ClockText(251600);
    white.is_running = true;
    PlayerStripState black;
    black.is_white = false;
    black.name     = "Zenith 0.9";
    black.details  = "Black \xc2\xb7 engine \xc2\xb7 Threads 1, Hash 64 MB, ponder";
    black.clock    = ClockText(238900);
    view.SetPlayers(white, black);

    view.SetCard({{"Event", "Engine match, game 7 of 20"},
                  {"Opening", "B90 Sicilian, Najdorf, English Attack"},
                  {"Time control", "5 min + 3 s"},
                  {"Book", "openings-6ply.epd, left after move 3"},
                  {"Match score", "Pawnstar 3\xc2\xbd - 2\xc2\xbd Zenith (Elo +35 \xc2\xb1 140)"},
                  {"FEN", game.CurrentPosition().ToFen(), true}});

    EngineBlock pawnstar;
    pawnstar.name  = "Pawnstar 2.1";
    pawnstar.state = "thinking 4.8 s";
    pawnstar.stats = "depth 24/38 \xc2\xb7 11.2 Mn \xc2\xb7 2.31 Mn/s \xc2\xb7 hash 34 %";
    pawnstar.lines = {{"24/38", "+0.42", 1, "13.Ne2 Ne8 14.f4 a5 15.f5 a4 16.Nbd4 exd4 17.Nxd4 b3 18.Kb1"},
                      {"24/35", "+0.31", 1, "13.gxf6 bxc3 14.Qxc3 Nxf6 15.Kb1 a5 16.Bh3 Bxh3 17.Rhg1"},
                      {"23/33", "+0.08", 1, "13.Nd5 Nxd5 14.exd5 Bf5 15.Bd3 Bxd3 16.Qxd3 Nc5"}};
    EngineBlock zenith;
    zenith.name        = "Zenith 0.9";
    zenith.state       = "pondering 13.Ne2";
    zenith.is_thinking = false;
    zenith.stats       = "depth 21/33 \xc2\xb7 6.4 Mn \xc2\xb7 1.48 Mn/s \xc2\xb7 hash 21 %";
    zenith.lines       = {{"21/33", "-0.27", -1, "13...Ne8 14.f4 a5 15.f5 a4 16.Nbd4 exd4 17.Nxd4 b3 18.Kb1 bxc2+"}};
    view.SetAnalysis({pawnstar, zenith});
    view.SetAnalysing(true);

    // Each engine's own scores at its own moves, from White's side, after the book's six plies: Pawnstar a little more
    // pleased with White than Zenith is.
    EvalSeries   pawnstar_scores{"Pawnstar 2.1", std::vector<std::optional<EvalPoint>>(static_cast<std::size_t>(ply))};
    EvalSeries   zenith_scores{"Zenith 0.9", std::vector<std::optional<EvalPoint>>(static_cast<std::size_t>(ply))};
    const double white_view[] = {0.28, 0.33, 0.36, 0.40, 0.31, 0.34, 0.38, 0.36, 0.44};
    const double black_view[] = {0.18, 0.12, 0.21, 0.06, -0.08, 0.04, -0.15, -0.21, -0.27};
    for (std::size_t move = 0; move < std::size(white_view); ++move)
    {
        pawnstar_scores.points[6 + 2 * move] = EvalPoint{white_view[move], std::nullopt};
        zenith_scores.points[7 + 2 * move]   = EvalPoint{black_view[move], std::nullopt};
    }
    view.SetEval({pawnstar_scores, zenith_scores}, ply, ply, "12...b4  -0.27");

    EngineLog &log = view.log();
    log.Add("Pawnstar", LogDirection::kToEngine,
            "position startpos moves e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 c1e3 e7e5 d4b3 c8e6 f2f3 "
            "f8e7 d1d2 e8g8 e1c1 b8d7 g2g4 b7b5 g4g5 b5b4");
    log.Add("Pawnstar", LogDirection::kToEngine, "go wtime 251600 btime 238900 winc 3000 binc 3000");
    log.Add("Pawnstar", LogDirection::kFromEngine,
            "info depth 22 seldepth 34 multipv 1 score cp 39 nodes 6911042 nps 2290000 hashfull 301 time 3018 pv c3e2 "
            "f6e8 f3f4 a6a5");
    log.Add("Pawnstar", LogDirection::kFromEngine,
            "info depth 23 seldepth 36 multipv 1 score cp 44 nodes 8840210 nps 2302000 hashfull 322 time 3840 pv c3e2 "
            "f6e8 f3f4 a6a5 f4f5");
    log.Add("Pawnstar", LogDirection::kFromEngine,
            "info depth 24 seldepth 38 multipv 1 score cp 42 nodes 11180522 nps 2311000 hashfull 341 time 4838 pv "
            "c3e2 f6e8 f3f4 a6a5 f4f5 a5a4 b3d4 e5d4 e2d4 b4b3 c1b1");

    view.SetStatus(
        {"White to move", "Pawnstar searching", "Zenith pondering 13.Ne2", "Concurrency 1 \xc2\xb7 game 7 of 20"});
}

} // namespace uci_host::app
