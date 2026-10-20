// Time controls and clocks: the command line's syntax and PGN's, sessions that repeat, increments, a fixed time per
// move, and the flag with its margin.

#include "chess/TimeControl.h"

#include <gtest/gtest.h>

namespace amphitheater::chess
{
namespace
{

using namespace std::chrono_literals;

TEST(TimeControl, ParsesTheCommandLineSyntax)
{
    auto control = TimeControl::Parse("40/60+0.6");
    ASSERT_TRUE(control) << control.ErrorMessage();
    ASSERT_EQ(control->sessions.size(), 1u);
    EXPECT_EQ(control->sessions[0].moves, 40);
    EXPECT_EQ(control->sessions[0].time, 60000ms);
    EXPECT_EQ(control->sessions[0].increment, 600ms);
    EXPECT_EQ(control->ToString(), "40/60+0.6");
    EXPECT_TRUE(control->IsTimed());
    EXPECT_FALSE(control->IsInfinite());

    control = TimeControl::Parse("2:30+1.25");
    ASSERT_TRUE(control);
    EXPECT_EQ(control->sessions[0].moves, 0);
    EXPECT_EQ(control->sessions[0].time, 150000ms);
    EXPECT_EQ(control->sessions[0].increment, 1250ms);
    EXPECT_EQ(control->ToString(), "150+1.25");

    control = TimeControl::Parse("40/120,20/60,15+30");
    ASSERT_TRUE(control);
    ASSERT_EQ(control->sessions.size(), 3u);
    EXPECT_EQ(control->ToString(), "40/120,20/60,15+30");
    EXPECT_EQ(control->ToPgnTag(), "40/120:20/60:15+30");

    control = TimeControl::Parse("inf");
    ASSERT_TRUE(control);
    EXPECT_TRUE(control->IsInfinite());
    EXPECT_EQ(control->ToString(), "inf");
    EXPECT_EQ(control->ToPgnTag(), "-");
    EXPECT_TRUE(TimeControl::Parse("infinite"));
    EXPECT_TRUE(TimeControl::Parse(".5"));
}

TEST(TimeControl, RefusesWhatIsNotATimeControl)
{
    for (const char *text : {"", "abc", "40/", "/60", "0/60", "-5", "60+", "60+x", "1.2345", "60.", "1:2:3", "1.5:00",
                             "60,40/30", "40/60,,10", "99999999999"})
    {
        EXPECT_FALSE(TimeControl::Parse(text)) << text;
    }
}

TEST(TimeControl, ReadsAndWritesPgnTags)
{
    auto control = TimeControl::ParsePgnTag("40/7200:3600");
    ASSERT_TRUE(control) << control.ErrorMessage();
    ASSERT_EQ(control->sessions.size(), 2u);
    EXPECT_EQ(control->sessions[1].time, 3600000ms);
    EXPECT_EQ(control->ToPgnTag(), "40/7200:3600");
    control = TimeControl::ParsePgnTag("300+5");
    ASSERT_TRUE(control);
    EXPECT_EQ(control->ToPgnTag(), "300+5");
    control = TimeControl::ParsePgnTag("0.1/move");
    ASSERT_TRUE(control);
    EXPECT_EQ(control->move_time, 100ms);
    EXPECT_EQ(control->ToPgnTag(), "0.1/move");
    EXPECT_TRUE(TimeControl::ParsePgnTag("-")->IsInfinite());
    EXPECT_TRUE(TimeControl::ParsePgnTag("?")->IsInfinite());
    EXPECT_TRUE(TimeControl::ParsePgnTag("")->IsInfinite());
    EXPECT_FALSE(TimeControl::ParsePgnTag("*180"));
    EXPECT_FALSE(TimeControl::ParsePgnTag("x/move"));
    EXPECT_FALSE(TimeControl::ParsePgnTag("300:40/60"));
}

TEST(TimeControl, PrintsLimits)
{
    TimeControl control;
    control.move_time = 1500ms;
    control.depth     = 12;
    control.nodes     = 100000;
    EXPECT_EQ(control.ToString(), "st=1.5 depth=12 nodes=100000");
    EXPECT_EQ(control.ToPgnTag(), "1.5/move");
    TimeControl depth_only;
    depth_only.depth = 5;
    EXPECT_FALSE(depth_only.IsTimed());
    EXPECT_FALSE(depth_only.IsInfinite());
    EXPECT_EQ(depth_only.ToPgnTag(), "-");
}

TEST(TimeControl, SecondsTextIsShortest)
{
    EXPECT_EQ(SecondsText(60000ms), "60");
    EXPECT_EQ(SecondsText(600ms), "0.6");
    EXPECT_EQ(SecondsText(1250ms), "1.25");
    EXPECT_EQ(SecondsText(1001ms), "1.001");
    EXPECT_EQ(SecondsText(0ms), "0");
    EXPECT_EQ(SecondsText(-1500ms), "-1.5");
    EXPECT_EQ(SecondsText(-500ms), "-0.5");
    EXPECT_EQ(ParseSeconds("0.6"), 600ms);
    EXPECT_FALSE(ParseSeconds("."));
}

TEST(Clock, SpendsIncrementsAndAddsTheNextSession)
{
    Clock clock(*TimeControl::Parse("2/10+1,5"));
    EXPECT_EQ(clock.Remaining(), 10000ms);
    EXPECT_EQ(clock.MovesToGo(), 2);
    EXPECT_EQ(clock.Increment(), 1000ms);
    EXPECT_TRUE(clock.Spend(3000ms));
    EXPECT_EQ(clock.Remaining(), 8000ms);
    EXPECT_EQ(clock.MovesToGo(), 1);
    EXPECT_TRUE(clock.Spend(4000ms));
    // The session's two moves made: its increment, then the next session's time.
    EXPECT_EQ(clock.Remaining(), 10000ms);
    EXPECT_EQ(clock.MovesToGo(), 0);
    EXPECT_EQ(clock.Increment(), 0ms);
    EXPECT_EQ(clock.MovesMade(), 2);
    EXPECT_EQ(clock.TimeAvailable(), 10000ms);
}

TEST(Clock, TheLastSessionWithAMoveCountRepeats)
{
    Clock clock(*TimeControl::Parse("2/10"));
    EXPECT_TRUE(clock.Spend(1000ms));
    EXPECT_TRUE(clock.Spend(1000ms));
    EXPECT_EQ(clock.Remaining(), 18000ms);
    EXPECT_EQ(clock.MovesToGo(), 2);
}

TEST(Clock, TheFlagFallsPastTheMargin)
{
    Clock clock(*TimeControl::Parse("1+0.1"));
    EXPECT_TRUE(clock.Spend(1050ms, 100ms)) << "inside the margin";
    EXPECT_EQ(clock.Remaining(), 50ms) << "-50, then the increment";
    EXPECT_FALSE(clock.Spend(300ms, 100ms));
    EXPECT_TRUE(clock.HasFlagFallen());
    EXPECT_EQ(clock.Remaining(), -250ms);
    EXPECT_FALSE(clock.Spend(0ms)) << "a fallen flag stays down";
}

TEST(Clock, AFixedTimePerMove)
{
    TimeControl control;
    control.move_time = 100ms;
    Clock clock(control);
    EXPECT_EQ(clock.TimeAvailable(), 100ms);
    EXPECT_EQ(clock.Remaining(), 0ms);
    EXPECT_EQ(clock.MovesToGo(), 0);
    EXPECT_TRUE(clock.Spend(100ms));
    EXPECT_TRUE(clock.Spend(120ms, 50ms));
    EXPECT_FALSE(clock.Spend(151ms, 50ms));
    // With sessions too, the smaller of the two.
    control.sessions = TimeControl::Parse("0.05")->sessions;
    EXPECT_EQ(Clock(control).TimeAvailable(), 50ms);
}

TEST(Clock, NoTimeControlNeverRunsOut)
{
    Clock clock;
    EXPECT_FALSE(clock.TimeAvailable());
    EXPECT_TRUE(clock.Spend(1h));
    EXPECT_FALSE(clock.HasFlagFallen());
}

} // namespace
} // namespace amphitheater::chess
