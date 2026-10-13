/* ATW800/2 transputer-side COPY2D, enabled explicitly by -atw-copy2d. */
#ifndef HELIOS_ATW_COPY2D_H
#define HELIOS_ATW_COPY2D_H
#define HELIOS_ATW_VRAM_END 0x1f3c00UL /* SharedMem minus driver's 1 KiB margin */
extern int heliosAtwEnabled;
extern unsigned long heliosAtwCopies;
extern unsigned long heliosAtwFills;
extern unsigned long heliosAtwTiles;
extern unsigned long heliosAtwStages, heliosAtwCacheHits;
int heliosAtwInit(void *pixels, int width, int height, int stride);
/* CPU view of the configured framebuffer, coherent with FPGA writes. */
void *heliosAtwFramebuffer(void);
int heliosAtwFillAvailable(void);
/* Init automatically reserves scratch then pixmaps below HELIOS_ATW_VRAM_END.
 * Override pixmap end (0 disables pixmaps), without moving scratch. */
unsigned long heliosAtwVramSetup(unsigned long endOffset);
void *heliosAtwPixmapAlloc(int width, int height, int stride, int depth);
void heliosAtwPixmapFree(void *base);
/* Take a client region (MIT-SHM) off the top of the pool; call after
 * VramSetup, before any pixmap. Returns its CPU (uncached) address. */
void *heliosAtwReserve(unsigned long bytes);
/* Make CPU-addressed VRAM, e.g. a shared image, a COPY2D source or
 * destination until heliosAtwPixmapFree(base). 0 when not VRAM or when
 * base is already a surface (which then stays as it is). */
int heliosAtwSurfaceAdd(void *base, int width, int height, int stride);
/* 1: completed/no-op; 0: use CFB; -1: engine timeout, do NOT replay. */
int heliosAtwCopy(void *srcBase, void *dstBase, int sx, int sy,
                  int dx, int dy, int width, int height);
/* Full-plane solid replacement only; same return convention as Copy.
 * Small rectangles and configurations without scratch VRAM use CFB. */
int heliosAtwFill(void *dstBase, int x, int y, int width, int height,
                  unsigned pixel);
/* Full-plane replacement tiles: widths dividing 32, heights 1..64.
 * Origin is in screen coordinates; unsupported periods fall back to CFB. */
int heliosAtwTile(void *dstBase, int x, int y, int width, int height,
                  const unsigned char *tile, int tileWidth, int tileHeight,
                  int tileStride, int xorigin, int yorigin);
#endif
