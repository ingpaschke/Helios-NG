#ifndef __NANO_VTG_H__

#define __NANO_VTG_H__


#ifndef int32_t
#define int32_t long
#endif

#ifndef uint32_t
#define uint32_t unsigned long
#endif

#ifndef int16_t
#define int16_t short
#endif

#ifndef uint16_t
#define uint16_t unsigned short
#endif

#ifndef MAXVAL
#define MAXVAL(x,y) ( (x) > (y) ? (x) : (y) )
#endif

#ifndef MINVAL
#define MINVAL(x,y) ( (x) < (y) ? (x) : (y) )
#endif
/*
Register Video Timing Generator (VTG):

C/DFF800 Control Register :

bit 0 = enable VTG
bit 1 = posHsync
bit 2 = posVsync
bit 3 = transition 0 to 1 transfers PLL registers
bit 4 und 5 = Video Mode, see below
bit 6 = RGB LUT select 0 = Atari, 1 = Transputer

C/DFF802 HFPreg : 8 bit horizontal front porch
C/DFF804 HSYreg : 8 bit horizontal sync
C/DFF806 HBPreg : 8 bit horizontal back porch
C/DFF808 HDIreg : 12 bit horizontal display
C/DFF80A VFPreg : 8 bit vertical front porch
C/DFF80C VSYreg : 8 bit vertical sync
C/DFF80E VBPreg : 8 bit vertical back porch
C/DFF810 VDIreg : 12 bit vertical display
C/DFF812 PLLfbdsel : video PLL value for feedback divider, register value = real value -1
C/DFF814 PLLidsel  : video PLL value for input divider, register value = real value -1
C/DFF816 PLLodsel  : video PLL value for output divider, register value = real value
C/DFF818 VidMemStartAddress : video memory start address lower 16 bits
C/DFF81A VidMemStartAddress : video memory start address upper 7 bits
C/DFF81E A20Gate : 0 = 1MB address space, 1 = 2 MB address space, all Cxxxxx IO addresses remap to Dxxxxx
*/

#define ATW_MAX_VIDEMEMSIZE 0x1F4000L

#define NANOxK_SHARED       (0x1F4000L) /* 0x1F4000 - 0x1FDBFF   -  39k shared Mem Atari<>Transputer */
#define NANOxK_ATWINFO      (0x1FDC00L) /* 0x1FDC00 - 0x1FFDFF   -  512 NANOVGA_CONF */
#define NANOxK_CLUTVALS     (0x1FDE00L) /* 0x1FDE00 - 0x1FDFFF   -  512 CLUT Shadow */
#define NANOxK_VDIBUF       (0x1FE000L) /* 0x1FE000 - 0x1FFFFF   -  8k Bereich für Buffer, Patterncaches u.ä. XVDI/NVDI*/

#define NANOx_CTRL			0  /* bit 5 und 4 = Video Mode, bit 3 = transition 0 to 1 transfers PLL registers, bit 2 = posVsync, bit 1 = posHsync, bit 0 = enable VTG */
#define NANOx_HFPreg		1  /* 8 bit horizontal front porch */
#define NANOx_HSYreg		2  /* 8 bit horizontal sync */
#define NANOx_HBPreg		3  /* 8 bit horizontal back porch */
#define NANOx_HDIreg		4  /* 12 bit horizontal display */
#define NANOx_VFPreg		5  /* 8 bit vertical front porch */
#define NANOx_VSYreg		6  /* 8 bit vertical sync */
#define NANOx_VBPreg		7  /* 8 bit vertical back porch */
#define NANOx_VDIreg		8  /* 12 bit vertical display */
#define NANOx_PLLfbdsel	    9  /* video PLL value for feedback divider, register value = real value -1 */
#define NANOx_PLLidsel		10  /* video PLL value for input divider, register value = real value -1 */
#define NANOx_PLLodsel		11  /* video PLL value for output divider, register value = real value */
#define NANOx_VMemstart_L	12  /* video memstart low 16 Bit */
#define NANOx_VMemstart_H	13  /* video memstart hight 7 Bit */
#define NANOx_RESERVE3		14
#define NANOx_A20GATE		15  /* 0 = 1MB Adressspace, 1 = 2 MB */

/* wird von xvdi/nvdi bzw AtariScreen-Servber erzeugt, dar�ber kann Transputer 
 aktuelle Werte �ber Sreenaufl�sung  bekommen  es wird in BIGENDIAN abgelegt, [x] gibt byteoffsets an
*/

#define KENN_ATW8 0x41545738L
#define KENN_XVDI 0x58564449
#define KENN_NVDI 0x4E564449
#define ENDMAGIC_ATW8 0x4711

#define KENN_T425 0x54343235L
#define KENN_2GEM 0x3247454DL
#define ENDMAGIC_T425 0x1147


