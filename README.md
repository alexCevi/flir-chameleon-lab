# Teledyne FLIR camera lab

A bench for two Teledyne FLIR cameras on an Apple Silicon Mac. They use different buses and different libraries. macOS can list either one. The Chameleon is USB 2.0 IIDC and uses a source build of libdc1394. The Flea3 is USB3 Vision and uses Aravis. libdc1394 does not see the Flea3.

| Camera | Bus | Program | Library |
|---|---|---|---|
| Chameleon `CMLN-13S2M-CS` | USB 2.0 IIDC, grayscale | `chameleon-live` | libdc1394 2.2.7 at `~/.local/libdc1394-usb` |
| Flea3 `FL3-U3-13S2C` | USB3 Vision, color | `flea3-live` | Homebrew Aravis 0.8.36 (`aravis-0.8`) |

`camera-live` chooses which viewer to run. From this repo it asks Aravis for a device list. If that list contains the Flea3, it replaces itself with `./flea3-live`. Otherwise it replaces itself with `./chameleon-live`.

![Teledyne FLIR Chameleon CMLN-13S2M, front and back](chameleon3-front-back.png)

The viewers and these notes are [MIT](LICENSE). libdc1394, Aravis, SDL2, and libusb stay under their own licenses and are not included here.

## Launch

Run this from the repo directory. `camera-live` execs `./flea3-live` or `./chameleon-live` by relative path, so the working directory has to be this one.

```bash
make
./camera-live
```

`make` is `make all`. It builds `flea3-live` and `camera-live`. It does not build `chameleon-live`.

`./camera-live` prints `Opening Flea3 viewer` and execs `./flea3-live` when Aravis enumerates a USB3 device, or a model whose name contains `Flea` or `FL3-`. If none of the enumerated devices match, it prints `Opening Chameleon viewer` and execs `./chameleon-live`.

`make live` builds those same two binaries and then runs `./camera-live`.

Build the Chameleon viewer before relying on that fallback:

```bash
make chameleon
./chameleon-live
```

Open the Flea3 viewer on its own with:

```bash
make flea3
./flea3-live
```

None of the three programs take flags.

With the Flea3 enumerated, `./camera-live` takes the Flea3 path. Use `./chameleon-live` when you want the Chameleon while the Flea3 is still plugged in. `chameleon-live` opens the first IIDC camera and does not check USB IDs, so unplug any other IIDC camera first. The Flea3 is not IIDC.

The Makefile sets no runtime environment for `./camera-live`. It does not export `DYLD_LIBRARY_PATH`. Leave that variable out of your shell rc.

| Target | Result |
|---|---|
| `make`, `make all` | `flea3-live`, `camera-live` |
| `make flea3`, `make flea3-live` | `flea3-live` |
| `make camera-live` | `camera-live` |
| `make chameleon`, `make chameleon-live` | `chameleon-live` |
| `make live` | `flea3-live`, `camera-live`, then `./camera-live` |

`flea3-live` and `camera-live` link Homebrew Aravis. The pkg-config module is `aravis-0.8`. `flea3-live` also links SDL2. `camera-live` does not.

```text
cc -O2 -Wall -Wextra \
  flea3-live.c flea3-view.c flea3-controls.c flea3-save.c flea3-input.c \
  -o flea3-live \
  $(pkg-config --cflags --libs aravis-0.8) \
  $(sdl2-config --cflags --libs)

cc -O2 -Wall -Wextra camera-live.c -o camera-live \
  $(pkg-config --cflags --libs aravis-0.8)
```

`chameleon-live` links SDL2 and libdc1394. The recipe prefixes pkg-config so the link uses `~/.local/libdc1394-usb` and Homebrew libusb:

```text
cc -O2 -Wall -Wextra chameleon-live.c -o chameleon-live \
  $(PKG_CONFIG_PATH="$HOME/.local/libdc1394-usb/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig" \
    pkg-config --cflags --libs libdc1394-2) \
  $(sdl2-config --cflags --libs)
```

That `PKG_CONFIG_PATH` is the build environment for this one link line. `make` does not export it for the other targets, and the viewers do not need it at runtime. After `make chameleon`, `otool -L ./chameleon-live` should list `~/.local/libdc1394-usb`. A line under `/opt/homebrew/opt/libdc1394` means the FireWire-oriented bottle got onto the link line.

Compile `chameleon-live.c` here. The `/tmp` tree is only for libdc1394 and `grab_gray_image`.

