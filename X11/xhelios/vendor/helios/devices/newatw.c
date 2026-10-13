/*
 * ATW800/2 Driver
 * Andre saischowa, 2024
 *
 * Vorlage: INMOS G300 device driver code
 * Copyright (c) 1990 by Perihelion Software Ltd.
 *
 * All rights reserved
 *
 * Author:  N Clifton
 * Version: 0.2
 * Date: 11/10/90
 */

/*
#define DEBUG
*/

#include <helios.h>
#include <device.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslib.h>

#include "hardware.h"


#define VRAM_START_ADRESS   0x80600000

#define AWTCOLOUR_PALETTE    0x40000000

#define VTG_REGISTER        0x40001000

#include "nano_vtg.h"
#define ATW_INFO_IMPLEMENTATION 1
#include "atwinfo.h"
/*
ATW800/2 Memorymap -- 21.10.2024
#80000070 - #80000FFF internal memory
#80001000 - #80800FFF external memory, configurable
#40000000 - #400003FF RGB LUT
#40000400 - #400007FF FPGA version string
#40001000 - #4000103F Video Timing Generator register

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


int * atw_clutBase=NULL;
char * atw_sharedBase=NULL;

word
DevClose( Hardware_Device * phardware )
/*
 * close down the device driver
 *
 * This function is not (currently) called and do
 * can be left null.  In the future, however, it may
 * be called as part of the shutdown sequence of the
 * X server, in which case it should reset the frame
 * buffer and CLUT and perform any other tidying necessary
 *
 * returns non-zero upon success, 0 upon failure
 */
{
  /* release the memory used by the phardware structure */

#ifdef DEBUG
  IOdebug( "newATW.d: closing frame buffer" );
#endif

  Free( phardware );
  {
    /*CLUT zurückschalten auf Atari-CLUT*/
    int * vtg_register;
    vtg_register=(int*)VTG_REGISTER;
    vtg_register[NANOx_VMemstart_H] = 0;
    vtg_register[NANOx_VMemstart_L] = 0;
    vtg_register[0] = (int)0x11;

  }

  /* finished */

  return 1;

} /* DevClose */

#define COLOR_565(G,B,R)  ((G & 0xFC) << 3) | ((B & 0xF8) >> 3) | ((R & 0xF8) << 8)

void
DevOperate(
  Hardware_Device *  phardware,
  DevReq *     preq )
    /*
 * This function will be called whenever the X server wants to modify
 * any hardware specific run-time parameters.  At the moment, all that
 * this means is that the X server wants to change entries in the colour
 * lookup tables.  (This may change, however, in future releases of the
 * X server).  The 'preq' structure is defined in 'device.h'
 */
{
  /* find out what needs to be done */

  switch (preq->Request)
    {
    case X_INSTALL_COLOURS:
      {
         ColormapReq *  pcmapreq = (ColormapReq *)preq;
         /*
         * install CLUT entries
         *
         * from
         *  (pcmapreq->index)
         * to
         *  (pcmapreq->index + pcmapreq->count - 1) [inclusive]
         *
         * for entry 'i' the colour components to be installed are given
         * by :-
         *  red component:    pcmapreq->rmap[ i - pcmapreq->index ]
         *  green component:  pcmapreq->gmap[ i - pcmapreq->index ]
         *  blue component:      pcmapreq->bmap[ i - pcmapreq->index ]
         *
         * each component has 16 significant bits.  It is expected that the
         * device driver will truncate the compoents to a suitable length
         * before attempting to install them in the CLUT.  For an 8 bit deep
         * CLUT the device driver should impliment a 3:3:2 truncation.
         *
         * for example :-
         *
         * register unsigned int i;
         * register unsigned int entry;
         *
         *
         * for (i = 0, entry = pcmapreq->index;
         *      i < pcmapreq->count;
         *      i++, entry++)
         *   {
         *      *(CLUT_PIXEL_NUMBER)    = entry;
         *      *(CLUT_PIXEL_RGB_VALUE) =
         *           ((pcmapreq->rmap[ i ] >> (16 - 3)) << (0)) |
         *           ((pcmapreq->gmap[ i ] >> (16 - 3)) << (3)) |
         *           ((pcmapreq->bmap[ i ] >> (16 - 2)) << (6)) ;
         *   }
         */
         if(pcmapreq->count>=0 && pcmapreq->count <= 256){
              register unsigned int i;
              register unsigned int entry;
              int * clutbase = (int*)AWTCOLOUR_PALETTE;

                for (i = 0, entry = pcmapreq->index;
                      i < pcmapreq->count;
                      i++, entry++)
                    {
                      int r = (pcmapreq->rmap[ i ] >> 8) & 0xFF;
                      int g = (pcmapreq->gmap[ i ] >> 8) & 0xFF;
                      int b = (pcmapreq->bmap[ i ] >> 8) & 0xFF;
                      int rgb565 = COLOR_565(g,b,r);
                      clutbase[entry] = rgb565;
                    }
         } else{

/*#ifdef DEBUG*/
                IOdebug( "newATW.d: bad number of colormap entries => %d", pcmapreq->count );
/*#endif*/

         }
      }
      break;
    default:
/*#ifdef DEBUG*/
      IOdebug(  "newATW.d: Unknown device request %d", preq->Request );
/*#endif*/
      break;
  }

}

