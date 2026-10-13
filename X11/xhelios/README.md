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

Two scripts do the work; the component makefile runs them:

- `tools/server_build.py` copies `../X11R4/mit`, applies the edits of
  `tools/prepare.py` to the copy (each one checked to apply exactly once),
  compiles the server with `clang --target=t800-helios` and links it with
  the Helios `asm`.
- `build.py` builds the drivers in `vendor/helios/devices`.

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

## Sources

- `src/`: the Helios device layer (screen, colormaps, input, configuration,
  the ATW800/2 blitter, the MIT-SHM region).
- `vendor/helios`: files of the Helios 1.3 X server sources
  (`local/xsrc`): the hardware interface (`hardware.h`), the keyboard
  tables and the drivers. `devices/newatw.c`, the ATW800/2 driver, is by
  Andre Saischowa (2024), based on Perihelion's G300 driver.
  `vendor/manifest.json` lists where each file came from; only line
  endings and shortened file names were changed.
- `vendor/include`: X11 headers as Helios 1.3 installed them, and
  `xioevents.h`.
