#pragma once

#include <Arduino_GFX_Library.h>

#include <atomic>
#include <cstdint>

#include "platforms/nextui/NextUiHost.h"

namespace NextUiDisplay {

    // The firmware's panel: an RGB565 framebuffer. flush() marks a frame ready for the host.
    class HostCanvas : public Arduino_Canvas {
    public:
        HostCanvas() : Arduino_Canvas(NextUiHost::kPanelWidth, NextUiHost::kPanelHeight, nullptr) {}

        void flush(bool = false) override {
            frame_.fetch_add(1, std::memory_order_release);
        }

        uint32_t frame() const {
            return frame_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<uint32_t> frame_{0};
    };

    HostCanvas& canvas();

} // namespace NextUiDisplay
