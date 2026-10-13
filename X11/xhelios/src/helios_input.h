#ifndef HELIOS_INPUT_H
#define HELIOS_INPUT_H
extern int heliosNativeInput;
extern char *heliosKeyboardType;
extern long heliosLastInputTime;
extern int heliosRepeatDelay, heliosRepeatInterval;
void heliosInitInput(DevicePtr pointerDevice, DevicePtr keyboardDevice);
int heliosInputSwitch(int isKeyboard, int what);
Bool heliosKeymap(KeySymsPtr syms, CARD8 mods[256]);
#endif
