/* Register layout: examples/dev-deps/atw_gfx.h, transputer interface >=1.07.
 * Addresses are offsets in the 2 MiB aperture, not CPU pointers. The current
 * newATW driver maps that aperture at 0x80600000. Preserve its memory page.
 * CPU access uses the cache-off alias documented in ATW's dlgpu.c:
 * address | 0x01000000. COPY2D offsets still refer to the physical aperture.
 * The opt-in backend assumes exclusive engine ownership. */
#include "atw_copy2d.h"

enum { SRC_H, SRC_L, DST_H, DST_L, SRC_STRIDE, DST_STRIDE,
       WIDTH, HEIGHT, CONTROL };
#define APERTURE_SIZE 0x200000UL
/* Conservative documented signed 12-bit stride range. */
#define MAX_STRIDE 2047
#define POLL_LIMIT 1000000UL
/* Scratch belongs to the space after the Helios screen, never VDI caches. */
#define FILL_BYTES 64
#define PATTERN_BYTES 4096
#define MIN_FILL_PIXELS 256
#define VRAM_CEILING HELIOS_ATW_VRAM_END
#define MAX_PIXMAPS 64
#define MIN_PIXMAP_BYTES 16384UL

#ifdef HELIOS_ATW_TEST
extern unsigned long heliosAtwTestBase(void);
extern unsigned heliosAtwTestRead(unsigned);
extern void heliosAtwTestWrite(unsigned, unsigned);
extern void *heliosAtwTestUncached(void *);
#define VRAM_BASE (heliosAtwTestBase())
#define readReg heliosAtwTestRead
#define writeReg heliosAtwTestWrite
#define uncachedAddress heliosAtwTestUncached
#else
#define VRAM_BASE 0x80600000UL
static void *uncachedAddress(void *address)
{
    return (void *)((unsigned long)address | 0x01000000UL);
}
static unsigned readReg(unsigned index)
{
    return ((volatile unsigned *)0x40001040UL)[index];
}
static void writeReg(unsigned index, unsigned value)
{
    ((volatile unsigned *)0x40001040UL)[index] = value;
}
#endif

int heliosAtwEnabled;
unsigned long heliosAtwCopies;
unsigned long heliosAtwFills;
unsigned long heliosAtwTiles;
unsigned long heliosAtwStages, heliosAtwCacheHits;
typedef struct {
    void *base;
    unsigned long offset, size;
    int width, height, stride;
} Surface;
static Surface screenSurface, pixmaps[MAX_PIXMAPS];
static unsigned long poolStart, poolEnd, scratchOffset, scratchEnd;
static int cacheKind, cacheWidth, cacheHeight, cacheX, cacheY, cachePhased;
static unsigned cacheColour;
static unsigned char cachePixels[32 * 64];
static void *framebuffer;
static volatile unsigned *fillTile;
static int patternAvailable;
static unsigned long screenOffset;
static int screenWidth, screenHeight, screenStride;

int heliosAtwInit(void *pixels, int width, int height, int stride)
{
    unsigned long address = (unsigned long)pixels, offset, span;
    int i;
    heliosAtwEnabled = 0;
    heliosAtwCopies = 0;
    heliosAtwFills = 0;
    heliosAtwTiles = 0;
    heliosAtwStages = heliosAtwCacheHits = 0;
    cacheKind = 0;
    poolStart = poolEnd = 0;
    screenSurface.base = 0;
    for (i = 0; i < MAX_PIXMAPS; ++i) pixmaps[i].base = 0;
    framebuffer = 0;
    fillTile = 0;
    patternAvailable = 0;
    if (address < VRAM_BASE || address - VRAM_BASE >= APERTURE_SIZE ||
        width <= 0 || height <= 0 || height > 32767 ||
        stride < width || stride > MAX_STRIDE || (stride & 3)) return 0;
    offset = address - VRAM_BASE;
    span = (unsigned long)(height - 1) * stride + width;
    if (span > APERTURE_SIZE - offset) return 0;
    framebuffer = uncachedAddress(pixels);
    screenOffset = offset;
    screenWidth = width; screenHeight = height; screenStride = stride;
    screenSurface.base = framebuffer; screenSurface.offset = offset;
    screenSurface.size = (unsigned long)height * stride;
    screenSurface.width = width; screenSurface.height = height; screenSurface.stride = stride;
    /* Include row padding and align staging/pixmaps to 16 bytes. Keep the
     * driver's 1 KiB margin below SharedMem at 0x1f4000. */
    scratchOffset = (offset + screenSurface.size + 15) & ~15UL;
    scratchEnd = scratchOffset;
    if (scratchOffset < VRAM_CEILING && VRAM_CEILING - scratchOffset >= FILL_BYTES) {
        fillTile = (volatile unsigned *)uncachedAddress((void *)(VRAM_BASE + scratchOffset));
        patternAvailable = VRAM_CEILING - scratchOffset >= PATTERN_BYTES;
        scratchEnd += patternAvailable ? PATTERN_BYTES : FILL_BYTES;
    }
    heliosAtwEnabled = 1;
    heliosAtwVramSetup(VRAM_CEILING);
    return 1;
}

