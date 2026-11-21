#pragma once

#include <cstdint>

// Controls cheat sheet shown by the host on first launch and whenever Y is pressed.
namespace NextUiHelp {

    // Draws the cheat sheet into an RGB565 buffer of NextUiHost::kPanelWidth x kPanelHeight pixels.
    // Returns nullptr if the buffer could not be allocated. The pixels stay valid for the program's life.
    const uint16_t* render();

} // namespace NextUiHelp
