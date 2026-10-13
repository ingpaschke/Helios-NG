#include <string.h>

/* Recovered tables contain seven leading rows for reserved X keycodes.
 * Keep the X keycode (native scan code + 7), but omit those rows from the
 * map whose advertised minimum is 8. The old modifier table has a separate
 * offset and inconsistent Alt/NumLock entries, so derive it from keysyms. */
static void normalizeKeymap(KeySymsPtr syms, CARD8 mods[256])
{
    int key;
    syms->minKeyCode = 8;
    memmove(syms->map, syms->map + 7 * syms->mapWidth,
            (syms->maxKeyCode - syms->minKeyCode + 1) * syms->mapWidth * sizeof(KeySym));
    memset(mods, 0, 256);
    for (key = syms->minKeyCode; key <= syms->maxKeyCode; ++key) {
        switch (syms->map[(key - syms->minKeyCode) * syms->mapWidth]) {
        case XK_Shift_L: case XK_Shift_R: mods[key] = ShiftMask; break;
        case XK_Control_L: case XK_Control_R: mods[key] = ControlMask; break;
        case XK_Caps_Lock: case XK_Shift_Lock: mods[key] = LockMask; break;
        case XK_Alt_L: case XK_Alt_R: mods[key] = Mod1Mask; break;
        case XK_Num_Lock: mods[key] = Mod2Mask; break;
        }
    }
}