static Surface *surface(void *base)
{
    int i;
    if (!base) return 0;
    if (base == screenSurface.base) return &screenSurface;
    for (i = 0; i < MAX_PIXMAPS; ++i)
        if (base == pixmaps[i].base) return &pixmaps[i];
    return 0;
}

unsigned long heliosAtwVramSetup(unsigned long end)
{
    int i;
    if (!heliosAtwEnabled || end > VRAM_CEILING) return 0;
    for (i = 0; i < MAX_PIXMAPS; ++i) if (pixmaps[i].base) return 0;
    poolStart = scratchEnd;
    poolEnd = end > poolStart ? end : poolStart;
    return poolEnd - poolStart;
}

void *heliosAtwReserve(unsigned long bytes)
{
    int i;
    bytes = (bytes + 15) & ~15UL;
    if (!heliosAtwEnabled || !bytes || bytes > poolEnd - poolStart) return 0;
    for (i = 0; i < MAX_PIXMAPS; ++i) if (pixmaps[i].base) return 0;
    poolEnd -= bytes;
    return uncachedAddress((void *)(VRAM_BASE + poolEnd));
}

int heliosAtwSurfaceAdd(void *base, int width, int height, int stride)
{
    unsigned long start = (unsigned long)uncachedAddress((void *)VRAM_BASE), offset;
    int slot;
    if (!heliosAtwEnabled || (unsigned long)base < start ||
        width <= 0 || height <= 0 || height > 32767 ||
        stride < width || stride > MAX_STRIDE || (stride & 3)) return 0;
    offset = (unsigned long)base - start;
    if (offset >= APERTURE_SIZE ||
        (unsigned long)height * stride > APERTURE_SIZE - offset) return 0;
    /* Already registered (a shared pixmap at this address): keep that. */
    if (surface(base)) return 0;
    for (slot = 0; slot < MAX_PIXMAPS && pixmaps[slot].base; ++slot) ;
    if (slot == MAX_PIXMAPS) return 0;
    /* Outside [poolStart, poolEnd): the pool allocator never sees it. */
    pixmaps[slot].base = base;
    pixmaps[slot].offset = offset; pixmaps[slot].size = 0;
    pixmaps[slot].width = width; pixmaps[slot].height = height; pixmaps[slot].stride = stride;
    return 1;
}

void *heliosAtwPixmapAlloc(int width, int height, int stride, int depth)
{
    unsigned long bytes, start, next;
    int slot, i, nearest;
    if (!heliosAtwEnabled || depth != 8 || width < 64 || height <= 0 || height > 32767 ||
        stride < width || stride > MAX_STRIDE || (stride & 3)) return 0;
    bytes = ((unsigned long)height * stride + 15) & ~15UL;
    if (bytes < MIN_PIXMAP_BYTES || bytes > poolEnd - poolStart) return 0;
    for (slot = 0; slot < MAX_PIXMAPS && pixmaps[slot].base; ++slot) ;
    if (slot == MAX_PIXMAPS) return 0;
    /* Sorted-gap search; metadata is in ordinary RAM. Free holes coalesce
     * implicitly, with no headers stored in the aperture. */
    start = poolStart;
    for (;;) {
        next = poolEnd; nearest = -1;
        for (i = 0; i < MAX_PIXMAPS; ++i)
            if (pixmaps[i].base && pixmaps[i].offset >= start && pixmaps[i].offset < next) {
                next = pixmaps[i].offset; nearest = i;
            }
        if (bytes <= next - start) break;
        if (nearest < 0) return 0;
        start = pixmaps[nearest].offset + pixmaps[nearest].size;
    }
    pixmaps[slot].base = uncachedAddress((void *)(VRAM_BASE + start));
    pixmaps[slot].offset = start; pixmaps[slot].size = bytes;
    pixmaps[slot].width = width; pixmaps[slot].height = height; pixmaps[slot].stride = stride;
    return pixmaps[slot].base;
}

void heliosAtwPixmapFree(void *base)
{
    Surface *p = surface(base);
    if (p && p != &screenSurface) p->base = 0;
}

void *heliosAtwFramebuffer(void)
{
    return framebuffer;
}

int heliosAtwFillAvailable(void)
{
    return heliosAtwEnabled && fillTile != 0;
}

