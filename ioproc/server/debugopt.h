/*------------------------------------------------------------------------
--                                                                      --
--          H E L I O S   I N P U T / O U T P U T   S E R V E R         --
--          ---------------------------------------------------         --
--                                                                      --
--             Copyright (C) 1987, Perihelion Software Ltd.             --
--                        All Rights Reserved.                          --
--                                                                      --
--      Debugopt.h                                                      --
--                                                                      --
--  Author:  BLV 10/12/87                                               --
--                                                                      --
------------------------------------------------------------------------*/
/* RcsId: $Id: debugopt.h,v 1.3 1993/08/12 14:24:15 bart Exp $ */
/* Copyright (C) 1987, Perihelion Software Ltd.   			*/

#ifndef Daemon_Module

/**
*** This header file is used to control the debugging options available in
*** the Server. There is a set of flags, debugflags, and a debugging option
*** is on when the corresponding bit is set in debugflags. The various flags
*** are declared. To keep track of all the debugging options I use a structure
*** debug_options, each option consisting of a character and the corresponding
*** flag, and I have an array of these options.
***
*** For example, when ctrl-shift M is pressed to toggle the message debugging
*** flag the system looks down options_list to find an entry with the letter
*** 'm'. It finds this, and xors debugflags with the corresponding flag
*** Message_Flag.
**/

typedef struct debug_options {
         int    flagchar;
         word   flag;
         char   *name;
} debug_options;

#if LINUX
#define Message_Flag                 0x1
#define Search_Flag                  0x2
#define Open_Flag                    0x4
#define Name_Flag                    0x8
#define Read_Flag                   0x10
#define Boot_Flag                   0x20
#define Memory_Flag                 0x40
#define Keyboard_Flag               0x80
#define Init_Flag                  0x100
#define Com_Flag                   0x200
#define Write_Flag                 0x400
#define Quit_Flag                  0x800
#define Close_Flag                0x1000
#define HardDisk_Flag             0x2000
#define Log_Flag                  0x4000
#define Graphics_Flag             0x8000
#define Reconfigure_Flag         0x10000
#define Timeout_Flag             0x20000
#define OpenReply_Flag           0x40000
#define FileIO_Flag              0x80000
#define Delete_Flag             0x100000
#define Directory_Flag          0x200000
#define Nopop_Flag              0x400000
#define ListAll_Flag            0x800000
#define Error_Flag             0x1000000
#define DDE_Flag               0x2000000
#else
#define Message_Flag                 0x1L
#define Search_Flag                  0x2L
#define Open_Flag                    0x4L
#define Name_Flag                    0x8L
#define Read_Flag                   0x10L
#define Boot_Flag                   0x20L
#define Memory_Flag                 0x40L
#define Keyboard_Flag               0x80L
#define Init_Flag                  0x100L
#define Com_Flag                   0x200L
#define Write_Flag                 0x400L
#define Quit_Flag                  0x800L
#define Close_Flag                0x1000L
#define HardDisk_Flag             0x2000L
#define Log_Flag                  0x4000L
#define Graphics_Flag             0x8000L
#define Reconfigure_Flag         0x10000L
#define Timeout_Flag             0x20000L
#define OpenReply_Flag           0x40000L
#define FileIO_Flag              0x80000L
#define Delete_Flag             0x100000L
#define Directory_Flag          0x200000L
#define Nopop_Flag              0x400000L
#define ListAll_Flag            0x800000L
#define Error_Flag             0x1000000L
#define DDE_Flag               0x2000000L
#endif /* LINUX */
/**
*** All_Debug_Flags is a mask for all the debugging options except the
*** one-off ones : memory, log, reconfigure, nopop, listall.
*** It is used for -a etc.
**/
#if LINUX
#define All_Debug_Flags       0x033EBFBF
#else
#define All_Debug_Flags       0x033EBFBFL
#endif /* LINUX */
              
#define Log_to_screen           1
#define Log_to_file             2
#define Log_to_both             3

#ifdef Server_Module
word debugflags;
int  log_dest = Log_to_screen;
debug_options options_list[] = 
                               {
             /* 'a' == all */
             { 'b', Boot_Flag,        "boot"        },
             { 'c', Com_Flag,         "serial"      },
             { 'd', Delete_Flag,      "delete"      },
             { 'e', Error_Flag,       "errors"      },
             { 'f', FileIO_Flag,      "file I/O"    },
             { 'g', Graphics_Flag,    "graphics"    },
             { 'h', HardDisk_Flag,    "raw disk"    },
             { 'i', Init_Flag,        "init"        },
             { 'j', Directory_Flag,   "directory"   },
             { 'k', Keyboard_Flag,    "keyboard"    },
             { 'l', Log_Flag,         "logger"      },
             { 'm', Message_Flag,     "messages"    }, 
             { 'n', Name_Flag,        "names"       },
             { 'o', Open_Flag,        "open"        },
             { 'p', Close_Flag,       "close"       },
             { 'q', Quit_Flag,        "exit"        },
             { 'r', Read_Flag,        "read"        },
             { 's', Search_Flag,      "search"      },
             { 't', Timeout_Flag,     "timeouts"    },
             { 'u', Nopop_Flag,       "nopop"       },
             { 'v', OpenReply_Flag,   "open reply"  },
             { 'w', Write_Flag,       "write"       },
             { 'x', Memory_Flag,      "resources"   },
             { 'y', ListAll_Flag,     "list"        },
             { 'z', Reconfigure_Flag, "reconfigure" },
             { '\0', 0L              } };

#endif
#ifndef Server_Module
extern word debugflags;
extern int  log_dest;
extern debug_options options_list[];
#endif

/**
*** Where the bootstrap program is loaded, assumes T800 but not really
*** important
**/
#ifndef MemStart
#define MemStart  MinInt+0x70
#endif

/**
*** arguments to boot_transputer() in module tload.c
**/
#define debugboot       1
#define serverboot      2

/**
*** If the I/O Server is compiled with -DSMALL, most of the debugging
*** options disappear
**/
#ifdef SMALL
#define Debug(a, b)
#else
#define Debug(a,b) if (debugflags & a) ServerDebug b
#endif /* SMALL */

#endif  /* Daemon module */