## Cameras

### Chameleon CMLN-13S2M-CS

Visible-light grayscale, USB 2.0, IIDC. `chameleon-live` opens `list->ids[0]` and draws with SDL2.

| | |
|---|---|
| Camera | Teledyne FLIR Chameleon `CMLN-13S2M-CS` |
| Link | USB 2.0, vendor `0x1e10`, product `0x2005` |
| Speed and power | 480 Mb/s, 500 mA |
| Image | 1280×960 grayscale, 8-bit or 16-bit |
| Lens on this bench | Computar `H6Z0812`, all mechanical |
| How it was powered | CalDigit dock, the full 500 mA the camera asks for |

The USB descriptor still says Point Grey Research. That is the string `system_profiler` and libdc1394 print. Leave it as printed. The viewer prints a GUID at startup. Leave that line out of git.

### Flea3 FL3-U3-13S2C

Color, USB3 Vision. `flea3-live` opens the first Aravis camera (`arv_camera_new(NULL)`), asks for 1280×960 when the current region is something else, prefers RGB8, and draws with SDL2. On this bench the picture was Point Grey Research Flea3 FL3-U3-13S2C, 1280×960 RGB8, about 29 fps.

| | |
|---|---|
| Camera | Teledyne FLIR Flea3 `FL3-U3-13S2C` |
| Link | USB3 Vision, vendor `0x1e10`, product `0x3300` |
| Speed and power | 5 Gb/s, about 896 mA |
| Image | Color, 1280×960, RGB8 preferred |

This camera does not appear in libdc1394. Spinnaker is not required. The Homebrew package is `aravis`, version 0.8.36, pkg-config module `aravis-0.8`.

## What you can do with the Chameleon

Once the window is up, the camera is a little instrument rather than a black box:

- Watch a live 1280×960 frame, or drop to 640×480 when you want it quicker
- Switch between 8-bit and 16-bit capture. A 16-bit frame is still acquired in 16 bits and drawn as 8
- Crop on the sensor itself (640×480 or 320×240), which is different from zooming in software
- Ride shutter and gain by hand, or hand them back to auto
- Zoom, rotate, mirror, and keep a crosshair pinned to the middle of the picture
- Read a focus score while you turn the lens ring
- Save the frame the camera actually sent

## What you can do with the Flea3

`./flea3-live` prints the vendor and model, the pixel format it selected, the frame size, the control card from `flea3_help_text()`, and a window. Exposure-time auto and gain auto start on continuous (`ARV_AUTO_CONTINUOUS`). `Q` or `Esc` quits. Closing the window quits. Ctrl-C stops the loop.

While `I` is on, which is the start state, the terminal line about once a second is:

```text
frame=120  fps=29.00  size=1280x960  format=RGB8  mean=...  zoom 1.0x | rot 0 | H off | V off | crosshair on  AUTO shutter=...us gain=... fps=... mode=full roi=off size=1280x960
```

A non-zero `fps` means frames are arriving. The format preference order is RGB8, BGR8, YUV422, then Bayer RG8. The window starts as `Point Grey Flea3`. About once a second the title becomes vendor, model, size, fps, the view status, and the controls status. `I` hides the terminal line and leaves the title updates in place.

If frame 15 is still nearly black (`mean` under 2), the program prints that and asks you to check the lens cap and the light. This repo does not name a lens for the Flea3.

