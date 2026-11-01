#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "ui/debug_stats.hpp"

using namespace lsp;
using Clock = DebugStats::Clock;
using std::chrono::microseconds;
using std::chrono::milliseconds;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void a_steady_stroke_is_summarised() {
    DebugStats stats;
    const Clock::time_point t0{};
    stats.stroke_begin(t0, {0, 0});
    Clock::time_point now = t0;
    for (int i = 1; i <= 100; ++i) {
        const Clock::time_point began = now + milliseconds(5) - microseconds(200);
        now += milliseconds(5);
        stats.pointer_event(began, now, {10.0f * static_cast<float>(i), 0});
    }
    stats.stroke_end(now, {1000, 0});
    const std::string text = stats.report(now);
    CHECK(has(text, "last     100 moves  0.50 s  200/s"));
    CHECK(has(text, "gap      avg 5.0  max 5.0 ms  stalls 0"));
    CHECK(has(text, "segment  avg 9.9  max 10.0 px  path 1000 px"));
    CHECK(has(text, "drawing  idle"));
}

void a_slow_gap_counts_as_a_stall() {
    DebugStats stats;
    const Clock::time_point t0{};
    stats.stroke_begin(t0, {0, 0});
    stats.pointer_event(t0 + milliseconds(4), t0 + milliseconds(5), {5, 0});
    stats.pointer_event(t0 + milliseconds(59), t0 + milliseconds(60), {80, 0});
    stats.stroke_end(t0 + milliseconds(70), {80, 0});
    const std::string text = stats.report(t0 + milliseconds(70));
    CHECK(has(text, "stalls 1"));
    CHECK(has(text, "max 55.0 ms"));
    CHECK(has(text, "max 75.0 px"));
}

void a_stroke_in_progress_is_shown_live() {
    DebugStats stats;
    const Clock::time_point t0{};
    stats.stroke_begin(t0, {0, 0});
    for (int i = 1; i <= 10; ++i)
        stats.pointer_event(t0 + milliseconds(i * 10 - 1), t0 + milliseconds(i * 10), {static_cast<float>(i), 0});
    const std::string text = stats.report(t0 + milliseconds(100));
    CHECK(has(text, "drawing  10 moves  0.10 s  100/s"));
    CHECK(!has(text, "last "));
}

void windows_reset_after_each_report() {
    DebugStats stats;
    const Clock::time_point t0{};
    (void)stats.report(t0);
    stats.refresh_done(milliseconds(4));
    stats.refresh_done(milliseconds(12));
    stats.loop_lag(milliseconds(30));
    stats.pointer_event(t0, t0 + milliseconds(1), {0, 0});
    const std::string first = stats.report(t0 + milliseconds(1000));
    CHECK(has(first, "refresh  2/s  avg 8.00 max 12.00 ms  ev/refresh 0.5"));
    CHECK(has(first, "input    1 ev/s"));
    CHECK(has(first, "max lag 30.0 ms"));
    const std::string second = stats.report(t0 + milliseconds(2000));
    CHECK(has(second, "refresh  0/s"));
    CHECK(has(second, "max lag 0.0 ms"));
}

void the_scoped_timer_records_a_refresh() {
    DebugStats stats;
    (void)stats.report(Clock::now());
    { const ScopedRefreshTimer timer(&stats); }
    { const ScopedRefreshTimer timer(nullptr); }
    const std::string text = stats.report(Clock::now() + milliseconds(1000));
    CHECK(has(text, "refresh  1/s"));
}

void phases_are_listed_with_rates_and_totals() {
    DebugStats stats;
    const Clock::time_point t0{};
    (void)stats.report(t0);
    for (int i = 0; i < 4; ++i) stats.phase_done(Phase::Upload, milliseconds(2 + i));
    stats.phase_done(Phase::Images, milliseconds(1));
    stats.phase_done(Phase::Ants, milliseconds(1));
    const std::string text = stats.report(t0 + milliseconds(2000));
    CHECK(has(text, "phase       calls    avg    max   ms/s\n"));
    CHECK(has(text, "upload       2.0   3.50   5.00    7.0\n"));
    CHECK(has(text, "   images    0.5   1.00   1.00    0.5\n"));
    CHECK(has(text, "ants         0.5   1.00   1.00    0.5\n"));
    CHECK(!has(text, "flush"));
    const std::string next = stats.report(t0 + milliseconds(3000));
    CHECK(!has(next, "phase "));
}

void the_scoped_phase_reports_through_the_sink() {
    DebugStats stats;
    (void)stats.report(Clock::now());
    debug_stats_sink() = &stats;
    { const ScopedPhase timing(Phase::Flush); }
    debug_stats_sink() = nullptr;
    { const ScopedPhase timing(Phase::Flush); }
    const std::string text = stats.report(Clock::now() + milliseconds(1000));
    CHECK(has(text, "flush"));
    CHECK(has(text, "flush        1.0"));
}

}  // namespace

int main() {
    a_steady_stroke_is_summarised();
    a_slow_gap_counts_as_a_stall();
    a_stroke_in_progress_is_shown_live();
    windows_reset_after_each_report();
    the_scoped_timer_records_a_refresh();
    phases_are_listed_with_rates_and_totals();
    the_scoped_phase_reports_through_the_sink();
    if (failures == 0) std::printf("all debug stats tests passed\n");
    return failures == 0 ? 0 : 1;
}
