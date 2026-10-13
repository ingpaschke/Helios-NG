#ifndef HELIOS_X_CONFIG_H
#define HELIOS_X_CONFIG_H
/* Result remains valid until the next call. NULL means absent or malformed. */
const char *heliosConfigValue(const char *buffer, const char *name);
#endif
