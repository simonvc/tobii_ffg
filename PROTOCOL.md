# Tobii Eye Tracker 5 (IS5, USB 2104:0313 "EyeChip") — reverse-engineered protocol

Reversed from usbmon/dumpcap captures of Talon Voice driving the device on Linux,
correlated against Talon's logged gaze frames. The EyeChip computes gaze on-device
and streams it; the host must send an init/config sequence (incl. display setup)
before valid gaze is produced.

## USB topology
- Vendor-specific interface 0: **Bulk IN EP 0x83** (device→host data), **Bulk OUT EP 0x05** (host→device commands), Bulk OUT EP 0x04.
- Also a UVC video function (interfaces 1/2) — unused for gaze.
- Device draws 500 mA, high-speed; **must be on a direct port, not a hub** (hub causes re-enumeration/stream drops).

## Transport framing (each bulk transfer)
`[u32 LE type][u32 LE payload_len][payload ...]`
- type 1 = command (OUT) / response wrapper, type 2 = response (older official stack)
- The streaming pushes use type 1 with an inner opcode.

## TTP payload (big-endian)
`[u32 opcode][u32 seq][u32 status/flags][u32 object_id] ...`
- Command opcode **0x51** (host requests/config), responses **0x52**, async data push **0x53**.
- seq increments per command starting at 0; responses echo seq.
- Init = a series of 0x51 commands configuring/subscribing object IDs
  (0x3e8, 0x4ba+subids 0x500/0x501/0x504/0x508/0x50e/0x1770..0x1774, 0x4ce, 0x4c4,
   and 188-byte 0x5a0 commands carrying float display-geometry = display_setup).

## Gaze stream packet (the one that matters)
- **1724-byte** payload, transport type 1, inner opcode **0x53**, object **0x500**, pushed at ~33 Hz.
- **Validity:  byte @ offset 154** — nonzero ⇒ gaze valid (matched Talon's valid/invalid pattern 100%).
- **Gaze X: int16 BE @ offset 1411 (0x583)** — normalized·~1000 (left→right). norm_x ≈ raw/1044 (+~0.014).
- **Gaze Y: int16 BE @ offset 1424 (0x590)** — normalized·~1000 (top→bottom).  norm_y ≈ raw/1030 (+~0.006).
- Raw range observed: X [-112,1163], Y [-205,1715] (slightly outside screen = looking past edges).
- Many other float/int fields present (per-eye position blocks ~offset 267 & 360, etc.) — not needed for screen gaze.

Exact scale differs slightly X vs Y because Talon letterboxes the tracker's active
area into the display; treat raw/1000 as normalized and apply a per-user affine
(2-point or 4-corner calibration) for accuracy.

## To stream valid gaze from scratch (self-contained client)
1. Open device, detach kernel driver if bound, claim interface 0.
2. Replay the captured init command sequence on EP 0x05 (reading EP 0x83 between),
   including the 0x5a0 display-setup with screen geometry. (Capture: captures/talon_session.pcapng)
3. Read 1724-byte packets from EP 0x83; parse validity@154, X@1411, Y@1424 (BE int16).
