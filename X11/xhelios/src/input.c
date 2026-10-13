/* Drain native receiver queues on the X dispatch thread. Receiver threads
 * never access DIX objects and are joined before a server reset. */
#include <string.h>
#include <stdlib.h>
#ifdef HELIOS_INPUT_TEST
#include <stdio.h>
char *heliosTestInputPath;
#endif
#include <sys/time.h>
#include <helios.h>
#include <syslib.h>
#include <gsp.h>
#include <ioevents.h>
#include "native_events.h"
#undef BYTE
#define NEED_EVENTS
#include "X.h"
#include "Xproto.h"
#include "inputstr.h"
#include "scrnintstr.h"
#include "opaque.h"
#include "mipointer.h"
#include "helios_input.h"
#include "keysym.h"
#include "keymap_normalize.h"
#include "keymap_st_german.h"

extern void Acknowledge(Stream *, word), NegAcknowledge(Stream *, word);
extern void build_translation_tables(int, KeySymsPtr *, CARD8 **);
extern int heliosNativeInput;
extern char *heliosKeyboardType;
extern long heliosLastInputTime;

typedef struct {
    IOEventHdr hdr;
    union { Mouse_Event mouse; Keyboard_Event keyboard; } data;
} NativeEvent;
typedef struct {
    HeliosEventSource native;
    word expected;
    int haveCounter;
} EventSource;
static EventSource mouse, keyboard;
static HeliosEventNotify inputNotify;
static int notifyOpen, processingInput;
static DevicePtr pointerDevice, keyboardDevice;
static int havePosition, rawX, rawY, repeatKey;
static long repeatAt;

static void closeSource(EventSource *source)
{
    heliosCloseEvents(&source->native);
    memset(source, 0, sizeof(*source));
}

static Bool openSource(EventSource *source, char *name, word type)
{
#ifdef HELIOS_INPUT_TEST
    if (heliosTestInputPath) {
        FILE *file;
        if (!heliosOpenEvents(&source->native, NULL, type, &inputNotify)) return FALSE;
        if (type == Event_Mouse) {
            file = fopen(heliosTestInputPath, "w");
            if (!file) FatalError("Cannot publish test input port");
            if (fprintf(file, "%lx\n", (unsigned long)source->native.port) < 0 ||
                fclose(file)) FatalError("Cannot write test input port");
        }
        return TRUE;
    }
#endif
    return heliosOpenEvents(&source->native, name, type, &inputNotify);
}

static void sendKey(int key, int type)
{
    xEvent event;
    memset(&event, 0, sizeof(event));
    event.u.u.type = type;
    event.u.u.detail = key;
    event.u.keyButtonPointer.time = heliosLastInputTime = GetTimeInMillis();
    miPointerPosition(screenInfo.screens[0], &event.u.keyButtonPointer.rootX,
                       &event.u.keyButtonPointer.rootY);
    (*keyboardDevice->processInputProc)(&event, keyboardDevice, 1);
}

static void keyEvent(NativeEvent *event)
{
    int key = event->data.keyboard.Key + 7;
    int down = event->data.keyboard.What == Keys_KeyDown;
    DeviceIntPtr device = (DeviceIntPtr)keyboardDevice;
    if (key < device->key->curKeySyms.minKeyCode || key > device->key->curKeySyms.maxKeyCode)
        return;
    if (!down && event->data.keyboard.What != Keys_KeyUp) return;
    sendKey(key, down ? KeyPress : KeyRelease);
    if (down && !device->key->modifierMap[key]) {
        repeatKey = key;
        repeatAt = (unsigned long)GetTimeInMillis() + heliosRepeatDelay;
    } else if (!down && repeatKey == key) repeatKey = 0;
}

static int accelerate(int delta, PtrCtrl *ctrl)
{
    int magnitude = delta < 0 ? -delta : delta;
    if (ctrl->den > 0 && magnitude > ctrl->threshold)
        magnitude = ctrl->threshold + (magnitude - ctrl->threshold) * ctrl->num / ctrl->den;
    return delta < 0 ? -magnitude : magnitude;
}

