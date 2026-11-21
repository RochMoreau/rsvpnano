#pragma once
// Hooks between the Arduino runtime shims and the NextUI host loop.
namespace rsvp_runtime {
    void setLogLevel(int level);
    void setLogFile(const char* path);
    // Called when firmware code requests a restart (ESP.restart / esp_restart).
    void restartRequested();
} // namespace rsvp_runtime
