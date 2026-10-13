/* Helios device-dependent X11R4 layer. One 8-bit cfb screen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <helios.h>
#include <device.h>
#undef BYTE
#include "X.h"
#define NEED_EVENTS
#include "Xproto.h"
#include "scrnintstr.h"
#include "colormapst.h"
#include "resource.h"
#include "inputstr.h"
#include "opaque.h"
#include "servermd.h"
#include "mipointer.h"
#include "hardware.h"
#include "helios_input.h"
#include "config.h"
#include "atw_copy2d.h"
#include "windowstr.h"
#include "propertyst.h"
#include "Xatom.h"

extern Bool cfbScreenInit(), cfbCreateDefColormap(), miDCInitialize();
extern int TellLostMap(), TellGainedMap();
extern void miRegisterPointerDevice();
extern int monitorResolution;
extern WindowPtr *WindowTable;
extern void ShmRegisterFbFuncs();
extern word MachineName(byte *name);

static Hardware_Device *hardware;
static unsigned char *pixels;
static char *hardwareName;
static char *configName, *configBuffer;
static char configuredHardware[256], configuredKeyboard[256];
static int inputOption = -1, keyboardOption;
static int atwCopyOption;
static unsigned long atwVramEnd = HELIOS_ATW_VRAM_END;
static int width = 640, height = 480, stride;
/* MIT-SHM: one client region, in VRAM with -atw-copy2d (64 KiB by
 * default), else in RAM (only with -shm-kb). Its address is the shmid;
 * _HELIOS_SHM on the root says where. */
#define SHM_DEFAULT ((unsigned long)-1)
static unsigned long shmBytes = SHM_DEFAULT;
static char *shmRegion, *shmMemory;
static ColormapPtr installed;
static Bool (*savedCloseScreen)();
static DevicePtr pointerDevice, keyboardDevice;
long heliosLastInputTime;
int heliosNativeInput;
char *heliosKeyboardType = "PC_UK";
int heliosRepeatDelay = 500, heliosRepeatInterval = 60;
static unsigned short red[256], green[256], blue[256];

static void readPalette(ColormapPtr map)
{
    Pixel indexes[256];
    xrgb colours[256];
    int i;
    for (i = 0; i < 256; ++i) indexes[i] = i;
    if (QueryColors(map, 256, indexes, colours) != Success)
        FatalError("Cannot read installed colormap");
    for (i = 0; i < 256; ++i) {
        red[i] = colours[i].red;
        green[i] = colours[i].green;
        blue[i] = colours[i].blue;
    }
}

static void flushPalette(void)
{
    ColormapReq request;
    if (!hardware) return;
    memset(&request, 0, sizeof(request));
    request.DevReq.Request = X_INSTALL_COLOURS;
    request.count = 256;
    request.rmap = red;
    request.gmap = green;
    request.bmap = blue;
    Operate(&hardware->dcb, &request);
}

static void storeColors(ColormapPtr map, int count, xColorItem *items)
{
    if (map != installed) return;
    /* QueryColors also expands DirectColor's separate component tables. */
    readPalette(map);
    flushPalette();
}

static void installColormap(ColormapPtr map)
{
    if (installed == map) return;
    if (installed) WalkTree(map->pScreen, TellLostMap, (char *)&installed->mid);
    installed = map;
    readPalette(map);
    flushPalette();
    WalkTree(map->pScreen, TellGainedMap, (char *)&map->mid);
}

static void uninstallColormap(ColormapPtr map)
{
    ColormapPtr def;
    if (installed != map || map->mid == map->pScreen->defColormap) return;
    def = (ColormapPtr)LookupIDByType(map->pScreen->defColormap, RT_COLORMAP);
    /* Through the screen, as cfb does: miSprite wraps InstallColormap to
     * follow the installed map. Calling installColormap directly left it
     * on a client's freed colormap (white cursor, then a hang). */
    if (def) (*map->pScreen->InstallColormap)(def);
}

static int listColormaps(ScreenPtr screen, Colormap *maps)
{
    if (!installed) return 0;
    maps[0] = installed->mid;
    return 1;
}

