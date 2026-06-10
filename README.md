# tobii_ffg — Tobii Eye Tracker 5 on Linux, from scratch

Native tools for the **Tobii Eye Tracker 5** (IS5, USB `2104:0313`, "EyeChip") on
Linux — talking to the EyeChip **directly over libusb, with no Tobii libraries and
no daemon**.

The headline tool is **`tobii_ffg`**: Hyprland **focus-follows-gaze** in a single
process. It reads gaze straight from the tracker, maps it to your monitor, and
gives keyboard focus to whatever window you look at (after a short dwell) —
**without moving the mouse**, independent of focus-follows-mouse.

The wire protocol was reverse-engineered from USB captures — see [`PROTOCOL.md`](PROTOCOL.md).

## Build & install

```sh
make                     # builds tobii_ffg (needs libusb-1.0) + the standalone pair
sudo make install-udev   # one-time: lets you read the tracker without root
make install             # installs tobii_ffg to ~/.local/bin
```

> **Plug the tracker directly into the computer, not a USB hub.** It's a 500 mA
> high-bandwidth device; behind a hub it re-enumerates and the stream drops.

## Use — the combined tool

```sh
tobii_ffg                       # focus follows gaze, using your config
tobii_ffg --print               # just print "x y" gaze (don't focus) — for calibration
tobii_ffg --monitor DP-1 --dwell-ms 100 --debug
```

To start it automatically, add to your Hyprland config:

```
exec-once = tobii_ffg
```

### Configuration

On first run `tobii_ffg` writes `~/.config/tobii_ffg/config` with these defaults:

```ini
monitor=DP-1
dwell_ms=100
# gaze affine: normalized = raw*scale + offset (per display/user)
x_scale=0.000957714
x_offset=0.0144512
y_scale=0.000970392
y_offset=0.0058576
```

- **`monitor`** — which monitor the tracker sits under (use the `hyprctl monitors` name).
- **`dwell_ms`** — how long your gaze must rest on a window before it gets focus.
- **`x_*` / `y_*`** — the gaze→screen calibration. Tune these for your display/user
  (run `tobii_ffg --print` and watch the values at known screen points).

Command-line flags override the config for that run:
`--monitor NAME`, `--dwell-ms N`, `--print`, `--debug`.

## Use — the standalone pair (alternative)

The same functionality is also split into two composable tools if you prefer a
Unix pipe (e.g. to feed gaze into something else):

```sh
# print live normalized gaze (0..1 across the tracker's monitor):
./tobii_gaze            # "0.5231 0.4012" per line; "- -" when gaze is invalid
./tobii_gaze --raw      # "valid rawX rawY" (int16, ~ normalized*1000)

# focus-follows-eyes in Hyprland:
./tobii_gaze | ./tobii_focus --monitor DP-1 --dwell-ms 100 --debug
```

`tobii_gaze` is the libusb reader; `tobii_focus` reads gaze on stdin and does the
Hyprland focusing. `tobii_ffg` is simply these two merged into one process (and it
adds the config file).

## Notes & limitations

- Gaze accuracy uses the calibration baked into the device (set up once via Tobii
  Experience on Windows) plus the affine fit above. Recalibrate per display/user,
  or add a 2-/4-point calibration step.
- The init sequence in `src/init_seq.h` was captured for this unit's display
  geometry and includes a display-setup blob. Regenerate from a fresh capture if
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

See [`PROTOCOL.md`](PROTOCOL.md) for the full breakdown and `tools/` for the
analysis scripts.