struct nanoxk_atwinfo{
  int32_t kennung_a;       /*[0] 'ATW8'      -> 0x41545738 */
  int32_t kennung_b;       /*[4] 'XVDI'      -> 0x58564449 / 'NVDI' -> 0x4E564449 */
  int32_t version;         /*[8] z.B '0100'  -> 0x30313030 */
  int32_t version_fpga;    /*[12] z.B '0106' -> 0x30313036 */
  int32_t vdi_memstart;    /*[16] Startadresse Videomem für VDI */
  int32_t vdi_memsize;     /*[20] Gesamtsize Videomemory */
  int16_t  vdi_vstride;     /*[24] Anzahl bytes je Zeile */
  int16_t  vdi_bpp;         /*[26] bit per pixel 1(monochrom), 8, 15, 16 */
  int16_t  vdi_width;       /*[28] breite, welche VDI nutzt*/
  int16_t  vdi_height;      /*[30] h�he, welche VDI nutzt*/
  int16_t  vtg[16];         /*[32] Schatten-Register VTG, wie sie VDI setzt, dienen f�r Reinit nach nach Transputerprogramm */
  int16_t  endmagic;        /*[64] beinhaltet 0x4711 Verfahren noch unbestimmt, soll verhindern, das korrupte daten genutzt werden*/
  int16_t  reserviert_1;    /*[66] byte-stuffing*/  
/* rest vorerst reserviert , bis Byte 511*/
 };

typedef struct
  {
    int16_t         x;
    int16_t         y;
    int16_t         w;
    int16_t         h;
  } AS_RECT;

#define MAX_T425_DRAWRECTS 12
#define DRAWRECTFLAGS_RUN        0x0001 /* 1 Running, 0 Not Running*/
#define DRAWRECTFLAGS_CALC       0x0002 /* 1 neuberechnung läuft, 0 Not keine neuberechnung läuft*/
#define DRAWRECTFLAGS_WINTOP     0x0004 /* 1 Ausgabewindow ist Top, 0 Ausgabewindow nicht Top*/
#define DRAWRECTFLAGS_WINVISIBLE 0x0008 /* 1 Ausgabewindow Visible, 0 Ausgabewindow nicht Visible*/

#define MOUSEFLAGS_LEFT_PRESS     0x01;
#define MOUSEFLAGS_RIGHT_PRESS    0x02;

struct nanoxk_atwtp_info{
  int32_t   kennung_a;              /*[0] 'T425'      -> 0x54343235 */
  int32_t   kennung_b;              /*[4] '2GEM'      -> 0x3247454D */
  int32_t   memstart;               /*[8]  Offset Startadresse Videomem für TP */
  int32_t   memsize;                /*[12] Gesamtsize Videomemory */
  int16_t   vstride;                /*[16] Anzahl bytes je Zeile */
  int16_t   bpp;                    /*[18] bit per pixel 1(monochrom), 8, 15, 16 */
  int16_t   width;                  /*[20] breite, welche TP-Video nutzt*/
  int16_t   height;                 /*[22] h�he, welche TP-Video nutzt*/
  volatile uint32_t  paint_stamp;   /*[24] ??? wird von TP gesetzt, wenn neuer Frame gezeichnet wurde, bigendian*/
  volatile int16_t   flag_a;        /*[36] muss 16 Bit damit atomarer Zugriff von 68000*/
  volatile int16_t   flag_b;        /*[38] muss 16 Bit damit atomarer Zugriff von 68000*/
  volatile uint32_t  res_a;         /*[40]*/
  volatile uint32_t  res_b;         /*[44]*/
  volatile uint32_t  res_c;         /*[48]*/
  /*------------------------*/

  char     drawRectCount;
  char     mouseFlags;
  int16_t  mouseX;                 /* Mouspos X */
  int16_t  mouseY;                 /* Mouspos Y */
  int16_t  offDrawX;               /* Nullpunkt X Gemfenster */
  int16_t  offDrawY;               /* Nullpunkt Y Gemfenster */
  int16_t  offScrollX;             /* Scrolloffset X */
  int16_t  offScrollY;             /* Scrolloffset Y */
  int16_t  drawRectFlags;
  AS_RECT  drawRects[MAX_T425_DRAWRECTS];   /*maximal 12 Gemfenster können benutzt werden, auf TP-Videoram bezogen*/
  int16_t  drawRectChangeCounter;        /* wird bei jeder Änbderung incrementiert, nur 16Bit wegen atomaren Zugriff von 68000*/
  /* int16_t  reserviert_1;            byte-stuffing*/  
  int16_t  endmagic;               /* beinhaltet 0x1147 Verfahren noch unbestimmt, soll verhindern, das korrupte daten genutzt werden*/

 };  

#endif