#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace lsp {

enum class Phase {
    Zoom,
    Flush,
    Upload,
    Invalidate,
    Poll,
    Build,
    Publish,
    Images,
    Rows,
    Cursor,
    Preview,
    Title,
    Ants,
    Count,
};

struct PhaseInfo {
    std::string_view label;
    int depth;
};

inline constexpr PhaseInfo kPhaseInfo[] = {
    {"zoom", 0}, {"flush", 0},   {"upload", 0}, {"invalidate", 1}, {"poll", 1},   {"build", 2}, {"publish", 2},
    {"images", 3}, {"rows", 3}, {"cursor", 0}, {"preview", 0},    {"title", 0}, {"ants", 0},
};
static_assert(sizeof(kPhaseInfo) / sizeof(kPhaseInfo[0]) == static_cast<std::size_t>(Phase::Count));

class DebugStats {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kStallMs = 33.0;

    void stroke_begin(Clock::time_point now, Point2f at) {
        m_current = {};
        m_current.active = true;
        m_current.start = now;
        m_current.last = now;
        m_current.position = at;
    }

    void pointer_event(Clock::time_point began, Clock::time_point done, Point2f at) {
        const double spent = milliseconds(done - began);
        ++m_window.events;
        m_window.handler_sum += spent;
        m_window.handler_max = std::max(m_window.handler_max, spent);
        if (!m_current.active) return;
        const double gap = milliseconds(done - m_current.last);
        add_segment(m_current, at);
        ++m_current.moves;
        m_current.gap_sum += gap;
        m_current.gap_max = std::max(m_current.gap_max, gap);
        if (gap > kStallMs) ++m_current.stalls;
        m_current.last = done;
    }

    void stroke_end(Clock::time_point now, Point2f at) {
        if (!m_current.active) return;
        add_segment(m_current, at);
        m_current.active = false;
        m_current.end = now;
        m_previous = m_current;
        m_have_previous = true;
    }

    void refresh_done(Clock::duration spent) {
        const double ms = milliseconds(spent);
        ++m_window.refreshes;
        m_window.refresh_sum += ms;
        m_window.refresh_max = std::max(m_window.refresh_max, ms);
    }

    void phase_done(Phase phase, Clock::duration spent) {
        const double ms = milliseconds(spent);
        Slot& slot = m_window.phases[static_cast<std::size_t>(phase)];
        ++slot.calls;
        slot.sum += ms;
        slot.max = std::max(slot.max, ms);
    }

    void loop_lag(Clock::duration overshoot) {
        m_window.lag_max = std::max(m_window.lag_max, milliseconds(overshoot));
    }

    [[nodiscard]] std::string report(Clock::time_point now) {
        if (!m_window.started) {
            m_window.start = now;
            m_window.started = true;
        }
        const double seconds = std::max(0.001, std::chrono::duration<double>(now - m_window.start).count());
        std::string text = "LS_DEBUG\n";
        char line[200];

        if (m_current.active) {
            const double elapsed = std::chrono::duration<double>(now - m_current.start).count();
            std::snprintf(line, sizeof line, "drawing  %d moves  %.2f s  %.0f/s\n", m_current.moves, elapsed,
                          elapsed > 0 ? m_current.moves / elapsed : 0.0);
        } else {
            std::snprintf(line, sizeof line, "drawing  idle\n");
        }
        text += line;

        if (m_have_previous) {
            const Stroke& s = m_previous;
            const double duration = std::chrono::duration<double>(s.end - s.start).count();
            std::snprintf(line, sizeof line, "last     %d moves  %.2f s  %.0f/s\n", s.moves, duration,
                          duration > 0 ? s.moves / duration : 0.0);
            text += line;
            std::snprintf(line, sizeof line, "gap      avg %.1f  max %.1f ms  stalls %d\n",
                          s.moves > 0 ? s.gap_sum / s.moves : 0.0, s.gap_max, s.stalls);
            text += line;
            std::snprintf(line, sizeof line, "segment  avg %.1f  max %.1f px  path %.0f px\n",
                          s.segments > 0 ? s.path / s.segments : 0.0, s.max_segment, s.path);
            text += line;
        }

        std::snprintf(line, sizeof line, "input    %.0f ev/s  handler avg %.2f max %.2f ms\n", m_window.events / seconds,
                      m_window.events > 0 ? m_window.handler_sum / m_window.events : 0.0, m_window.handler_max);
        text += line;
        std::snprintf(line, sizeof line, "refresh  %.0f/s  avg %.2f max %.2f ms  ev/refresh %.1f\n",
                      m_window.refreshes / seconds,
                      m_window.refreshes > 0 ? m_window.refresh_sum / m_window.refreshes : 0.0, m_window.refresh_max,
                      m_window.refreshes > 0 ? static_cast<double>(m_window.events) / m_window.refreshes : 0.0);
        text += line;
        std::snprintf(line, sizeof line, "loop     max lag %.1f ms\n", m_window.lag_max);
        text += line;

        bool header = false;
        for (std::size_t i = 0; i < static_cast<std::size_t>(Phase::Count); ++i) {
            const Slot& slot = m_window.phases[i];
            if (slot.calls == 0) continue;
            if (!header) {
                text += "phase       calls    avg    max   ms/s\n";
                header = true;
            }
            const std::string name = std::string(static_cast<std::size_t>(kPhaseInfo[i].depth), ' ') +
                                     std::string(kPhaseInfo[i].label);
            std::snprintf(line, sizeof line, "%-10s %5.1f %6.2f %6.2f %6.1f\n", name.c_str(), slot.calls / seconds,
                          slot.sum / slot.calls, slot.max, slot.sum / seconds);
            text += line;
        }

        m_window = Window{};
        m_window.start = now;
        m_window.started = true;
        return text;
    }

