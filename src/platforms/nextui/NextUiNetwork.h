#pragma once

#include <string>

// The network NextUI connected the Brick to (RSVP Nano never manages Wi-Fi itself).
namespace NextUiNetwork {

    // IPv4 address of the first interface that is up, preferring Wi-Fi (wlan*). Empty if offline.
    std::string address();

} // namespace NextUiNetwork
