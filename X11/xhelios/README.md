# xhelios

An X11R4 server for Helios on transputers: the X11R4 device-independent
server (dispatch, windows, fonts, colormaps, the mi, cfb and mfb drawing
code) with a Helios device layer in `src/`, and the device drivers it
uses. It serves native Helios clients (`DISPLAY=helios:N`) and offers the
SHAPE, Multi-Buffering, MIT-SUNDRY-NONSTANDARD and MIT-SHM extensions.

## Building

`tools/build-t800/build.sh` builds it with the rest of the tree (this
component is `X11/xhelios`), with clang from llvm-t800 and the tree's own
headers, libraries and host tools. It installs

| File | What |
| --- | --- |
| `bin/xhelios` | the server, for the T425 (it runs on T800s too) |
| `lib/newATW.d` | display driver of the ATW800/2 card |
| `lib/mouse.d`, `lib/keyboard.d` | mouse and keyboard drivers |
| `etc/newxrc` | the server configuration for the ATW800/2 |

## Running

On the ATW800/2, with the configuration file:

```
run -d xhelios notcp -config /helios/etc/newxrc -atw-copy2d
```

Without `-hardware` or a configuration that names a hardware device, the
server draws into memory (640x480, 8 bits) and reads no input: useful in
an emulator, with X clients and `xwd` to look at the screen.

Server options beyond the standard X11R4 ones:

| Option | Meaning |
| --- | --- |
| `-hardware NAME` | the hardware device (a driver in `/helios/lib`) |
| `-config FILE` | read a newxrc configuration file |
| `-input`, `-noinput` | use or ignore the native keyboard and mouse |
| `-keyboard TYPE` | `PC_UK`, `PC_USA`, `PC_GERMAN`, `ST` or `ST_GERMAN` |
| `-atw-copy2d` | use the ATW800/2's blitter for copies, fills and offscreen pixmaps |
| `-atw-vram-kb N` | end of the VRAM used for pixmaps, in KiB |
| `-shm-kb N` | size of the MIT-SHM region in KiB |
| `-trace FILE` | write diagnostic trace points to a file |

## Source layout

- `src/`: Helios screen, colormap, input and configuration support,
  the ATW800/2 blitter and the MIT-SHM region.
- `vendor/helios`: hardware interfaces, keyboard tables and device drivers.
  The ATW800/2 driver (`devices/newatw.c`) is by Andre Saischowa,
  based on Perihelion's G300 driver. Source attribution is recorded in
  `vendor/manifest.json`.
- `vendor/include`: X11 headers and `xioevents.h`.
- `tools/server_build.py` and `tools/prepare.py`: server build preparation
  and compilation.
- `build.py`: device-driver build.
