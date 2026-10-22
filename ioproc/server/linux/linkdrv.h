/* Optional Linux LinkIO backend. Include after helios.h. */
#ifndef IOS_LINKDRV_H
#define IOS_LINKDRV_H
struct ios_link_driver {
    int (*open)(int);
    void (*free)(int);
    void (*reset)(void);
    void (*analyse)(void);
    WORD (*rdrdy)(void);
    WORD (*wrrdy)(void);
    WORD (*read_byte)(UBYTE *);
    WORD (*write_byte)(int);
    WORD (*send)(int, BYTE *, int);
    WORD (*fetch)(int, BYTE *, int);
};
void ios_linux_register_link(const struct ios_link_driver *);
#endif