    [[nodiscard]] double window_seconds(Clock::time_point now) const {
        return m_window.started ? std::chrono::duration<double>(now - m_window.start).count() : 0.0;
    }

private:
    struct Stroke {
        bool active = false;
        Clock::time_point start{};
        Clock::time_point last{};
        Clock::time_point end{};
        Point2f position{};
        int moves = 0;
        int segments = 0;
        int stalls = 0;
        double gap_sum = 0;
        double gap_max = 0;
        double path = 0;
        double max_segment = 0;
    };

    struct Slot {
        int calls = 0;
        double sum = 0;
        double max = 0;
    };

    struct Window {
        Slot phases[static_cast<std::size_t>(Phase::Count)] = {};
        bool started = false;
        Clock::time_point start{};
        int events = 0;
        int refreshes = 0;
        double handler_sum = 0;
        double handler_max = 0;
        double refresh_sum = 0;
        double refresh_max = 0;
        double lag_max = 0;
    };

    [[nodiscard]] static double milliseconds(Clock::duration span) {
        return std::chrono::duration<double, std::milli>(span).count();
    }

    static void add_segment(Stroke& stroke, Point2f at) {
        const double length = std::hypot(static_cast<double>(at.x - stroke.position.x),
                                         static_cast<double>(at.y - stroke.position.y));
        ++stroke.segments;
        stroke.path += length;
        stroke.max_segment = std::max(stroke.max_segment, length);
        stroke.position = at;
    }

    Stroke m_current;
    Stroke m_previous;
    bool m_have_previous = false;
    Window m_window;
};

inline DebugStats*& debug_stats_sink() {
    static DebugStats* sink = nullptr;
    return sink;
}

class ScopedPhase {
public:
    explicit ScopedPhase(Phase phase) : m_stats(debug_stats_sink()), m_phase(phase) {
        if (m_stats) m_started = DebugStats::Clock::now();
    }
    ScopedPhase(const ScopedPhase&) = delete;
    ScopedPhase& operator=(const ScopedPhase&) = delete;
    ~ScopedPhase() {
        if (m_stats) m_stats->phase_done(m_phase, DebugStats::Clock::now() - m_started);
    }

private:
    DebugStats* m_stats;
    Phase m_phase;
    DebugStats::Clock::time_point m_started{};
};

class ScopedRefreshTimer {
public:
    explicit ScopedRefreshTimer(DebugStats* stats) : m_stats(stats) {
        if (m_stats) m_started = DebugStats::Clock::now();
    }
    ScopedRefreshTimer(const ScopedRefreshTimer&) = delete;
    ScopedRefreshTimer& operator=(const ScopedRefreshTimer&) = delete;
    ~ScopedRefreshTimer() {
        if (m_stats) m_stats->refresh_done(DebugStats::Clock::now() - m_started);
    }

private:
    DebugStats* m_stats;
    DebugStats::Clock::time_point m_started{};
};

}  // namespace lsp