#define get_val( var, default )  \
   ((ptr = (*(pinfo->get_var))( pinfo->pbuffer, var )) == NULL ? \
   (default) : \
   ptr[ 1 ] == 'x' ? \
   strtol( (char *)ptr, (char **)&ptr, 16 ) : \
   atoi( ptr ))


Hardware_Device *
DevOpen(
  Device *  pdev,
  Info * pinfo )
/*
 * This function must do two things :-
 *
 *   Firstly it must initialise the frame buffer
 *
 *   Secondly it must allocate, fill in and return a pointer to a
 *    Hardware_Device structure.
 *
 * The 'pdev' parameter should be ignored.  The 'pinfo' parameter
 * contains two fields, 'pbuffer' which points to a pre-allocated
 * buffer containing the contents of the newXrc file in use by the
 * X server.  This buffer can be scanned using second field of the
 * 'pinfo' structure, which is a function called 'get_var'.
 * eg use :-
 *
 * res = (*(pinfo->get_var))( pinfo->pbuffer, "SCREEN_WIDTH" )
 *
 * to find the (string) value of a SCREEN_WIDTH parameter.
 * The macro get_val() can be used as well.  In this case a variable
 * 'const char * ptr' should be declared, and get_val() should be called
 * with two parameters; the first is the name of the variable to find,
 * the second is the default value for the variable.  eg :-
 *
 * screen_width = get_val( "SCREEN_WIDTH", 1024 );
 *
 * Do not free the pbuffer field.
 */
{
  Hardware_Device *  phardware = Malloc( sizeof( Hardware_Device ) );
  const char *    ptr;
  int     screenoffset;
  int       mono;

  int       width;
  int       height;

  int       vstride;
  int       hstride;
  char    useATWInfo;





#ifdef DEBUG
  IOdebug( "newAWT.d: initialising frame buffer" );
#endif

  /*
   * see if we were able to allocate space for the structure
   */

  if (phardware == NULL)
    {
#ifdef DEBUG
      IOdebug( "newAWT.d: Unable to allocate space for hardware structure" );
#endif
      return NULL;
    }

  /*
   * initialise the DCB - DO NOT EDIT
   */

  width    = (int)get_val( "SCREEN_WIDTH",      1024 );

  height   = (int)get_val( "SCREEN_HEIGHT",     768 );

  vstride  = (int)get_val( "SCREEN_VSTRIDE",    width );
  hstride  = (int)get_val( "SCREEN_HSTRIDE", 1 );


  /* ungenutzt, nur 256 farben*/

  mono  = (int)get_val( "MONOCHROME", 1 );

  if ((ptr = (const char *)*(pinfo->get_var)( pinfo->pbuffer, "MONOCHROME" )) != NULL)
    mono = (strcmp( ptr, "TRUE" ) == 0);
  else
    mono = false;

  /* wenn ATW-Info vorhanden, Parameter benutzen*/
  useATWInfo = (char)get_val( "SCREEN_ATWINFO",     0 );
  screenoffset = (int)get_val( "SCREEN_OFFSET",  0 );

  IOdebug( "====== Screendriver newATW V1.1 Andre Saischowa/2025 ======");

  IOdebug( "------ newxrc-Werte -------");
  IOdebug( "SCREEN_WIDTH     %d", width );
  IOdebug( "SCREEN_HEIGHT    %d", height );
  IOdebug( "SCREEN_VSTRIDE   %d", vstride );
  IOdebug( "SCREEN_HSTRIDE   %d", hstride );
  IOdebug( "SCREEN_ATWINFO   %d", useATWInfo );
  IOdebug( "SCREEN_OFFSET    0x%x", screenoffset );

  /* diese kann die Parameter überschreiben*/

        if(width == vstride){
          /* offset selbst berechnen, mit etwas reserve*/
          /*screenoffset = (vstride * height); */
        }
  if(useATWInfo) {
        struct nanoxk_atwinfo atwInfo;
        unsigned int atwAdr=0;
        atwAdr =  findAtwInfo(&atwInfo);
        if(atwAdr!=0){

          IOdebug( "----- ATW-Info  gefunden -----" );
          IOdebug( "ATW_WIDTH     %d", atwInfo.vdi_width );
          IOdebug( "ATW_HEIGHT    %d", atwInfo.vdi_height );
          IOdebug( "ATW_VSTRIDE   %d", atwInfo.vdi_vstride );
          IOdebug( "ATW_BPP       %d", atwInfo.vdi_bpp );
          mono = false;
          if(atwInfo.vdi_vstride > atwInfo.vdi_width && atwInfo.vdi_bpp==8){
              /* Splitscreen, TP-Screen neben Atariscreen legen*/
              screenoffset = atwInfo.vdi_width;
              width   = atwInfo.vdi_vstride - atwInfo.vdi_width;
              height  = atwInfo.vdi_height;
              vstride = atwInfo.vdi_vstride;

          }else{
              /* kein Splitscreen, TP-Screen hinter atariscreen legen*/
              screenoffset = atwInfo.vdi_vstride * atwInfo.vdi_height;

              if(atwInfo.vdi_bpp==8){
                  int h,w,v;
                  w = atwInfo.vdi_width;
                  h = atwInfo.vdi_height;
                  v = atwInfo.vdi_vstride;

                  /*hoehe verringern, falls es eng wird*/
                  while(((h * v) + screenoffset) > (ATW_MAX_VIDEMEMSIZE - 1024)){
                    h--;
                    h = (h/8) * 8;
                  }
                  width = w;
                  height = h;
                  vstride = v;
              }else if(atwInfo.vdi_bpp==1){
                int h,w,v;
                w = atwInfo.vdi_width;
                h = atwInfo.vdi_height;
                v = (atwInfo.vdi_vstride*8);

                /*hoehe verringern, falls es eng wird*/
                while(((h * v) + screenoffset) > (ATW_MAX_VIDEMEMSIZE - 1024)){
                  h--;
                  h = (h/8) * 8;
                }
                width = w;
                height = h;
                vstride = v;
            }
          }
        }else{
          IOdebug( "ATW-Info nicht gefunden" );
        }
    /*handleAtwInfo(&width,&height,&vstride);*/
  }
  IOdebug( "------ X11-Werte -------");
  IOdebug( "X11_WIDTH     %d", width );
  IOdebug( "X11_HEIGHT    %d", height );
  IOdebug( "X11_VSTRIDE   %d", vstride );
  IOdebug( "X11_HSTRIDE   %d", hstride );

  IOdebug( "X11_OFFSET    0x%x %d", screenoffset,screenoffset );

  phardware->dcb.Device  = pdev;
  phardware->dcb.Operate = DevOperate;
  phardware->dcb.Close   = DevClose;

  phardware->VRAM_base_address        = (unsigned char *)(VRAM_START_ADRESS + screenoffset);
  phardware->VRAM_width               = width;
  phardware->VRAM_height              = height;
  phardware->VRAM_depth               = 8;      /* fixed */
  phardware->VRAM_vertical_stride     = vstride;
  phardware->VRAM_horizontal_stride = hstride;     /* fixed */
  phardware->VRAM_monochrome         = mono;
  phardware->CLUT_depth               = 8;
  phardware->CLUT_length              = 256;
  phardware->pprivate                   = NULL;    /* hang any local varaibles off here */


  {
    /* Bildschirm löschen*/
    int x,y,i;
    int * clutbase = NULL;
    int * vtg_register;
    unsigned char * screenBuf= (unsigned char *)phardware->VRAM_base_address;
    /*erste zeile per Hand*/
    for(x=0;x < phardware->VRAM_width;x++){
         screenBuf[x]=0;
    }
    /*und den rest mit power*/
    for(y=1;y < phardware->VRAM_height;y++){
       memcpy(screenBuf+phardware->VRAM_vertical_stride,screenBuf,phardware->VRAM_width);
       screenBuf += (phardware->VRAM_vertical_stride);
    }

    /* CLUT von Atari übernehmen, oder Graukanal*/
    clutbase = (int*)AWTCOLOUR_PALETTE;
    for(i=0;i < 256;i++){
        int r = i;
        int g = i;
        int b = i;
        int rgb565 = COLOR_565(g,b,r);
        clutbase[i] = rgb565;
    }
    /*Standardwerte Hinterfrund weiss, vordergrund schwarz*/
    clutbase[0] = COLOR_565(255,255,255);
    clutbase[1] = COLOR_565(0,0,0);

    /*CLUT umschalten auf Transputer-CLUT*/
    vtg_register=(int*)VTG_REGISTER;

    if(width == vstride){
      vtg_register[NANOx_VMemstart_H] = ((screenoffset >> 16) & 0xFFFF);
      vtg_register[NANOx_VMemstart_L] = ((screenoffset & 0xFFFF));
    }
    /*vtg_register[0] = 0x19 | 0x40;*/


      vtg_register[NANOx_A20GATE]    = 1; /* auf 2MB gehen */
      vtg_register[NANOx_CTRL] = (int)(0x11 | 0x40);

  }

  /*
   * return filled in hardware structure
   */

#ifdef DEBUG
  IOdebug( "newCGD.d: finished" );
#endif

  return phardware;

} /* DevOpen */
