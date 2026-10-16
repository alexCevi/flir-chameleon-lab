# Agent notes: bring a camera up

Use this when the task is to set up this Teledyne FLIR bench and decide whether a camera actually runs. The bench has two cameras. Human setup steps live in `README.md`. Follow those commands. This file is the order, the stop conditions, and what counts as done.

Leave serial numbers and GUIDs out of git, notes, and commits. The Chameleon viewer prints a GUID. Do not copy it.

## Cameras

1. Chameleon `CMLN-13S2M-CS`. USB 2.0, visible grayscale, IIDC. Vendor `0x1e10`, product `0x2005`, 480 Mb/s, current required 500 mA. Its USB descriptor still says `Point Grey Research`. That string is what `system_profiler` and libdc1394 print. Do not rename it. Backend is a source build of libdc1394 2.2.7 with IIDC-over-USB, installed at `~/.local/libdc1394-usb`. Viewer source is `chameleon-live.c`. The Makefile target is `make chameleon`.

2. Flea3 `FL3-U3-13S2C`. USB3 Vision, color. Vendor `0x1e10`, product `0x3300`, 5 Gb/s, current required about 896 mA. This camera will not appear in libdc1394. Backend is Homebrew Aravis 0.8.36. The package name is `aravis`. The pkg-config module is `aravis-0.8`. Viewer source is `flea3-live.c` with `flea3-view.c`, `flea3-controls.c`, `flea3-save.c`, and `flea3-input.c`. The Makefile `flea3-live` target compiles those five files. `flea3-live.c` calls `flea3_view_render`, `flea3_save_frame`, and `flea3_controls_handle_key`. It asks for 1280×960 and prefers RGB8. On this bench that was about 29 fps. Do not invent a lens model for it. Spinnaker is not required.

`camera-live.c` links `aravis-0.8` only. It enumerates Aravis devices and execs `./flea3-live` for the first USB3 device, or the first model containing `Flea` or `FL3-`. Otherwise it execs `./chameleon-live`. Both paths are relative. Run it from this repo.

## Before you compile anything

1. Confirm the hardware path. The check passes if either camera is present with the identity below. Both present is also a pass.

```bash
system_profiler SPUSBDataType | grep -i -A 15 -B 3 "Point Grey\|Chameleon\|Flea"
```

Chameleon pass: vendor `0x1e10`, product `0x2005`, speed 480 Mb/s, current required 500 mA.

Flea3 pass: vendor `0x1e10`, product `0x3300`, speed 5 Gb/s. Current required is about 896 mA.

If that is empty, stop. Report the cable, hub, and power. Do not install or rebuild a camera library.

2. The Chameleon library is the source build at `~/.local/libdc1394-usb`. The Homebrew libdc1394 bottle often has only the macOS FireWire backend, so it reports 0 cameras while macOS lists the Chameleon. Link `chameleon-live` with the `PKG_CONFIG_PATH` in the Makefile recipe.

3. Split the stacks. The Chameleon viewer uses libdc1394 and SDL2. The Flea3 viewer uses Aravis (`aravis-0.8`) and SDL2. `camera-live` uses Aravis and does not link SDL. Spinnaker is not required. FlyCapture is not part of this path.

4. The Makefile does not export `DYLD_LIBRARY_PATH`. Do not put `DYLD_LIBRARY_PATH` in the user's global shell config. The only special build environment is the inline `PKG_CONFIG_PATH` on the `chameleon-live` link:

```text
PKG_CONFIG_PATH="$(HOME)/.local/libdc1394-usb/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig"
```

5. `/tmp/libdc1394-2.2.7` disappears on reboot, and a tree left there may be only a stub. If it is gone, or if `examples/grab_gray_image` is missing, the install under `~/.local` can still be fine. Re-download the 2.2.7 tarball, as in the README, only to rebuild and rerun `grab_gray_image`. Build the viewers from this repo, not from `/tmp`.

