/* Optional Linux LinkIO backend. Include after helios.h.
 * Callbacks run on the coroutine thread and use the server's current_link.
 * Open/free receive a link-table index. Byte/block transfers return the number
 * of bytes not transferred (zero means success), matching the LinkIO ABI. */
#ifndef IOS_LINKDRV_H
#define IOS_LINKDRV_H

struct ios_link_driver {
    int (*open)(int link_index);
    void (*free)(int link_index);
    void (*reset)(void);
    void (*analyse)(void);
    WORD (*rdrdy)(void);
    WORD (*wrrdy)(void);
    WORD (*read_byte)(UBYTE *destination);
    WORD (*write_byte)(int value);
    WORD (*send)(int count, BYTE *data, int timeout);
    WORD (*fetch)(int count, BYTE *data, int timeout);
};

void ios_linux_register_link(const struct ios_link_driver *driver);
#endif