static Bool saveScreen(ScreenPtr screen, int state) { return TRUE; }
static long eventTime(ScreenPtr screen) { return GetTimeInMillis(); }
static Bool offScreen(ScreenPtr *screen, int *x, int *y) { return FALSE; }
static void crossScreen(ScreenPtr screen, Bool entering) { }
static void queueEvent(xEvent *event, DevicePtr device, ScreenPtr screen)
{
    (*device->processInputProc)(event, device, 1);
}
static miPointerCursorFuncRec cursorFuncs = {
    eventTime, offScreen, crossScreen, queueEvent
};

static Bool closeScreen(int index, ScreenPtr screen)
{
    Bool result;
    if (atwCopyOption)
        ErrorF("Xhelios: ATW COPY2D completed %lu copies, %lu fills, %lu tiles\n",
               heliosAtwCopies, heliosAtwFills, heliosAtwTiles);
    if (atwCopyOption)
        ErrorF("Xhelios: ATW pattern cache %lu uploads, %lu hits\n",
               heliosAtwStages, heliosAtwCacheHits);
    screen->CloseScreen = savedCloseScreen;
    result = (*savedCloseScreen)(index, screen);
    /* A DIX reset follows the last client disconnect (including startup
     * probes). Keep hardware in graphics mode until ddxGiveUp/AbortDDX. */
    if (!hardware) { free(pixels); pixels = NULL; }
    installed = NULL;
    return result;
}

static const char *defaultVariable(const char *buffer, const char *name)
{
    const char *value = heliosConfigValue(buffer, name);
    if (value) return value;
    /* ATW hardware can obtain its geometry from the shared ATWINFO block. */
    if (!strcmp(name, "SCREEN_ATWINFO")) return "1";
    return NULL;
}

static int configInt(const char *name, int fallback, int minimum, int maximum)
{
    const char *value = heliosConfigValue(configBuffer, name);
    char *end;
    long number;
    if (!value) return fallback;
    number = strtol(value, &end, 0);
    if (!*value || *end || number < minimum || number > maximum)
        FatalError("Invalid newXrc value for %s", name);
    return number;
}

static void loadConfig(void)
{
    FILE *file;
    long length;
    const char *value;
    if (configName) {
        file = fopen(configName, "rb");
        if (!file) FatalError("Cannot read newXrc file %s", configName);
        if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || length > 65536)
            FatalError("Invalid newXrc file size");
        rewind(file);
        free(configBuffer);
        configBuffer = malloc(length + 1);
        if (!configBuffer) FatalError("Cannot allocate newXrc buffer");
        if (fread(configBuffer, 1, length, file) != length)
            FatalError("Cannot read complete newXrc file");
        fclose(file);
        configBuffer[length] = 0;
        value = heliosConfigValue(configBuffer, "HARDWARE_DEVICE");
        if (value && (!hardwareName || hardwareName == configuredHardware)) {
            strcpy(configuredHardware, value);
            hardwareName = configuredHardware;
        }
        value = heliosConfigValue(configBuffer, "KEYBOARD");
        if (value && !keyboardOption) {
            strcpy(configuredKeyboard, value);
            heliosKeyboardType = configuredKeyboard;
        }
    }
    width = configInt("SCREEN_WIDTH", 640, 1, 32767);
    height = configInt("SCREEN_HEIGHT", 480, 1, 32767);
    heliosRepeatDelay = configInt("AUTO_REPEAT_DELAY", 500, 1, 60000);
    heliosRepeatInterval = configInt("AUTO_REPEAT_INTERVAL", 60, 1, 60000);
    heliosNativeInput = inputOption < 0 ? hardwareName != NULL : inputOption;
    if (strcmp(heliosKeyboardType, "PC_UK") && strcmp(heliosKeyboardType, "PC_GERMAN") &&
        strcmp(heliosKeyboardType, "PC_USA") && strcmp(heliosKeyboardType, "PC") &&
        strcmp(heliosKeyboardType, "ST") && strcmp(heliosKeyboardType, "ST_GERMAN"))
        FatalError("Unknown keyboard type %s", heliosKeyboardType);
}

