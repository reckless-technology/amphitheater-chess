#pragma once
/// @file AnalysisSession.h Analyse: the selected engines thinking without end ("go infinite") about the position the
/// board shows, the search moved to each new position as the person steps through the game. Each engine is started -
/// its handshake, MultiPV and UCI_AnalyseMode - on a worker thread, then handed to the window's thread, which owns it
/// from then on and polls its events once a frame without ever waiting: a new position stops the search under way and
/// starts the next when its "bestmove" comes. Engines are quit on worker threads too, so the window never waits.

#include "EngineCatalogue.h"
#include "SearchTracker.h"
#include "chess/Game.h"
#include "uci/Engine.h"

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace uci_host::app
{

/** A line an analysing engine exchanged. */
struct AnalysisLogLine
{
    std::string                           engine;    ///< The engine's name.
    uci::LineDirection                    direction; ///< Which way it went.
    std::string                           text;      ///< The line.
    std::chrono::steady_clock::time_point at;        ///< When.
};

/** Engines being quit off the window's thread, waited for only when the window goes. */
class EngineReaper
{
  public:
    /** Waits for every engine still being quit. */
    ~EngineReaper();

    /**
     * Quit an engine on a worker thread.
     * @param engine The engine; null for none.
     */
    void Quit(std::unique_ptr<uci::Engine> engine);

    /**
     * Quit an engine still being started, once its start ends.
     * @param starting The start under way.
     */
    void QuitWhenStarted(std::future<std::unique_ptr<uci::Engine>> starting);

    /** Forget the quits that have finished. */
    void Collect();

    /** @return How many quits are under way. */
    std::size_t Pending() const
    {
        return quitting_.size();
    }

  private:
    std::vector<std::future<void>> quitting_; ///< The quits under way.
};

/** The engines analysing the position shown. */
class AnalysisSession
{
  public:
    using TimePoint = std::chrono::steady_clock::time_point; ///< When something happened.

    /**
     * Start the engines, each on a worker thread.
     * @param engines The engines.
     * @param multipv The lines each is asked for, where it offers MultiPV.
     * @param reaper Where its engines go to be quit; outlives the session.
     */
    AnalysisSession(const std::vector<EngineEntry> &engines, int multipv, EngineReaper &reaper);

    /** Hands every engine to the reaper. */
    ~AnalysisSession();

    AnalysisSession(const AnalysisSession &)            = delete;
    AnalysisSession &operator=(const AnalysisSession &) = delete;

    /**
     * The position to analyse: the game's position at a ply, sent with the moves that led to it so an engine sees
     * its repetitions.
     * @param game The game.
     * @param ply The position, from 0.
     */
    void SetPosition(const chess::Game &game, int ply);

    /**
     * Move every engine on, never waiting: a start that has finished taken in, the engines' events read, a search
     * stopped for a new position, the next started once the last has answered.
     * @param now The time.
     */
    void Poll(TimePoint now);

    /** @return The lines the engines exchanged since the last call, in order. */
    std::vector<AnalysisLogLine> DrainLog();

    /** @return Each engine's search, in order. */
    std::vector<const SearchTracker *> Trackers() const;

    /** @return What went wrong with each engine that failed: its name and why. */
    std::vector<std::string> Failures() const;

    /** @return The engines' names, in order. */
    std::vector<std::string> Names() const;

  private:
    /** One engine and its state. */
    struct Analyser
    {
        EngineEntry                               entry;                ///< The engine.
        std::future<std::unique_ptr<uci::Engine>> starting;             ///< Its start, while under way.
        std::unique_ptr<uci::Engine>              engine;               ///< The engine, once started.
        SearchTracker                             tracker;              ///< Its search.
        bool                                      is_searching = false; ///< A "go" not yet answered.
        bool                                      is_stopping  = false; ///< A "stop" not yet answered.
        int                                       searched     = -1;    ///< The position version under search.
        std::string                               failure;              ///< Why it failed; empty while it is well.
    };

    /**
     * Start an engine: process, handshake, analysis options. Runs on a worker thread.
     * @param entry The engine.
     * @param multipv The lines to ask for.
     * @param lock Guards the log.
     * @param log Where its lines go.
     * @return The engine, ready; or null when it could not start, with the reason logged.
     */
    static std::unique_ptr<uci::Engine> StartEngine(EngineEntry entry, int multipv, std::shared_ptr<std::mutex> lock,
                                                    std::shared_ptr<std::vector<AnalysisLogLine>> log);

    EngineReaper                                 &reaper_;      ///< Where engines go to be quit.
    std::vector<std::unique_ptr<Analyser>>        analysers_;   ///< The engines.
    std::shared_ptr<std::mutex>                   log_lock_;    ///< Guards the log.
    std::shared_ptr<std::vector<AnalysisLogLine>> log_;         ///< Lines not yet drained.
    std::vector<AnalysisLogLine>                  drained_;     ///< Lines fed to the trackers, for DrainLog.
    std::string                                   fen_;         ///< The position's starting FEN; empty: start.
    std::vector<std::string>                      moves_;       ///< The moves to the position.
    int                                           version_ = 0; ///< Counts the positions asked for.
};

} // namespace uci_host::app
