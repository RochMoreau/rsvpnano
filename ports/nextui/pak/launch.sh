#!/bin/sh
# RSVP Nano for NextUI (TrimUI Brick / Smart Pro, tg5040).
PAK_DIR="$(dirname "$0")"
cd "$PAK_DIR" || exit 1

SDCARD="${SDCARD_PATH:-/mnt/SDCARD}"
# The reader's "SD card": put books in "RSVP Nano/library/" at the root of the card.
DATA_DIR="$SDCARD/RSVP Nano"
# Preferences and reading positions live with the other NextUI user data.
STATE_DIR="${USERDATA_PATH:-$SDCARD/.userdata/${PLATFORM:-tg5040}}/RSVP Nano"
LOG_FILE="${LOGS_PATH:-$STATE_DIR}/RSVP Nano.txt"

mkdir -p "$DATA_DIR/library/books" "$STATE_DIR" "$(dirname "$LOG_FILE")"
export LD_LIBRARY_PATH="$PAK_DIR/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"

# --raw-joystick reads the buttons by number, as NextUI does, so A and B match the rest of the
# system. If they are swapped on your device, set RSVP_SWAP_AB=1 (or add --swap-ab).
./rsvpnano --fullscreen --raw-joystick --data "$DATA_DIR" --state "$STATE_DIR" --log "$LOG_FILE" > /dev/null 2>&1
