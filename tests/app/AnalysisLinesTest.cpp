// An engine's lines in the analysis panel: one giving a line at a time shows its newest and the lines it gave at the
// depths before, dimmed; one giving several (MultiPV) shows each rank's latest; and the panel shares its rows alike
// among the engines, so each shows as many lines.

#include "app/AnalysisPanel.h"
#include "app/SearchTracker.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace amphitheater::app
{
namespace
{

/**
 * A tracker searching the starting position.
 * @param now The time.
 * @return The tracker.
 */
SearchTracker Searching(std::chrono::steady_clock::time_point now)
{
    SearchTracker tracker("Pawnstar");
    tracker.Feed(true, "position startpos", now);
    tracker.Feed(true, "go infinite", now);
    return tracker;
}

} // namespace

TEST(AnalysisLines, AnEngineGivingOneLineShowsItsNewestAndTheDepthsBeforeDimmed)
{
    const auto    now     = std::chrono::steady_clock::now();
    SearchTracker tracker = Searching(now);
    tracker.Feed(false, "info depth 10 score cp 20 pv e2e4", now);
    tracker.Feed(false, "info depth 11 score cp 25 pv d2d4", now);
    tracker.Feed(false, "info depth 11 score cp 28 pv g1f3", now); // a better line at the same depth takes its place
    tracker.Feed(false, "info depth 12 score cp 31 pv c2c4", now);
    tracker.Feed(false, "info depth 12 nodes 5000 nps 1000", now); // figures alone: no line

    const EngineBlock block = tracker.Block(now);
    ASSERT_EQ(block.lines.size(), 3u) << "one line a depth";
    EXPECT_EQ(block.lines[0].depth, "12");
    EXPECT_EQ(block.lines[0].pv, "1.c4");
    EXPECT_FALSE(block.lines[0].is_earlier) << "the newest is lit";
    EXPECT_EQ(block.lines[1].pv, "1.Nf3") << "depth 11's last word";
    EXPECT_TRUE(block.lines[1].is_earlier);
    EXPECT_EQ(block.lines[2].depth, "10");
    EXPECT_TRUE(block.lines[2].is_earlier);
    EXPECT_EQ(tracker.Block(now, 2).lines.size(), 2u) << "held to the lines asked for";
}

TEST(AnalysisLines, OnlyTheLastFewDepthsAreKeptAndANewSearchStartsAfresh)
{
    const auto    now     = std::chrono::steady_clock::now();
    SearchTracker tracker = Searching(now);
    for (int depth = 1; depth <= 20; ++depth)
    {
        tracker.Feed(false, "info depth " + std::to_string(depth) + " score cp 10 pv e2e4", now);
    }
    const EngineBlock block = tracker.Block(now, 100);
    ASSERT_EQ(block.lines.size(), SearchTracker::kDepthsKept);
    EXPECT_EQ(block.lines.front().depth, "20");
    EXPECT_EQ(block.lines.back().depth, std::to_string(20 - SearchTracker::kDepthsKept + 1));

    tracker.Feed(true, "position startpos moves e2e4", now);
    tracker.Feed(true, "go infinite", now);
    EXPECT_TRUE(tracker.Block(now).lines.empty()) << "the last position's lines gone with it";
}

TEST(AnalysisLines, AnEngineGivingSeveralLinesShowsEachRanksLatestBestFirst)
{
    const auto    now     = std::chrono::steady_clock::now();
    SearchTracker tracker = Searching(now);
    tracker.Feed(false, "info depth 9 multipv 1 score cp 30 pv e2e4", now);
    tracker.Feed(false, "info depth 9 multipv 2 score cp 20 pv d2d4", now);
    tracker.Feed(false, "info depth 10 multipv 1 score cp 32 pv e2e4 e7e5", now);
    const EngineBlock block = tracker.Block(now);
    ASSERT_EQ(block.lines.size(), 2u) << "a line a rank, not a depth";
    EXPECT_EQ(block.lines[0].pv, "1.e4 e5");
    EXPECT_EQ(block.lines[1].pv, "1.d4");
    EXPECT_FALSE(block.lines[1].is_earlier) << "the second best is no older line";
}

TEST(AnalysisLines, ThePanelSharesItsRowsAlikeAmongTheEngines)
{
    constexpr int height = 192; // the window's analysis panel
    EXPECT_EQ(AnalysisPanel::LinesPerBlock(height, 1), 5u);
    EXPECT_EQ(AnalysisPanel::LinesPerBlock(height, 2), 2u) << "two engines: two lines each, both blocks whole";
    EXPECT_EQ(AnalysisPanel::LinesPerBlock(height, 3), 1u) << "never less than one";
    EXPECT_EQ(AnalysisPanel::LinesPerBlock(height, 0), 1u);
    const int two_blocks = 2 * AnalysisPanel::kHeaderHeight + AnalysisPanel::kGap +
                           2 * static_cast<int>(AnalysisPanel::LinesPerBlock(height, 2)) * AnalysisPanel::kLineHeight;
    EXPECT_LE(two_blocks, height);
}

} // namespace amphitheater::app
