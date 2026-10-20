#pragma once
/// @file Showcase.h The window with sample state, as the approved mockup shows it: twelve moves of a Sicilian between
/// two engines, Pawnstar to move and thinking, Zenith pondering, their lines, the graph, the log and the clocks - for
/// the render tool's review images and the window's suite.

#include "MainView.h"

namespace amphitheater::app
{

/**
 * Fill a main view with the mockup's state.
 * @param view The view.
 */
void ShowSample(MainView &view);

} // namespace amphitheater::app
