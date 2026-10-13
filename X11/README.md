# X11

The X Window System server for Helios on transputers.

| Directory | Contents |
| --- | --- |
| `xhelios/` | X11R4 server for Helios and ATW800/2 display, mouse and keyboard drivers |
| `X11R4/mit/` | X11R4 server sources, protocol headers, extensions and supporting libraries |

Run `tools/build-t800/build.sh` from the repository root to build the
server and drivers. See [xhelios/README.md](xhelios/README.md) for the
installed files, configuration and server options.

The X11R4 sources are patch level 18. Their copyright notices and licence
terms are in `X11R4/mit/COPYRIGHTS`.