static Bool screenInit(int index, ScreenPtr screen, int argc, char **argv)
{
    int dpi = monitorResolution ? monitorResolution : 75;
    stride = (width + 3) & ~3;
    if (hardwareName) {
        Info info;
        info.get_var = defaultVariable;
        info.pbuffer = configBuffer ? configBuffer : "";
        if (!hardware) {
            hardware = (Hardware_Device *)OpenDevice(hardwareName, &info);
            if (hardware)
                ErrorF("Xhelios: %dx%d, 8-bit hardware framebuffer\n",
                       hardware->VRAM_width, hardware->VRAM_height);
        }
        if (!hardware) FatalError("Cannot open framebuffer device %s", hardwareName);
        if (hardware->VRAM_depth != 8 || hardware->VRAM_horizontal_stride != 1 ||
            hardware->VRAM_vertical_stride <= 0 || (hardware->VRAM_vertical_stride & 3) ||
            hardware->VRAM_width < 1 || hardware->VRAM_width > 32767 ||
            hardware->VRAM_height < 1 || hardware->VRAM_height > 32767 ||
            (unsigned long)hardware->VRAM_vertical_stride < hardware->VRAM_width)
            FatalError("Framebuffer needs 8-bit pixels and a positive, word-aligned stride");
        width = hardware->VRAM_width;
        height = hardware->VRAM_height;
        stride = hardware->VRAM_vertical_stride;
        pixels = hardware->VRAM_base_address;
    } else {
        pixels = (unsigned char *)calloc(height, stride);
        if (!pixels) return FALSE;
    }
    HTRACE("framebuffer", pixels, width, height, stride);
    if (atwCopyOption) {
        if (!hardware || !heliosAtwInit(pixels, width, height, stride))
            FatalError("ATW COPY2D needs an 8-bit framebuffer in the 0x80600000 aperture, stride <= 2047");
        /* Cursor saves, GetImage and software read/modify/write operations
         * must see pixels written by the FPGA, not cached snapshots. */
        pixels = heliosAtwFramebuffer();
        HTRACE("framebuffer.uncached", pixels, width, height, stride);
        ErrorF("Xhelios: ATW COPY2D enabled (uncached CPU framebuffer)\n");
        ErrorF("Xhelios: ATW solid fills %s\n",
               heliosAtwFillAvailable() ? "enabled (scratch after framebuffer)" :
                                         "disabled (no offscreen scratch space)");
        ErrorF("Xhelios: ATW pixmap pool %lu bytes (exclusive end offset %lu KiB)\n",
                   heliosAtwVramSetup(atwVramEnd), atwVramEnd / 1024);
    }
    shmRegion = 0;
    if (shmBytes == SHM_DEFAULT) shmBytes = atwCopyOption ? 64 * 1024 : 0;
    if (shmBytes && atwCopyOption) {
        shmRegion = heliosAtwReserve(shmBytes);
        if (!shmRegion)
            ErrorF("Xhelios: no VRAM left for the %lu KiB MIT-SHM region\n", shmBytes / 1024);
    } else if (shmBytes) {
        /* Kept across server resets: clients may still point at it. */
        if (!shmMemory) shmMemory = (char *)malloc(shmBytes);
        shmRegion = shmMemory;
    }
    if (shmRegion)
        ErrorF("Xhelios: MIT-SHM region %lu KiB at %08lx\n", shmBytes / 1024,
               (unsigned long)shmRegion);
    if (!cfbScreenInit(screen, pixels, width, height, dpi, dpi, stride)) return FALSE;
    ShmRegisterFbFuncs(screen);
    screen->SaveScreen = saveScreen;
    screen->InstallColormap = installColormap;
    screen->UninstallColormap = uninstallColormap;
    screen->ListInstalledColormaps = listColormaps;
    screen->StoreColors = storeColors;
    if (!miDCInitialize(screen, &cursorFuncs)) return FALSE;
    savedCloseScreen = screen->CloseScreen;
    screen->CloseScreen = closeScreen;
    if (!cfbCreateDefColormap(screen)) return FALSE;
    if (!hardware)
        ErrorF("Xhelios: %dx%d, 8-bit memory framebuffer\n", width, height);
    return TRUE;
}