static void mouseEvent(NativeEvent *event)
{
    xEvent out;
    int x = event->data.mouse.X, y = event->data.mouse.Y, dx, dy, bit;
    unsigned buttons = event->data.mouse.Buttons;
    PtrCtrl *ctrl = &((DeviceIntPtr)pointerDevice)->ptrfeed->ctrl;
    heliosLastInputTime = GetTimeInMillis();
    if (havePosition) {
        dx = (short)(x - rawX); dy = (short)(y - rawY);
        if (dx || dy) {
            HTRACE("input.motion.begin", dx, dy, x, y);
            miPointerDeltaCursor(screenInfo.screens[0], accelerate(dx, ctrl),
                                 accelerate(dy, ctrl), TRUE);
            HTRACE("input.motion.end", 0, 0, 0, 0);
        }
    }
    rawX = x; rawY = y; havePosition = TRUE;
    memset(&out, 0, sizeof(out));
    out.u.u.type = (buttons & 0x8000) ? ButtonRelease : ButtonPress;
    out.u.keyButtonPointer.time = heliosLastInputTime;
    miPointerPosition(screenInfo.screens[0], &out.u.keyButtonPointer.rootX,
                      &out.u.keyButtonPointer.rootY);
    for (bit = 0; bit < 3; ++bit) {
        if (!(buttons & (1 << bit))) continue;
        out.u.u.detail = bit == 0 ? 1 : bit == 1 ? 3 : 2;
        HTRACE("input.button.begin", out.u.u.type, out.u.u.detail, 0, 0);
        (*pointerDevice->processInputProc)(&out, pointerDevice, 1);
        HTRACE("input.button.end", 0, 0, 0, 0);
    }
}

static void drain(EventSource *source, word type, void (*consume)(NativeEvent *))
{
    HeliosEventPacket message;
    NativeEvent *event;
    int packet, count, i, ready;
    word result;
    for (packet = 0; packet < 32; ++packet) {
        ready = heliosReadEvents(&source->native, &message);
        if (ready < 0) FatalError("Native input receiver failed");
        if (!ready) break;
        result = message.result;
        if (message.size % sizeof(NativeEvent)) continue;
        event = (NativeEvent *)message.data.bytes;
        count = message.size / sizeof(NativeEvent);
        for (i = 0; i < count; ++i, ++event) {
            if (type == Event_Keyboard && source->haveCounter &&
                event->hdr.Counter != source->expected) {
                if ((long)((unsigned long)event->hdr.Counter - (unsigned long)source->expected) < 0)
                    continue;
                if (!(result & EventRc_IgnoreLost)) {
                    NegAcknowledge(source->native.stream, source->expected);
                    break;
                }
            }
            source->expected = event->hdr.Counter + 1;
            source->haveCounter = TRUE;
            if (event->hdr.Type == type) consume(event);
        }
        if (count && i == count && (result & EventRc_Acknowledge))
            Acknowledge(source->native.stream, source->expected - 1);
    }
}

void ProcessInputEvents(void)
{
    DeviceIntPtr device;
    KeybdCtrl *ctrl;
    if (!heliosNativeInput || !notifyOpen || processingInput) return;
    processingInput = TRUE;
    HTRACE("input.notify", 0, 0, 0, 0);
    if (!heliosConsumeEventNotify(&inputNotify))
        FatalError("Native input wakeup failed");
    HTRACE("input.mouse", 0, 0, 0, 0);
    drain(&mouse, Event_Mouse, mouseEvent);
    HTRACE("input.keyboard", 0, 0, 0, 0);
    drain(&keyboard, Event_Keyboard, keyEvent);
    HTRACE("input.drained", 0, 0, 0, 0);
    if (!repeatKey || !keyboard.native.port) { processingInput = FALSE; return; }
    device = (DeviceIntPtr)keyboardDevice;
    ctrl = &device->kbdfeed->ctrl;
    if (ctrl->autoRepeat && (ctrl->autoRepeats[repeatKey >> 3] & (1 << (repeatKey & 7))) &&
        (long)((unsigned long)GetTimeInMillis() - (unsigned long)repeatAt) >= 0) {
        sendKey(repeatKey, KeyRelease);
        sendKey(repeatKey, KeyPress);
        repeatAt = (unsigned long)GetTimeInMillis() + heliosRepeatInterval;
    }
    processingInput = FALSE;
}