On this bench the grab program is present. `examples/grab_gray_image` is a libtool wrapper. The Mach-O is `examples/.libs/grab_gray_image`, linked to `~/.local/libdc1394-usb`, with IIDC-over-USB enabled. It was not run while only the Flea3 was attached.

## Pass checks, in order

Stop at the first failure for the camera you were asked to bring up, and say which check failed. A missing Flea3 does not fail the Chameleon list. A missing Chameleon does not fail the Flea3 check. The hardware check above already passed when either camera was enumerated correctly.

### Chameleon

| Check | Pass |
|---|---|
| `PKG_CONFIG_PATH="$HOME/.local/libdc1394-usb/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig" pkg-config --variable=prefix libdc1394-2` | `$HOME/.local/libdc1394-usb`, not `/opt/homebrew` |
| Configure summary | IIDC-over-USB enabled |
| `otool -L /tmp/libdc1394-2.2.7/examples/.libs/grab_gray_image` | Links `~/.local/libdc1394-usb`. The sibling `examples/grab_gray_image` is a libtool wrapper, not the Mach-O |
| `DC1394_DEBUG=1 ./examples/grab_gray_image` from `/tmp/libdc1394-2.2.7`, Chameleon attached | Log contains `Initializing platform 1: usb`, `vendor:prod 1e10:2005`, and `Platform usb has 1 device(s)` |
| `image.pgm` from that grab | About 1,228,800 bytes for 1280×960 MONO8 |
| `make chameleon` in this repo | Binary exists, and `otool -L ./chameleon-live` shows `~/.local/libdc1394-usb` |
| `./chameleon-live` | Prints the camera name and a GUID, opens a window, and emits `frame=` lines with a non-zero fps |

Use `DC1394_DEBUG=1` only for the grab check. Leave it unset for `./camera-live`, `./flea3-live`, and `./chameleon-live`. Debug mode logs every USB bulk transfer.

Skip the grab when the Chameleon is not attached. The Flea3 will not appear in it.

`chameleon-live` opens `list->ids[0]` and does not check the USB IDs. Unplug other IIDC cameras first. The Flea3 is not IIDC. If the Flea3 is enumerated, `./camera-live` execs `./flea3-live` instead. Launch `./chameleon-live` directly for the Chameleon.

`make` does not build `chameleon-live`. The fallback inside `camera-live` needs `make chameleon` first.

A display is required. If SDL cannot create a window, say so. A compile plus a successful `grab_gray_image` is camera proof. A live window is viewer proof.

If `examples/grab_gray_image` is missing, the `/tmp` tree is a stub or was wiped. Re-download per the README before this check. Do not compile a viewer there.

### Flea3

| Check | Pass |
|---|---|
| `pkg-config --modversion aravis-0.8` | `0.8.36` on this bench. The Homebrew package is `aravis` |
| `make` in this repo | Produces `./flea3-live` and `./camera-live`. `otool -L` on both shows `libaravis-0.8`. `flea3-live` also shows SDL2. `camera-live` does not |
| `./camera-live` with the Flea3 enumerated | Prints `Opening Flea3 viewer`, then `Camera:` with the vendor and model, opens a window, and emits `frame=` lines with a non-zero fps |

`flea3-live` has no flags. `Q` or `Esc` quits. Closing the window quits. Ctrl-C stops the loop. Startup prints the help card, then opens the window. Exposure-time auto and gain auto start on continuous. With `I` on, the frame line about once a second is `frame=` `fps=` `size=` `format=` `mean=` plus the view status and the controls status. A live run here was 1280×960 RGB8 at about 29 fps.

This camera will not appear in `grab_gray_image`. Do not set `DC1394_DEBUG` for it.

### Flea3 keys

`make flea3` links `flea3-controls.c`. `./flea3-live` handles these keys. Zoom starts at 1.0 and steps by 0.5 inside 1.0x–8.0x. Rotation starts at 0. Mirrors start off. The crosshair starts on. `flea3_view_render` builds a new RGB8 image from the acquired RGB8 frame: centered zoom crop, then rotation (`R` / `L`, 90° steps; 90° and 270° swap width and height), then mirror, then the crosshair on the center of that display image. `S` saves the acquired frame before those steps.