void InitOutput(ScreenInfo *info, int argc, char **argv)
{
    loadConfig();
    info->imageByteOrder = IMAGE_BYTE_ORDER;
    info->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    info->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    info->bitmapBitOrder = BITMAP_BIT_ORDER;
    info->numPixmapFormats = 2;
    info->formats[0].depth = info->formats[0].bitsPerPixel = 1;
    info->formats[1].depth = info->formats[1].bitsPerPixel = 8;
    info->formats[0].scanlinePad = info->formats[1].scanlinePad = BITMAP_SCANLINE_PAD;
    if (AddScreen(screenInit, argc, argv) < 0) FatalError("Cannot initialize screen");
}

static int motionEvents() { return 0; }
static void pointerControl(DevicePtr device, PtrCtrl *control) { }
static void bell(int volume, DevicePtr device, KeybdCtrl *control) { }
static void keyboardControl(DevicePtr device, KeybdCtrl *control) { }

static int pointerProc(DevicePtr device, int what)
{
    BYTE map[4];
    int status;
    if (what == DEVICE_INIT) {
        map[1] = 1; map[2] = 2; map[3] = 3;
        if (!InitPointerDeviceStruct(device, map, 3, motionEvents, pointerControl, 0))
            return BadAlloc;
    }
    status = heliosInputSwitch(FALSE, what);
    if (status != Success) return status;
    device->on = (what == DEVICE_ON);
    return Success;
}

static int keyboardProc(DevicePtr device, int what)
{
    KeySymsRec syms;
    CARD8 mods[256];
    int status;
    if (what == DEVICE_INIT) {
        heliosKeymap(&syms, mods);
        status = InitKeyboardDeviceStruct(device, &syms, mods, bell, keyboardControl);
        free(syms.map);
        if (!status) return BadAlloc;
    }
    status = heliosInputSwitch(TRUE, what);
    if (status != Success) return status;
    device->on = (what == DEVICE_ON);
    return Success;
}

/* shm.c: the only segment a client can attach is the region. */
char *heliosShmAttach(unsigned long shmid, unsigned long *size)
{
    if (!shmRegion || shmid != (unsigned long)shmRegion) return 0;
    *size = shmBytes;
    return shmRegion;
}

/* _HELIOS_SHM = "address size processor": a client on the same
 * processor writes its image there and passes the address as shmid. */
static void shmProperty(void)
{
    WindowPtr root = WindowTable[0];
    PropertyPtr prop;
    char text[320], name[256];
    int len;
    if (!shmRegion) return;
    if (MachineName((byte *)name) < 0) name[0] = 0;
    sprintf(text, "%lu %lu %s", (unsigned long)shmRegion, shmBytes, name);
    len = strlen(text);
    if (!root->optional && !MakeWindowOptional(root)) return;
    prop = (PropertyPtr)xalloc(sizeof(PropertyRec));
    if (!prop) return;
    prop->data = (pointer)xalloc(len);
    if (!prop->data) { xfree(prop); return; }
    memcpy(prop->data, text, len);
    prop->propertyName = MakeAtom("_HELIOS_SHM", 11, TRUE);
    prop->type = XA_STRING;
    prop->format = 8;
    prop->size = len;
    prop->next = root->optional->userProps;
    root->optional->userProps = prop;
}

void InitInput(int argc, char **argv)
{
    shmProperty();
    heliosLastInputTime = GetTimeInMillis();
    pointerDevice = AddInputDevice(pointerProc, TRUE);
    keyboardDevice = AddInputDevice(keyboardProc, TRUE);
    if (!pointerDevice || !keyboardDevice) FatalError("Cannot allocate input devices");
    RegisterPointerDevice(pointerDevice);
    RegisterKeyboardDevice(keyboardDevice);
    miRegisterPointerDevice(screenInfo.screens[0], pointerDevice);
    heliosInitInput(pointerDevice, keyboardDevice);
}