static int waitIdle(void)
{
    unsigned long remaining = POLL_LIMIT;
    /* Bounded if MMIO reads return; software cannot time out a bus stall. */
    while (remaining--) if (!(readReg(CONTROL) & 1)) return 1;
    heliosAtwEnabled = 0;
    return 0;
}

int heliosAtwCopy(void *srcBase, void *dstBase, int sx, int sy,
                  int dx, int dy, int width, int height)
{
    unsigned long src, dst;
    int srcStride, dstStride, reverse;
    Surface *s = surface(srcBase), *d = surface(dstBase);
    if (!heliosAtwEnabled || !s || !d)
        return 0;
    if (sx < 0 || sy < 0 || dx < 0 || dy < 0 || width < 0 || height < 0 ||
        sx > s->width || dx > d->width || sy > s->height || dy > d->height ||
        width > s->width - sx || width > d->width - dx ||
        height > s->height - sy || height > d->height - dy)
        return 0;
    if (!width || !height || (s == d && sx == dx && sy == dy)) return 1;
    src = s->offset + (unsigned long)sy * s->stride + sx;
    dst = d->offset + (unsigned long)dy * d->stride + dx;
    reverse = s == d && dst > src && dx < sx + width && sx < dx + width &&
              dy < sy + height && sy < dy + height;
    srcStride = s->stride; dstStride = d->stride;
    if (reverse) {
        src += (unsigned long)(height - 1) * srcStride + width - 1;
        dst += (unsigned long)(height - 1) * dstStride + width - 1;
        srcStride = -srcStride; dstStride = -dstStride;
    }
    if (!waitIdle()) return -1;
    writeReg(SRC_H, (src >> 16) & 0xffff);
    writeReg(SRC_L, src & 0xffff);
    writeReg(DST_H, (dst >> 16) & 0xffff);
    writeReg(DST_L, dst & 0xffff);
    writeReg(SRC_STRIDE, (unsigned)srcStride & 0xffff);
    writeReg(DST_STRIDE, (unsigned)dstStride & 0xffff);
    writeReg(WIDTH, (unsigned)width);
    writeReg(HEIGHT, (unsigned)height);
    writeReg(CONTROL, reverse ? 3 : 1);
    if (!waitIdle()) return -1;
    ++heliosAtwCopies;
    return 1;
}

int heliosAtwFill(void *dstBase, int x, int y, int width, int height,
                  unsigned pixel)
{
    unsigned long dst;
    unsigned word;
    int i;
    if (!heliosAtwFillAvailable() || dstBase != framebuffer) return 0;
    if (x < 0 || y < 0 || width < 0 || height < 0 ||
        x > screenWidth || y > screenHeight ||
        width > screenWidth - x || height > screenHeight - y) return 0;
    if (!width || !height) return 1;
    if ((unsigned long)width * height < MIN_FILL_PIXELS) return 0;
    if (!waitIdle()) return -1;
    /* CTRL=5 reads 64 bytes, then repeats bytes 32..63 horizontally.
     * Source stride zero reuses that row vertically. A uniform tile is
     * independent of alignment/phase. Seed through the uncached CPU alias. */
    word = (pixel & 255U) * 0x01010101U;
    if (cacheKind == 1 && cacheColour == (pixel & 255U)) ++heliosAtwCacheHits;
    else {
        for (i = 0; i < FILL_BYTES / 4; ++i) fillTile[i] = word;
        cacheKind = 1; cacheColour = pixel & 255U; ++heliosAtwStages;
    }
    dst = screenOffset + (unsigned long)y * screenStride + x;
    writeReg(SRC_H, (scratchOffset >> 16) & 0xffff);
    writeReg(SRC_L, scratchOffset & 0xffff);
    writeReg(DST_H, (dst >> 16) & 0xffff);
    writeReg(DST_L, dst & 0xffff);
    writeReg(SRC_STRIDE, 0);
    writeReg(DST_STRIDE, (unsigned)screenStride);
    writeReg(WIDTH, (unsigned)width);
    writeReg(HEIGHT, (unsigned)height);
    writeReg(CONTROL, 5);
    if (!waitIdle()) return -1;
    ++heliosAtwFills;
    return 1;
}

