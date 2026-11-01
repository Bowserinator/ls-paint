#include "ui/screen_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

#include "utils/env.hpp"
#include "utils/math.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <wayland-client.h>
#endif

namespace lsp {
namespace {

#if !defined(_WIN32)
struct WaylandOutput {
    wl_output* handle = nullptr;
    int width = 0;
    int height = 0;
    int scale = 1;
    int transform = WL_OUTPUT_TRANSFORM_NORMAL;
    ~WaylandOutput() { if (handle) wl_output_destroy(handle); }
};

std::vector<RectI> wayland_screens() {
    wl_display* display = wl_display_connect(nullptr);
    if (!display) return {};
    wl_registry* registry = wl_display_get_registry(display);
    std::vector<std::unique_ptr<WaylandOutput>> outputs;
    static const wl_output_listener output_listener{
        [](void* data, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t transform) {
            static_cast<WaylandOutput*>(data)->transform = transform;
        },
        [](void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t) {
            if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
            auto& output = *static_cast<WaylandOutput*>(data);
            output.width = width;
            output.height = height;
        },
        [](void*, wl_output*) {},
        [](void* data, wl_output*, int32_t scale) { static_cast<WaylandOutput*>(data)->scale = std::max(1, scale); },
        nullptr, nullptr
    };
    static const wl_registry_listener registry_listener{
        [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
            if (std::string_view(interface) != wl_output_interface.name) return;
            auto output = std::make_unique<WaylandOutput>();
            output->handle = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface,
                                                                      std::min(version, 2u)));
            if (!output->handle) return;
            wl_output_add_listener(output->handle, &output_listener, output.get());
            static_cast<std::vector<std::unique_ptr<WaylandOutput>>*>(data)->push_back(std::move(output));
        },
        [](void*, wl_registry*, uint32_t) {}
    };
    wl_registry_add_listener(registry, &registry_listener, &outputs);
    std::vector<RectI> areas;
    if (wl_display_roundtrip(display) >= 0 && wl_display_roundtrip(display) >= 0) {
        for (const auto& output : outputs) {
            int width = output->width / output->scale;
            int height = output->height / output->scale;
            if (output->transform % 2 != 0) std::swap(width, height);
            if (width > 0 && height > 0) areas.push_back({0, 0, width, height});
        }
    }
    outputs.clear();
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return areas;
}
#endif

}  // namespace

ScreenGeometry screen_geometry(float window_scale) {
    ScreenGeometry geometry;
    const float scale = std::isfinite(window_scale) && window_scale >= 0.25f ? window_scale : 1.0f;
#if defined(_WIN32)
    struct MonitorData {
        ScreenGeometry& geometry;
        float scale;
        static BOOL CALLBACK collect(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
            auto& data = *reinterpret_cast<MonitorData*>(param);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info)) return TRUE;
            const RECT& r = info.rcWork;
            RectI area{ceil_to_int(r.left / data.scale), ceil_to_int(r.top / data.scale),
                       floor_to_int((r.right - r.left) / data.scale), floor_to_int((r.bottom - r.top) / data.scale)};
            if (info.dwFlags & MONITORINFOF_PRIMARY)
                data.geometry.work_areas.insert(data.geometry.work_areas.begin(), area);
            else
                data.geometry.work_areas.push_back(area);
            return TRUE;
        }
    } data{geometry, scale};
    EnumDisplayMonitors(nullptr, nullptr, MonitorData::collect, reinterpret_cast<LPARAM>(&data));
#else
    if (env_value("WAYLAND_DISPLAY")) {
        geometry.position_supported = false;
        geometry.work_areas = wayland_screens();
        return geometry;
    }
    int screen_number = 0;
    xcb_connection_t* connection = xcb_connect(nullptr, &screen_number);
    if (xcb_connection_has_error(connection)) {
        xcb_disconnect(connection);
        return geometry;
    }
    auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection));
    for (int i = 0; i < screen_number && screen.rem; ++i) xcb_screen_next(&screen);
    if (screen.rem) {
        const std::unique_ptr<xcb_randr_get_monitors_reply_t, decltype(&std::free)> reply(
            xcb_randr_get_monitors_reply(connection,
                xcb_randr_get_monitors(connection, screen.data->root, true), nullptr), &std::free);
        if (reply) {
            for (auto monitors = xcb_randr_get_monitors_monitors_iterator(reply.get()); monitors.rem;
                 xcb_randr_monitor_info_next(&monitors)) {
                const auto& m = *monitors.data;
                RectI area{ceil_to_int(m.x / scale), ceil_to_int(m.y / scale),
                           floor_to_int(m.width / scale), floor_to_int(m.height / scale)};
                if (area.empty()) continue;
                if (m.primary) geometry.work_areas.insert(geometry.work_areas.begin(), area);
                else geometry.work_areas.push_back(area);
            }
        }
    }
    xcb_disconnect(connection);
#endif
    return geometry;
}

}  // namespace lsp