Bool LegalModifier(unsigned int key, DevicePtr device) { return TRUE; }
int TimeSinceLastInputEvent(void) { return (unsigned long)GetTimeInMillis() - (unsigned long)heliosLastInputTime; }
void DDXRingBell(int volume, int pitch, int duration) { }
void AbortDDX(void)
{
    heliosInputSwitch(FALSE, DEVICE_CLOSE);
    heliosInputSwitch(TRUE, DEVICE_CLOSE);
    if (hardware) { CloseDevice(&hardware->dcb); hardware = NULL; }
}
void ddxGiveUp(void) { AbortDDX(); }
void ddxUseMsg(void)
{
    ErrorF("-hardware path         use a Helios framebuffer driver (default: memory)\n");
    ErrorF("-config path           load a newXrc resource file\n");
    ErrorF("notcp                  native Helios connections only\n");
    ErrorF("-input / -noinput      enable / disable native mouse and keyboard\n");
    ErrorF("-keyboard type         PC_UK, PC_USA, PC_GERMAN, ST or ST_GERMAN\n");
    ErrorF("-trace path            write move diagnostics and errors to a file\n");
    ErrorF("-atw-copy2d            enable ATW800/2 FPGA copies and fills\n");
    ErrorF("-shm-kb N              MIT-SHM client region in KiB (-atw-copy2d: default 64, 0 disables)\n");
    ErrorF("-atw-vram-kb N         reserve VRAM after screen to exclusive end N KiB (max 1999; default automatic)\n");
}
int ddxProcessArgument(int argc, char **argv, int index)
{
#ifdef HELIOS_INPUT_TEST
    extern char *heliosTestInputPath;
    if (!strcmp(argv[index], "-test-input") && index + 1 < argc) {
        heliosTestInputPath = argv[index + 1]; inputOption = TRUE; return 2;
    }
#endif
    if (!strcmp(argv[index], "notcp")) return 1;
    if (!strcmp(argv[index], "-atw-copy2d")) { atwCopyOption = TRUE; return 1; }
    if (!strcmp(argv[index], "-atw-vram-kb")) {
        char *end;
        long kib;
        if (index + 1 >= argc) FatalError("-atw-vram-kb needs an exclusive end offset in KiB");
        kib = strtol(argv[index + 1], &end, 10);
        if (end == argv[index + 1] || *end || kib < 0 || kib > 1999)
            FatalError("-atw-vram-kb must be 0..1999 (0 disables the pixmap pool)");
        atwVramEnd = (unsigned long)kib * 1024;
        if (kib) atwCopyOption = TRUE;
        return 2;
    }
    if (!strcmp(argv[index], "-shm-kb")) {
        char *end;
        long kib;
        if (index + 1 >= argc) FatalError("-shm-kb needs a size in KiB");
        kib = strtol(argv[index + 1], &end, 10);
        if (end == argv[index + 1] || *end || kib < 0 || kib > 1024)
            FatalError("-shm-kb must be 0..1024");
        shmBytes = (unsigned long)kib * 1024;
        return 2;
    }
    if (!strcmp(argv[index], "-trace") && index + 1 < argc) {
        heliosTracePath = argv[index + 1]; return 2;
    }
    if (!strcmp(argv[index], "-hardware") && index + 1 < argc) {
        hardwareName = argv[index + 1];
        return 2;
    }
    if (!strcmp(argv[index], "-config") && index + 1 < argc) {
        configName = argv[index + 1]; return 2;
    }
    if (!strcmp(argv[index], "-input")) { inputOption = TRUE; return 1; }
    if (!strcmp(argv[index], "-noinput")) { inputOption = FALSE; return 1; }
    if (!strcmp(argv[index], "-keyboard") && index + 1 < argc) {
        heliosKeyboardType = argv[index + 1];
        keyboardOption = TRUE;
        return 2;
    }
    return 0;
}
