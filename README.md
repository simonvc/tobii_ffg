# tobii-gaze — Tobii Eye Tracker 5 on Linux, from scratch

Two small native tools for the **Tobii Eye Tracker 5** (IS5, USB `2104:0313`,
"EyeChip") on Linux — with **no Tobii libraries and no daemon**:

- **`tobii_gaze`** — talks to the EyeChip directly over libusb, replays the
  reverse-engineered init/config sequence, and streams normalized gaze `x y`.
- **`tobii_focus`** — reads gaze on stdin and gives Hyprland **focus-follows-eyes**:
  the window you look at gets keyboard focus (after a short dwell), independent of
  the mouse / focus-follows-mouse.

The whole thing is ~250 lines of C. The wire protocol was reverse-engineered from
USB captures — see [`PROTOCOL.md`](PROTOCOL.md).

## Build

```sh
make                 # builds tobii_gaze (needs libusb-1.0) and tobii_focus
sudo make install-udev   # one-time: lets you read the tracker without root
```

## Use

```sh
# print live normalized gaze (0..1 across the tracker's monitor):
./tobii_gaze            # "0.5231 0.4012" per line; "- -" when gaze is invalid
./tobii_gaze --raw      # "valid rawX rawY" (int16, ~ normalized*1000)

# focus-follows-eyes in Hyprland:
./tobii_gaze | ./tobii_focus
./tobii_gaze | ./tobii_focus --monitor DP-1 --dwell-ms 250 --debug
```

`tobii_focus` options:
- `--monitor NAME`  which monitor the tracker sits under (default: the one at 0,0)
- `--dwell-ms N`    how long gaze must rest on a window before focusing (default 250)
- `--debug`         log each focus change

## Notes & limitations

- **Plug the tracker directly into the computer, not a USB hub.** It's a 500 mA
  high-bandwidth device; hubs cause re-enumeration and dropped streams.
- Gaze accuracy uses the calibration baked into the device (set up once via Tobii
  Experience on Windows) plus an affine fit for this display. For another
  display/user, recalibrate the coefficients in `src/tobii_gaze.c` (or add a
  2-/4-point calibration step).
- The init sequence in `src/init_seq.h` was captured for this unit's display
  geometry; it includes a display-setup blob. Regenerate from a fresh capture if
  you change displays significantly.

## How it was built

The Tobii Eye Tracker 5 has no official Linux support and an undocumented USB
protocol. Approach: drove the device with Talon (which bundles working IS5
support), captured the USB traffic with `dumpcap` on `usbmon`, and correlated the
1724-byte stream packets against Talon's logged gaze to locate the fields:
- transport framing `[u32 LE type][u32 LE len][big-endian TTP payload]`
- gaze validity at byte offset 154, gaze X/Y as big-endian int16 at offsets
  1411 / 1424
- a 26-command init sequence (incl. a vendor "enable" control transfer and a
  display-setup blob) that makes the EyeChip stream valid gaze.

See `PROTOCOL.md` for the full breakdown and `tools/` for the analysis scripts.