int heliosAtwTile(void *dstBase, int x, int y, int width, int height,
                  const unsigned char *tile, int tileWidth, int tileHeight,
                  int tileStride, int xorigin, int yorigin)
{
    unsigned long src, dst, first;
    int row, column, sx, sy, rows, count, pass, sourceStride, control, band;
    int stageStride, stageRows, phased, miss, k, tx, ty;
    unsigned long stagedSource;
    union { unsigned word; unsigned char bytes[4]; } packed;
    if (!heliosAtwFillAvailable() || !patternAvailable || dstBase != framebuffer ||
        !tile || tileWidth <= 0 || tileWidth > 32 || 32 % tileWidth ||
        tileHeight <= 0 || tileHeight > PATTERN_BYTES / 64 || tileStride < tileWidth)
        return 0;
    if (x < 0 || y < 0 || width < 0 || height < 0 ||
        x > screenWidth || y > screenHeight ||
        width > screenWidth - x || height > screenHeight - y) return 0;
    if (!width || !height) return 1;
    if ((unsigned long)width * height < MIN_FILL_PIXELS) return 0;
    if (!waitIdle()) return -1;
    /* Only periods dividing the engine's 32-byte ring are representable.
     * Phase the staged rows at this clipped box's absolute destination.
     * Modulo separately avoids signed overflow with an extreme origin. */
    sx = (x % tileWidth - xorigin % tileWidth + tileWidth) % tileWidth;
    sy = (y % tileHeight - yorigin % tileHeight + tileHeight) % tileHeight;
    rows = height < tileHeight ? height : tileHeight;
    /* Cache a phase-independent tile with enough right/bottom repetition
     * to start at any phase. Tall patterns which cannot fit use a phased
     * 64-byte-row layout. Compare CONTENT, not a mutable pixmap pointer or
     * drawable serial (pixel edits do not necessarily change the serial). */
    stageStride = (64 + tileWidth - 1 + 3) & ~3;
    stageRows = tileHeight * 2 - 1;
    phased = stageStride * stageRows > PATTERN_BYTES;
    if (phased) { stageStride = 64; stageRows = tileHeight; }
    miss = cacheKind != 2 || cacheWidth != tileWidth || cacheHeight != tileHeight ||
           cachePhased != phased || (phased && (cacheX != sx || cacheY != sy));
    for (row = 0; row < tileHeight && !miss; ++row)
        for (column = 0; column < tileWidth; ++column)
            if (cachePixels[row * tileWidth + column] !=
                tile[(unsigned long)row * tileStride + column]) { miss = 1; break; }
    if (miss) {
        for (row = 0; row < tileHeight; ++row)
            for (column = 0; column < tileWidth; ++column)
                cachePixels[row * tileWidth + column] = tile[(unsigned long)row * tileStride + column];
        for (row = 0; row < stageRows; ++row) {
            ty = ((phased ? sy : 0) + row) % tileHeight;
            for (column = 0; column < stageStride; column += 4) {
                for (k = 0; k < 4; ++k) {
                    tx = ((phased ? sx : 0) + column + k) % tileWidth;
                    packed.bytes[k] = cachePixels[ty * tileWidth + tx];
                }
                fillTile[(row * stageStride + column) / 4] = packed.word;
            }
        }
        cacheKind = 2; cacheWidth = tileWidth; cacheHeight = tileHeight;
        cachePhased = phased; cacheX = sx; cacheY = sy;
        ++heliosAtwStages;
    } else {
        ++heliosAtwCacheHits;
    }
    stagedSource = scratchOffset + (phased ? 0 : sy * stageStride + sx);
    first = screenOffset + (unsigned long)y * screenStride + x;
    band = tileHeight == 1 ? height : rows;
    for (pass = 0; pass < 2; ++pass) {
        if (!pass) {
            src = stagedSource; dst = first; count = band;
            sourceStride = tileHeight == 1 ? 0 : stageStride;
            control = 5;
        } else {
            if (height <= band) break;
            /* Deliberately forward, overlapping COPY2D: newly written rows
             * feed subsequent rows, propagating the initial pattern band.
             * Do NOT use heliosAtwCopy's memmove/reverse-overlap semantics.
             * ATW fVDI atw_fill.c's two-blit path documents hardware proof.
             * Its scanout-source rewind hazard is harmless only for the
             * horizontal periods dividing 32 accepted above. */
            src = first; dst = first + (unsigned long)band * screenStride;
            count = height - band; sourceStride = screenStride; control = 1;
        }
        writeReg(SRC_H, (src >> 16) & 0xffff);
        writeReg(SRC_L, src & 0xffff);
        writeReg(DST_H, (dst >> 16) & 0xffff);
        writeReg(DST_L, dst & 0xffff);
        writeReg(SRC_STRIDE, (unsigned)sourceStride);
        writeReg(DST_STRIDE, (unsigned)screenStride);
        writeReg(WIDTH, (unsigned)width);
        writeReg(HEIGHT, (unsigned)count);
        writeReg(CONTROL, (unsigned)control);
        if (!waitIdle()) return -1;
    }
    ++heliosAtwTiles;
    return 1;
}
