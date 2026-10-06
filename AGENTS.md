# Agent notes: bring the camera up

Use this when the task is to set up this Teledyne FLIR Chameleon bench and decide whether it actually runs. Human setup steps live in `README.md`. Follow those commands. This file is the order, the stop conditions, and what counts as done.

The camera is a Teledyne FLIR Chameleon `CMLN-13S2M-CS` (USB 2.0, visible grayscale, IIDC). Its USB descriptor still says `Point Grey Research`. That string is what `system_profiler` and libdc1394 print. Do not rename it, and do not commit a real serial number or GUID.

## Before you compile anything

1. Confirm the hardware path. The camera needs USB 2.0 and the full 500 mA. This bench used a CalDigit dock.

```bash
system_profiler SPUSBDataType | grep -i -A 15 -B 3 "Point Grey\|Chameleon"
```

Pass: vendor `0x1e10`, product `0x2005`, speed 480 Mb/s, current required 500 mA.

If that is empty, stop. Report the cable, hub, and power. Do not install or rebuild libdc1394.

2. Do not `brew install libdc1394` and then link against it. That bottle often has only the macOS FireWire backend, so it reports 0 cameras while macOS lists the device. The working library is a source build of libdc1394 2.2.7 with IIDC-over-USB, installed at `~/.local/libdc1394-usb`.

3. Do not install Spinnaker or FlyCapture. This viewer does not use them.

4. Keep `PKG_CONFIG_PATH` and `DYLD_LIBRARY_PATH` in the shell that builds and runs the viewer. Do not put `DYLD_LIBRARY_PATH` in the user's global shell config.

5. `/tmp/libdc1394-2.2.7` disappears on reboot. If it is gone, the install under `~/.local` can still be fine. Re-download only to rerun `grab_gray_image`.

## Pass checks, in order

Stop at the first failure and say which check failed.

| Check | Pass |
|---|---|
| `pkg-config --variable=prefix libdc1394-2` | `$HOME/.local/libdc1394-usb`, not `/opt/homebrew` |
| Configure summary | IIDC-over-USB enabled |
| `DC1394_DEBUG=1 ./examples/grab_gray_image` from the 2.2.7 tree | Log contains `Initializing platform 1: usb`, `vendor:prod 1e10:2005`, and `Platform usb has 1 device(s)` |
| `image.pgm` from that grab | About 1,228,800 bytes for 1280×960 MONO8 |
| `cc` of `chameleon-live.c` as in the README | Binary exists, and `otool -L ./chameleon-live` shows the `~/.local/libdc1394-usb` library |
| `./chameleon-live` | Prints the camera name and a GUID, opens a window, and emits `frame=` lines with a non-zero fps |

Use `DC1394_DEBUG=1` only for the grab check. Leave it unset for `./chameleon-live`. Debug mode logs every USB bulk transfer.

The viewer opens `list->ids[0]` and does not check the USB IDs. Unplug other IIDC cameras first.

A display is required. If SDL cannot create a window, say so. A compile plus a successful `grab_gray_image` is camera proof. A live window is viewer proof.

## How to read a bad picture

Iris, zoom, and focus on the Computar `H6Z0812` are mechanical. libdc1394 reporting them unavailable is expected.

If the frame is black and shutter and gain are near the top of the range (about shutter `1077`, gain `842`), check the lens cap, iris, and light before changing code or registers.

`S` saves the acquired 8-bit frame, before zoom, rotation, mirror, and the crosshair. Files are `chameleon-NNNN.pgm` in the working directory.

## Do not commit

- `system_profiler` output, terminal logs, or the printed GUID
- `chameleon-live`, `*.o`, `*.pgm`, `image.png`, or a `libdc1394-*` source tree

`.gitignore` already covers those build products.