There is no pause key and no help-overlay key. The help card's `B` line says MONO8 / MONO16. The key leaves the pixel format on RGB8 and prints `Bit depth unchanged (RGB8; MONO8/MONO16 would replace it).`

Up and Down change exposure time by 10% of the current value, at least 1 µs, and leave auto. Right and Left change gain by 10/682 of its range and leave auto. A larger camera increment replaces that step. `M` toggles a 15 fps request and a 30 fps request, clamped to the camera bounds. The first `M` asks for 30 fps. `X` cycles ROI off → 640×480 → 320×240 → off, stops the stream, and reopens it. A failed region set tries binning (2×2, then 4×4).

| Key | What it does |
|---|---|
| `C` | Toggle the crosshair. Starts on. |
| `+` / `=` / numpad `+` | Zoom in by 0.5, clamp 1x–8x |
| `-` / numpad `-` | Zoom out by 0.5, clamp 1x–8x |
| `1` | Reset zoom to 1x |
| Mouse wheel | Positive `y` zooms in, negative `y` zooms out, same step and clamp |
| `R` | Rotate 90° clockwise |
| `L` | Rotate 90° counter-clockwise |
| `H` | Mirror horizontal |
| `V` | Mirror vertical |
| `F` | Toggle `SDL_WINDOW_FULLSCREEN_DESKTOP`. Starts windowed. |
| `I` | Toggle the terminal `frame=` line. Starts on. The window title still updates. |
| `S` | Save the acquired frame before zoom, rotation, mirror, and the crosshair. The viewer passes 3 channels, so the file is `flea3-NNNN.ppm`. |
| `Q` / `Esc` | Quit |
| `A` | Toggle exposure-time auto and gain auto together |
| Up / Down | Lengthen / shorten exposure time, and leave auto |
| Right / Left | Raise / lower gain, and leave auto |
| `0` | Restore exposure-time auto and gain auto |
| `M` | Toggle 15 fps and 30 fps, clamped to the camera bounds |
| `X` | Cycle ROI: off → 640×480 → 320×240 → off, reopening the stream |
| `B` | Leave the pixel format on RGB8 |

A display is required. If SDL cannot create a window, say so.

Direct launch, same pass on the `Camera:` and `frame=` lines:

```bash
make flea3
./flea3-live
```

## How to read a bad picture

Iris, zoom, and focus on the Chameleon's Computar `H6Z0812` are mechanical. libdc1394 reporting them unavailable is expected.

If the Chameleon frame is black and shutter and gain are near the top of the range (about shutter `1077`, gain `842`), check the lens cap, iris, and light before changing code or registers.

`S` in `chameleon-live` saves the acquired 8-bit frame, before zoom, rotation, mirror, and the crosshair. Files are `chameleon-NNNN.pgm` in the working directory.

`S` in `flea3-live` saves the acquired frame before zoom, rotation, mirror, and the crosshair. The call passes 3 channels, so the file is `flea3-NNNN.ppm` (P6, max value 255). Packed RGB8 with no row padding is the camera payload. Every other accepted format is the RGB buffer just converted for display. `flea3_save_frame` writes `flea3-NNNN.pgm` when given 1 channel. This viewer always passes 3. `NNNN` is the smallest unused index from `0000` to `9999`, and either extension occupies that index. The function prints `Saved snapshot:` and the filename.

If `flea3-live` reports a black picture (`mean` under 2 at frame 15), check the lens cap and the light. Do not invent a lens model for the Flea3.

## Do not commit

`.gitignore` already ignores these build products:

- `chameleon-live`, `flea3-live`, `camera-live`
- `*.o`, `*.dSYM/`
- `*.pgm`, `*.ppm`, `*.pdf`, `image.png`
- a `libdc1394-*` source tree

Also leave `system_profiler` output, terminal logs, and a printed GUID untracked. Do not add them to a commit. `*.ppm` covers `flea3-NNNN.ppm`. `*.pdf` covers datasheets that do not belong in this tree.
