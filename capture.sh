#!/bin/sh
# One-shot clean capture of the official Tobii stack streaming gaze.
# Run AFTER replugging the tracker. Captures full USB payloads to captures/full.pcapng
# while the stream-engine client subscribes to gaze. Look around the screen during the run.
set -u
cd "$(dirname "$0")"
BASE="$PWD"
USBLIB="$BASE/vendor/tobii_installer/extracted/usbservice/usr/local/lib/tobiiusb"
DAEMON="$BASE/vendor/tobii_installer/extracted/usbservice/usr/local/sbin/tobiiusbserviced"
SE_LIB="$BASE/vendor/tobii_installer/extracted/stream_engine/lib/x64"

# locate the tracker on the bus
DEV=$(lsusb | awk '/2104:0313/{gsub(":","",$4); printf "/dev/bus/usb/%s/%s\n",$2,$4}')
echo "[*] tracker at $DEV"

start_daemon() {
  sudo pkill -f '[t]obiiusbserviced' 2>/dev/null
  sleep 1
  sudo rm -f /run/tobii_log /run/tobiiusb/tobiiusbservice.pid
  sudo env LD_LIBRARY_PATH="$USBLIB" "$DAEMON"
  sleep 2
  pgrep -f '[t]obiiusbserviced' >/dev/null && echo "[*] daemon up" || { echo "[!] daemon failed"; exit 1; }
}

# Probe connection; if it fails, reset the device on the bus and retry once.
probe() { RUN_SECS=0 LD_LIBRARY_PATH="$SE_LIB" "$BASE/test/diag" 2>&1 | grep -q "create.*No error"; }

start_daemon
if ! probe; then
  echo "[*] first connect failed — resetting device on the bus and retrying"
  gcc -o /tmp/usbreset "$BASE/tools/usbreset.c" 2>/dev/null
  sudo /tmp/usbreset "$DEV"
  sleep 2
  DEV=$(lsusb | awk '/2104:0313/{gsub(":","",$4); printf "/dev/bus/usb/%s/%s\n",$2,$4}')
  start_daemon
fi

echo "[*] starting full-payload capture on usbmon1"
PCAP=/tmp/tobii_full.pcapng
sudo rm -f "$PCAP"
sudo dumpcap -i usbmon1 -w "$PCAP" >/tmp/dumpcap.log 2>&1 &
sleep 2
echo "[*] streaming for 16s — LOOK SLOWLY AROUND THE SCREEN (corners, then centre) NOW"
RUN_SECS=16 LD_LIBRARY_PATH="$SE_LIB" "$BASE/test/diag" 2>&1
sleep 1
sudo pkill -f '[d]umpcap' 2>/dev/null
sleep 1
sudo cp "$PCAP" "$BASE/captures/full.pcapng" 2>/dev/null
sudo chown "$USER" "$BASE/captures/full.pcapng" 2>/dev/null
echo "[*] done: $(ls -la "$BASE/captures/full.pcapng" 2>&1)"
