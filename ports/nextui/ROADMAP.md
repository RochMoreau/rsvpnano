# NextUI (TrimUI Brick) port — roadmap

What the Brick port still lacks compared with the ESP32 readers, and what the Brick's own
hardware could add. Roughly in priority order.

## Done

### 1. Wi-Fi book transfer
- The companion API runs on the network NextUI is connected to (Device → Companion sync), served
  by a POSIX implementation of ESP-IDF's `esp_http_server` (`runtime/HttpServer.cpp`).
- The Brick serves its own upload page at `/`, so any phone or computer browser on the same
  network can send books (EPUB/PDF as they are) and delete them.

### 2. Reader controls designed for buttons
- L1 / R1 jump to the previous / next sentence, L2 / R2 to the previous / next paragraph, without
  stopping playback (new `Input` actions, usable by any board with spare buttons).
- Holding Up / Down repeats the speed change.

### Screens for what the Brick can do
- Settings → Network & updates shows the connection NextUI made (Wi-Fi and updates are NextUI's
  job) instead of the firmware's Wi-Fi picker, saved network and update-check options.
- The Device tab drops USB transfer, RSS, OTA and encryption, which have no NextUI equivalent.

## Next

### Wi-Fi follow-ups
- RSS feed sync (needs HTTPS: libcurl/OpenSSL on the device).
- Advertise the Brick over mDNS (`rsvp-nano-XXXXXX.local`) so the address need not be typed.
- Show a QR code of the address on the Sync screen.

### Controls follow-ups
- Direct button handling for sliders and steppers instead of A + D-pad dragging.
- Chapter jumps (for example Select + L1 / R1).

### 3. Sharper text
The UI renders at 640 × 480 and is scaled to the 1024 × 768 panel, so text is slightly soft.
Render the reader (or at least the current word) at the panel's native resolution.

### 4. Rumble and sound instead of the beeper
The Brick has a vibration motor and a speaker. Use them for focus-timer alarms and
end-of-chapter alerts.

### 5. LED strips
Show reading progress or the remaining focus time on the Brick's RGB LEDs.

## To check on hardware

### 6. Sleep and battery
Power currently opens the app's own standby screen, which keeps the system awake. Hook into
NextUI's suspend instead, and verify the battery reading on the device.

### 7. Updates
There is no built-in updater. Publish the pak to NextUI's Pak Store so updates come from there.

## Won't do

- Motion sensor features (flip to start a focus timer): the Brick has no IMU.
- USB mass storage: the Brick's USB port is not exposed as a gadget under NextUI.
