/* Linux host: system headers + I/O Server headers (Linux). */
#ifndef IOS_LINUX_HELIOS_H
#define IOS_LINUX_HELIOS_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/file.h>
#include <memory.h>
#include <sys/vfs.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/un.h>
#include <dirent.h>
#include <termios.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <utime.h>
/* SunOS declared these in <stdio.h>/<unistd.h>; glibc only with _GNU_SOURCE */
extern char *cuserid(char *);
#include "defines.h"
#include "barthdr"
#include "protocol.h"
#include "structs.h"
#include "fundefs.h"
#include "server.h"
#include "debugopt.h"
#include "sun/sunlocal.h"
/* Wire layout must not depend on the host's pointer/long/time_t width. */
typedef char ios_word_is_32[(sizeof(WORD) == 4) ? 1 : -1];
typedef char ios_header_is_16[(sizeof(MsgHdr) == 16) ? 1 : -1];
typedef char ios_direntry_is_44[(sizeof(DirEntry) == 44) ? 1 : -1];
typedef char ios_objinfo_is_64[(sizeof(ObjInfo) == 64) ? 1 : -1];
typedef char ios_openreply_is_24[(sizeof(IOCReply1) == 24) ? 1 : -1];
typedef char ios_config_links_at_48[(offsetof(Config, LinkConf) == 48) ? 1 : -1];
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The Linux I/O Server configuration requires a little-endian host"
#endif
#endif /* IOS_LINUX_HELIOS_H */
