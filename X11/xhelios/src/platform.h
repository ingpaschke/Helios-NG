/* Machine description for the 32-bit little-endian Helios Transputer port. */
#ifndef XHELIOS_PLATFORM_H
#define XHELIOS_PLATFORM_H
#include "trace.h"
#define IMAGE_BYTE_ORDER LSBFirst
#define BITMAP_BIT_ORDER LSBFirst
#define GLYPHPADBYTES 4
#define GETLEFTBITS_ALIGNMENT 1
#define NO_ALLOCA 1
#define NOSTDHDRS 0
#define NOFILE 64
#define _BSD 1
#define FONT_SNF 1
#define COMPILEDDEFAULTFONTPATH "/helios/lib/x11/fonts/misc/"
#define RGB_DB "/helios/lib/x11/rgb"
#define VENDOR_STRING "Low Performance Computing Labs"
#define VENDOR_RELEASE 1
#endif
