# X11

The X Window System server for Helios on transputers.

| Directory | Contents |
| --- | --- |
| `xhelios/` | `xhelios`, an X11R4 server for Helios, and its device drivers (the ATW800/2 display, mouse, keyboard) |
| `X11R4/mit/` | the parts of X11R4 the server is built from, unchanged |

`tools/build-t800/build.sh` builds both with the rest of the tree; see
`xhelios/README.md`.

## X11R4

`X11R4/mit` is a subset of X11R4 patch level 18 (the server, the
protocol headers, the SHAPE, Multi-Buffering, MIT-SUNDRY-NONSTANDARD and
MIT-SHM extensions, `mkfontdir` and `Xau`), as published at
https://github.com/LegalizeAdulthood/X11R4 (commit `793f7f4e`). The files
are unchanged: the server build copies them and applies its edits to the
copy (`xhelios/tools/prepare.py`). Their copyright notices and terms are
in `X11R4/mit/COPYRIGHTS`.