static void block(pointer data, struct timeval **timeout, pointer mask)
{
    static struct timeval pollTime;
    ProcessInputEvents();
    if (!repeatKey) return;
    /* Native input wakes select through EnabledDevices, even under a server
     * grab. Only software key repeat needs a timer. */
    if (!*timeout || (*timeout)->tv_sec || (*timeout)->tv_usec > 20000) {
        pollTime.tv_sec = 0; pollTime.tv_usec = 20000;
        *timeout = &pollTime;
    }
}
static void wakeup(pointer data, unsigned long result, pointer mask) { ProcessInputEvents(); }

void heliosInitInput(DevicePtr pointer, DevicePtr keys)
{
    pointerDevice = pointer; keyboardDevice = keys;
    havePosition = repeatKey = 0;
    processingInput = FALSE;
    inputNotify.produced = inputNotify.consumed = 0;
    SetInputCheck(&inputNotify.produced, &inputNotify.consumed);
    if (heliosNativeInput) {
        if (!heliosInitEventNotify(&inputNotify))
            FatalError("Cannot create native input wakeup pipe");
        notifyOpen = TRUE;
        if (inputNotify.pipe[0] >= NOFILE - 1)
            FatalError("Native input descriptor exceeds select mask");
        AddEnabledDevice(inputNotify.pipe[0]);
    }
    if (heliosNativeInput && !RegisterBlockAndWakeupHandlers(block, wakeup, NULL))
        FatalError("Cannot register native input polling");
}

int heliosInputSwitch(int isKeyboard, int what)
{
    EventSource *source = isKeyboard ? &keyboard : &mouse;
    if (!heliosNativeInput) return Success;
    if (what == DEVICE_ON && !source->native.running) {
        if (!openSource(source, isKeyboard ? "/keyboard" : "/mouse",
                        isKeyboard ? Event_Keyboard : Event_Mouse)) {
            ErrorF("Cannot open native %s event source\n", isKeyboard ? "keyboard" : "mouse");
            return BadImplementation;
        }
    } else if (what == DEVICE_OFF || what == DEVICE_CLOSE) {
        closeSource(source);
        if (isKeyboard) repeatKey = 0;
        else havePosition = 0;
        if (what == DEVICE_CLOSE && notifyOpen &&
            !mouse.native.running && !keyboard.native.running) {
            RemoveEnabledDevice(inputNotify.pipe[0]);
            heliosCloseEventNotify(&inputNotify);
            notifyOpen = FALSE;
        }
    }
    return Success;
}

Bool heliosKeymap(KeySymsPtr syms, CARD8 mods[256])
{
    KeySymsPtr legacy;
    CARD8 *legacyMods;
    int type = 2; /* recovered enum: PC_GERMAN, PC, PC_UK, ST */
    if (!strcmp(heliosKeyboardType, "PC_GERMAN")) type = 0;
    else if (!strcmp(heliosKeyboardType, "PC") || !strcmp(heliosKeyboardType, "PC_USA")) type = 1;
    else if (!strcmp(heliosKeyboardType, "ST") || !strcmp(heliosKeyboardType, "ST_GERMAN")) type = 3;
    build_translation_tables(type, &legacy, &legacyMods);
    *syms = *legacy;
    normalizeKeymap(syms, mods);
    if (!strcmp(heliosKeyboardType, "ST_GERMAN")) germanSTKeymap(syms);
    ErrorF("Xhelios: keyboard %s (native scan code + 7)\n", heliosKeyboardType);
    free(legacyMods);
    free(legacy);
    return TRUE;
}