`make flea3` compiles `flea3-live.c`, `flea3-view.c`, `flea3-controls.c`, `flea3-save.c`, and `flea3-input.c` into `./flea3-live`. That binary calls `flea3_view_render`, `flea3_save_frame`, and `flea3_controls_handle_key`. The keys are in [Flea3 controls](#flea3-controls).

## First-time setup

You need the Xcode Command Line Tools (`cc` and `make`) and Homebrew on `PATH`.

### Confirm macOS sees a camera

```bash
system_profiler SPUSBDataType | grep -i -A 15 -B 3 "Point Grey\|Chameleon\|Flea"
```

The check passes when either camera is present with the identity below. Both present is also a pass. Leave the raw listing out of git. It contains the unit serial.

Chameleon `CMLN-13S2M-CS`: vendor `0x1e10`, product `0x2005`, 480 Mb/s, current required 500 mA. The descriptor still says Point Grey Research.

Flea3 `FL3-U3-13S2C`: vendor `0x1e10`, product `0x3300`, 5 Gb/s, current required about 896 mA.

If the command prints nothing, stop. Report the cable, hub, and power.

On this bench the Chameleon path was the camera, a USB 2.0 Mini-B cable, a CalDigit dock, and the Mac. The Flea3 needs a USB 3 port that can supply about 896 mA.

### Install the packages the Makefile uses

```bash
brew install libusb pkgconf sdl2 aravis
```

```bash
sdl2-config --version
pkg-config --modversion libusb-1.0
pkg-config --modversion aravis-0.8
```

`aravis-0.8` on this bench is 0.8.36. `pkg-config --cflags --libs aravis-0.8` also pulls in GLib. Spinnaker is not required. The Chameleon viewer links the source-built libdc1394 only. FlyCapture is not part of this path.

`autoconf`, `automake`, and `libtool` are only needed if you regenerate the libdc1394 build system. The 2.2.7 tarball already ships `./configure`.

### Chameleon library

The working Chameleon library is a source build of libdc1394 2.2.7 with IIDC-over-USB, installed at `~/.local/libdc1394-usb`. The Homebrew libdc1394 bottle often has only the macOS FireWire backend, so it reports zero cameras while macOS lists the device:

```text
libdc1394 debug: Initializing platform 0: macosx
libdc1394 debug: Initialized platform 0
libdc1394 debug: Enumerating platform macosx
libdc1394 debug: Platform macosx has 0 device(s)
Found 0 dc1394 camera(s)
```

A working build brings up a second platform:

```text
Initializing platform 0: macosx
Initializing platform 1: usb
```

The Flea3 stays absent from libdc1394 either way. It is USB3 Vision.

The install prefix is `~/.local/libdc1394-usb`, next to Homebrew rather than over it. `/tmp` is wiped on reboot, and a tree left there may be only a stub. Use `/tmp` to compile libdc1394 and to run `examples/grab_gray_image`. Later Chameleon sessions use the copy under `~/.local`. If `examples/grab_gray_image` is missing, re-download.

```bash
cd /tmp
rm -rf libdc1394-2.2.7 libdc1394-2.2.7.tar.gz

curl -L \
  "https://downloads.sourceforge.net/project/libdc1394/libdc1394-2/2.2.7/libdc1394-2.2.7.tar.gz" \
  -o libdc1394-2.2.7.tar.gz

tar xzf libdc1394-2.2.7.tar.gz
cd libdc1394-2.2.7

export LIBUSB_PREFIX="$(brew --prefix libusb)"
export CPPFLAGS="-I${LIBUSB_PREFIX}/include"
export LDFLAGS="-L${LIBUSB_PREFIX}/lib"
export PKG_CONFIG_PATH="${LIBUSB_PREFIX}/lib/pkgconfig"

./configure --prefix="$HOME/.local/libdc1394-usb"
```

Stop if the configure summary says IIDC-over-USB is disabled. Fix the libusb paths before compiling.

```bash
make -j"$(sysctl -n hw.ncpu)"
make install
```

`~/.local/libdc1394-usb/lib` should contain `libdc1394.dylib`, `libdc1394.26.dylib`, `libdc1394.a`, `libdc1394.la`, and `pkgconfig`.

Confirm the prefix the Chameleon recipe will use:

```bash
PKG_CONFIG_PATH="$HOME/.local/libdc1394-usb/lib/pkgconfig:$(brew --prefix libusb)/lib/pkgconfig" \
  pkg-config --variable=prefix libdc1394-2
```

You want `$HOME/.local/libdc1394-usb`. A prefix under `/opt/homebrew` means the next `make chameleon` would still link the bottle.

### Prove the Chameleon is visible

On this bench the grab program is already built at `/tmp/libdc1394-2.2.7/examples/grab_gray_image`. That path is a libtool wrapper script. The Mach-O binary is `/tmp/libdc1394-2.2.7/examples/.libs/grab_gray_image`, linked to `~/.local/libdc1394-usb`, with IIDC-over-USB enabled. Check the Mach-O, then run the wrapper:

```bash
otool -L /tmp/libdc1394-2.2.7/examples/.libs/grab_gray_image
cd /tmp/libdc1394-2.2.7
DC1394_DEBUG=1 ./examples/grab_gray_image
```

`DC1394_DEBUG=1` is only for this grab. Leave it unset for `./camera-live`, `./flea3-live`, and `./chameleon-live`. Debug mode logs every USB bulk transfer. The Makefile does not set `DYLD_LIBRARY_PATH`; the Mach-O install name points at `~/.local/libdc1394-usb`.

A good trace names the USB platform and this device:

```text
libdc1394 debug: Initializing platform 1: usb
libdc1394 debug: usb: Found vendor:prod 1e10:2005
libdc1394 debug: Platform usb has 1 device(s)
libdc1394 debug: Adding camera ... (Point Grey Research:Chameleon CMLN-13S2M)
```

A full 1280×960 8-bit frame is 1,228,800 bytes. When the grab works, the example writes `image.pgm`. Open it with `open image.pgm`, or `sips -s format png image.pgm --out image.png`.

Run this only when the Chameleon is attached. The Flea3 will not show up in the trace. If `examples/grab_gray_image` is missing, the `/tmp` tree is a stub or was wiped. Re-download with the commands above. The install under `~/.local` can still be fine.

Unset `DC1394_DEBUG` before starting a viewer.

### Build the viewers

From this repo, after Aravis is installed and libdc1394 is in `~/.local/libdc1394-usb`:

```bash
make
make chameleon
```

`otool -L ./flea3-live` and `otool -L ./camera-live` should list `libaravis-0.8`. `otool -L ./chameleon-live` should list `~/.local/libdc1394-usb`. `chameleon-live.c` includes `<SDL.h>` because `sdl2-config --cflags` adds the SDL include directory.

Then:

```bash
./camera-live
```

With the Flea3 attached, a pass prints `Opening Flea3 viewer`, then `Camera:` with the vendor and model, opens a window, and emits `frame=` lines with a non-zero `fps`.

With only the Chameleon attached, `./camera-live` prints `Opening Chameleon viewer` and runs `./chameleon-live`. That program prints the camera name and a GUID, opens a window, and emits `frame=` lines with a non-zero `fps`. Leave the GUID out of anything you save.

## Chameleon controls

`B`, `M`, and `X` each reset digital zoom to 1x. `M` also clears a hardware crop before it toggles full and fast. Arrow keys leave auto exposure before they change a value.

### Acquisition

| Key | What it does |
|---|---|
| `B` | Toggle 8-bit and 16-bit capture |
| `M` | Toggle 1280×960 and 640×480, and turn hardware crop off |
| `X` | Cycle hardware crop: off, 640×480, 320×240, off |

Requested rates with the hardware crop off:

| Mode | Size | Rate |
|---|---|---|
| 8-bit, full | 1280×960 | 15 fps |
| 16-bit, full | 1280×960 | 7.5 fps |
| Fast, either depth | 640×480 | 30 fps |

`X` is a Format7 Mode 0 region of interest, centered on the sensor. The camera sends only that rectangle. Digital zoom is the other thing: the camera still sends the whole frame, and the Mac crops it for display.

### View

| Key | What it does |
|---|---|
| `C` | Crosshair |
| `+` / `=` / numpad `+` | Zoom in, up to 8x |
| `-` / numpad `-` | Zoom out, down to 1x |
| `1` | Reset zoom |
| Mouse wheel | Zoom |
| `R` / `L` | Rotate 90° clockwise / counter-clockwise |
| `H` / `V` | Mirror horizontal / vertical |
| `F` | Fullscreen |

The crosshair stays on the center of the window through zoom, rotation, mirroring, and a hardware crop.

### Exposure and files

| Key | What it does |
|---|---|
| `A` | Toggle auto and manual exposure |
| Up / Down | Shutter, and leave auto |
| Right / Left | Gain, and leave auto |
| `0` | Back to auto |
| `S` | Save the acquired frame as `.pgm` |
| `I` | Toggle the terminal stats |
| `Q` / `Esc` | Quit |

`S` writes the 8-bit frame after a 16-bit capture has been reduced for display, and before zoom, rotation, mirror, and the crosshair. Files land in the working directory as `chameleon-0000.pgm`, `chameleon-0001.pgm`, and so on.

The terminal line is the live readout. Shutter and gain are shown as an absolute value and the raw register:

```text
frame=450  fps=15.00  acq=FULL 1280x960  depth=MONO8  size=1280x960  dzoom=1.0x  min=0  max=255  mean=87.52  focus=26.41  shutter=12.016ms(256)  gain=0.00dB(160)  AUTO
```

## Chameleon lens and focus

The Computar `H6Z0812` on the Chameleon has a mechanical iris, zoom, and focus. libdc1394 reporting those features as unavailable is the camera telling the truth: nothing there is electronic.

Open the iris, point at something bright, and turn the focus ring while watching `focus=` in the title bar. On the same scene and exposure, a higher number means more edge detail. Change the shutter or gain a lot and the score moves for a different reason, because noise looks like edges too.

That lens note is for the Chameleon only.

## Flea3 controls

`flea3-live` prints `flea3_help_text()` at startup. That card is not drawn in the window. There is no pause key and no help-overlay key. `flea3_view_status()` is `zoom %.1fx | rot %d | H on/off | V on/off | crosshair on/off`. `flea3_controls_status()` is `AUTO` or `MANUAL`, then `shutter=`, `gain=`, `fps=`, `mode=full` or `mode=fast`, `roi=`, and `size=`. Both strings are appended to the `frame=` line while `I` is on, and both go into the window title.

Key order in the viewer: `Q` / `Esc`, then `F` and `I`, then `X` (which stops the stream, changes the region, and opens it again), then the rest of `flea3_controls_handle_key`, then the view keys, then `S`.

On startup the controls object selects `GainSelector` `All` when that entry exists, sets `ExposureMode` to timed when that node exists, and turns exposure-time auto and gain auto on.

### View

`flea3_view_handle_key` and `flea3_view_handle_wheel` own these keys. A new view starts at zoom 1.0, rotation 0, mirrors off, crosshair on. Zoom moves by 0.5 and stays inside 1.0x–8.0x.

`flea3_view_render` takes the acquired RGB8 frame and returns a separate RGB8 display image. Zoom is a centered crop of that frame, scaled back up. `R` and `L` rotate the display in 90° steps. At 90° and 270° the display width and height swap. `H` and `V` mirror after that rotation. The crosshair is drawn last, on the center of the display image, so it stays in the middle of the window through zoom, rotation, and mirroring.

| Key | What it does |
|---|---|
| `C` | Toggle the crosshair |
| `+` / `=` / numpad `+` | Zoom in |
| `-` / numpad `-` | Zoom out |
| `1` | Reset zoom to 1x |
| Mouse wheel | Zoom in when wheel `y` is positive, zoom out when `y` is negative |
| `R` | Rotate 90° clockwise |
| `L` | Rotate 90° counter-clockwise |
| `H` | Mirror horizontal |
| `V` | Mirror vertical |

### Exposure, gain, rate, and ROI

Arrow keys leave auto before they change a value. Exposure time is in microseconds. Up and Down step by 10% of the current time, and by at least 1 µs. Right and Left step by 10/682 of the gain range, the same ratio as the Chameleon gain step of 10 across a 682-count span. If that gain step is not positive, the step is 1/40 of the range. When the camera increment is larger than the step, the step becomes the increment. The new value is snapped to the increment and clamped to the camera bounds. Up lengthens exposure. Down shortens it. Right raises gain. Left lowers it.

`M` toggles a 15 fps request and a 30 fps request, clamped to the camera's frame-rate bounds. The fast flag starts off. The viewer does not write a frame rate until `M`, or until `X` succeeds. The first `M` asks for 30 fps. The next asks for 15 fps.

`X` cycles the hardware region: off, then 640×480, then 320×240, then off. Off uses the sensor size, or 1280×960 when that read fails. The rectangle is centered and aligned to the camera's increments. `flea3-live` stops acquisition before `X` and reopens the stream afterward, because width and height cannot change while the stream is running. The window is resized to the new frame unless fullscreen is on. If setting the region fails, the control tries binning instead: 2×2 for 640×480, 4×4 for 320×240, and 1×1 for off. If that also fails, the region index stays where it was. A successful `X` writes the current 15 or 30 fps request again.

`B` is consumed and leaves the pixel format on RGB8. It prints `Bit depth unchanged (RGB8; MONO8/MONO16 would replace it).` The help card still says `B` toggles MONO8 / MONO16. The key does not.

After `A`, `0`, the arrows, `M`, and `X`, the viewer prints the controls status line.

| Key | What it does |
|---|---|
| `A` | Toggle exposure-time auto and gain auto together (`ARV_AUTO_CONTINUOUS` / `ARV_AUTO_OFF`) |
| Up / Down | Lengthen / shorten exposure time, and leave auto |
| Right / Left | Raise / lower gain, and leave auto |
| `0` | Restore exposure-time auto and gain auto |
| `M` | Toggle 15 fps and 30 fps, clamped to the camera bounds |
| `X` | Cycle ROI: off → 640×480 → 320×240 → off |
| `B` | Leave the pixel format on RGB8 |

### Save, fullscreen, stats, quit

| Key | What it does |
|---|---|
| `S` | Save the acquired frame before zoom, rotation, mirror, and the crosshair. `flea3-live` passes 3 channels, so the file is `flea3-NNNN.ppm` (P6, max value 255) in the working directory. Packed RGB8 with no row padding is saved from the camera payload. Every other accepted format is saved from the RGB buffer just converted for display. `flea3_save_frame` writes `flea3-NNNN.pgm` when given 1 channel. This viewer always passes 3. `NNNN` is the smallest unused index from `0000` to `9999`, and either extension occupies that index. On success it prints `Saved snapshot:` and the filename. |
| `F` | Toggle `SDL_WINDOW_FULLSCREEN_DESKTOP`. Starts windowed. |
| `I` | Toggle the terminal `frame=` line. Starts on. The window title still updates. |
| `Q` / `Esc` | Quit |

## When it goes wrong

**The USB listing is empty.** Stop. Check the cable, the hub, and power. The Chameleon wants USB 2.0, 480 Mb/s, and 500 mA. The Flea3 wants USB 3 at 5 Gb/s and about 896 mA.

**macOS lists the Chameleon and libdc1394 says zero devices.** The prefixed `pkg-config --variable=prefix libdc1394-2` should print `~/.local/libdc1394-usb`, and a debug grab should show `Initializing platform 1: usb`. A missing USB platform means the process is on a FireWire-only build. `otool -L` the Mach-O at `examples/.libs/grab_gray_image`, not the libtool wrapper.

**`./camera-live` opens the Flea3.** Aravis enumerated it. Run `./chameleon-live` for the Chameleon.

**`./camera-live` says `Opening Chameleon viewer` and then cannot exec.** `make` does not build `chameleon-live`. Run `make chameleon` from this repo.

**The Flea3 is missing from libdc1394.** That camera is USB3 Vision. Use `make` and `./camera-live`.

**The Chameleon picture stays black while shutter and gain sit at the top of the range.** On this camera those ranges were about shutter `0`–`1077` and gain `160`–`842`. Take the lens cap off, open the iris, and aim at a bright scene before chasing feature registers.

**The Flea3 picture stays black.** After 15 frames with `mean` under 2, check the lens cap and the light.

**`SDL.h` is missing.** The `flea3-live` and `chameleon-live` recipes pass `$(sdl2-config --cflags --libs)`. `sdl2-config --cflags` shows the include path.

**The terminal turns into a firehose.** Unset `DC1394_DEBUG` and start the viewer again. That variable is only for `./examples/grab_gray_image`.

## In this repo

```text
.
├── AGENTS.md
├── LICENSE
├── Makefile
├── README.md
├── .gitignore
├── camera-live.c
├── chameleon-live.c
├── flea3-live.c
├── flea3-view.c
├── flea3-view.h
├── flea3-controls.c
├── flea3-controls.h
├── flea3-input.c
├── flea3-input.h
├── flea3-save.c
├── flea3-save.h
└── chameleon3-front-back.png
```

Agents asked to set this up and decide whether a camera runs should follow [AGENTS.md](AGENTS.md).

`.gitignore` ignores `chameleon-live`, `flea3-live`, `camera-live`, `*.o`, `*.dSYM/`, `*.pgm`, `*.ppm`, `*.pdf`, `image.png`, and a local `libdc1394-*` source tree. Leave `system_profiler` output and the Chameleon GUID line out of git as well. `flea3-NNNN.ppm` is a Flea3 RGB snapshot.

Already in the Chameleon viewer: the USB path above, live preview, both bit depths, both sizes, hardware crop, digital zoom, crosshair, rotation, mirroring, fullscreen, auto and manual exposure, FPS, min/max/mean, the focus score, PGM snapshots, and a clean Ctrl-C shutdown.

The Flea3 viewer is `flea3-live.c` plus `flea3-view.c`, `flea3-controls.c`, `flea3-save.c`, and `flea3-input.c`. `make flea3` links those sources. The running window has digital zoom, rotation, mirror, crosshair, fullscreen, the stats toggle, exposure and gain, 15/30 fps, hardware ROI, and `flea3-NNNN.ppm` snapshots. `B` leaves the stream on RGB8.

Still on the bench for that viewer, unimplemented: a histogram, focus peaking, dark-frame and flat-field checks, software or GPIO trigger, strobe, a temperature readout, video recording, and a rangefinder.
