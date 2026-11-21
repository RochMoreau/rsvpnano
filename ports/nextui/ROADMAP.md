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

### Sleep, rumble, sound, LEDs, QR code
- Power sleeps like NextUI: backlight off at once, then NextUI's suspend script after its suspend
  timeout (not while charging). Rumble on sleep/wake follows NextUI's haptics setting.
- Focus timers work without a motion sensor: A on the hourglass starts, pauses and resumes a phase
  (`focus::Session::manualFlip`, usable by any board without an IMU).
- Phase ends chime through SDL audio, rumble and blink the LEDs. The top bar LED shows book
  progress while reading and breathes during focus sessions (`Board::Indicators`); NextUI's own LED
  settings come back otherwise.
- The Sync screen shows a QR code of its address.

## Next

### Wi-Fi follow-ups
- RSS feed sync (needs HTTPS: libcurl/OpenSSL on the device).
- Advertise the Brick over mDNS (`rsvp-nano-XXXXXX.local`).

### Controls follow-ups
- Direct button handling for sliders and steppers instead of A + D-pad dragging.
- Chapter jumps (for example Select + L1 / R1).
- Optional end-of-chapter cue (rumble / chime) while reading.

### Sharper text
The UI renders at 640 × 480 and is scaled to the 1024 × 768 panel, so text is slightly soft.
Render the reader (or at least the current word) at the panel's native resolution.

## To check on hardware

- Sleep and wake through NextUI's suspend script, rumble strength, chime volume and LED colours.
- Battery percentage and charging state (read from `/sys/class/power_supply/axp2202-*`).

### Updates
There is no built-in updater. Publish the pak to NextUI's Pak Store so updates come from there.

## Won't do

- USB mass storage: the Brick's USB port is not exposed as a gadget under NextUI.
