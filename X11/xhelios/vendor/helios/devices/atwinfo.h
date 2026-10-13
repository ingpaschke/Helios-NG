#ifndef __ATW_INFO_H__

#define __ATW_INFO_H__
/*
#include "as_int.h"
#include "NANO_VTG.H"
*/
#define MIT_HW_BLITTER 1

#ifdef MIT_HW_BLITTER

#define ATWBLIT_REGISTER    0x40001040
#define COPY2D_SRC_H              0 /* #40001040 src address high */
#define COPY2D_SRC_L              1 /* #40001044 src address low */
#define COPY2D_DST_H              2 /* #40001048 dst address high */
#define COPY2D_DST_L              3 /* #4000104C dst address low */
#define COPY2D_SRC_STRIDE         4 /* #40001050 src stride 12 bit signed */
#define COPY2D_DST_STRIDE         5 /* #40001054 dst stride 12 bit signed */
#define COPY2D_WIDTH              6 /* #40001058 width 15 bit unsigned */
#define COPY2D_HEIGHT             7 /* #4000105C length 15 bit unsigned */
#define COPY2D_CTRL               8 /* #40001060 control/status bit0=start/status 1=start, 1=running, 0=done, bit1=direction 0=forward 1=reverse */
#define COPY2D_MPAGE              9 /* #40001064 3 bit memory page register */

#endif

unsigned int findAtwInfo(struct nanoxk_atwinfo * atwInfo);
unsigned int findAtwTPInfo(struct nanoxk_atwtp_info  * atwTPInfo);
char checkATWTPInfo(unsigned int atwTPAdr);
char getATWTPStamps(unsigned int atwTPAdr,uint32_t * paintStamp/*,uint32_t * refresh_stamp,uint32_t * paint_check*/);
void incrementPaintStamp(unsigned int atwTPAdr);
/*ret = 1 zeichen ok, 0 fehler, eventuell nach kurzer zeit wiederholen*/
char drawGemWinRects(int gemVidStart,int gemVidStride, unsigned int atwTPAdr, AS_RECT * clip,int repeats);


/* ! Byte swap unsigned short */
uint16_t SWAP_UINT16( uint16_t val ) ;

/* ! Byte swap unsigned int */
uint32_t SWAP_UINT32( uint32_t val );


/* ! Byte swap unsigned short */
int16_t SWAP_INT16( int16_t val ) ;

/* ! Byte swap unsigned int */
int32_t SWAP_INT32( int32_t val );

#ifdef ATW_INFO_IMPLEMENTATION

/* ! Byte swap unsigned short */
uint16_t SWAP_UINT16( uint16_t val ) 
{
    return (val << 8) | (val >> 8 );
}

/* ! Byte swap short */
int16_t SWAP_INT16( int16_t val ) 
{
    return (val << 8) | ((val >> 8) & 0xFF);
}

/* ! Byte swap unsigned int */
uint32_t SWAP_UINT32( uint32_t val )
{
    val = ((val << 8) & 0xFF00FF00 ) | ((val >> 8) & 0xFF00FF ); 
    return (val << 16) | (val >> 16);
}

/* ! Byte swap int */
int32_t SWAP_INT32( int32_t val )
{
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF ); 
    return (val << 16) | ((val >> 16) & 0xFFFF);
}


char getATWTPStamps(unsigned int atwTPAdr,uint32_t * paintStamp/*,uint32_t * refresh_stamp,uint32_t * paint_check*/){
   if(checkATWTPInfo(atwTPAdr)){
      struct nanoxk_atwtp_info * atwTPInfo = (struct nanoxk_atwtp_info *)atwTPAdr;
      *paintStamp    = SWAP_UINT32(atwTPInfo->paint_stamp);

      return 1;
   }  
   return 0;
}
void incrementPaintStamp(unsigned int atwTPAdr){
   if(checkATWTPInfo(atwTPAdr)){
      struct nanoxk_atwtp_info * atwTPInfo = (struct nanoxk_atwtp_info *)atwTPAdr;
      atwTPInfo->paint_stamp = SWAP_UINT32(SWAP_UINT32(atwTPInfo->paint_stamp) + 1);

   }   
}


unsigned int findAtwInfo(struct nanoxk_atwinfo * atwInfo) {
   int i;
   struct nanoxk_atwinfo * infoAdr = (struct nanoxk_atwinfo *)(VRAM_START_ADRESS + NANOxK_ATWINFO);
   

  atwInfo->kennung_a    = SWAP_INT32(infoAdr->kennung_a);       /*[0] 'ATW8'      -> 0x41545738 */
  atwInfo->kennung_b    = SWAP_INT32(infoAdr->kennung_b);       /*[4] 'XVDI'      -> 0x58564449 / 'NVDI' -> 0x4E564449 */
  atwInfo->version      = SWAP_INT32(infoAdr->version);         /*[8] z.B '0100'  -> 0x30313030 */
  atwInfo->version_fpga = SWAP_INT32(infoAdr->version_fpga);    /*[12] z.B '0106' -> 0x30313036 */
  atwInfo->vdi_memstart = SWAP_INT32(infoAdr->vdi_memstart);    /*[16] Startadresse Videomem für VDI */
  atwInfo->vdi_memsize  = SWAP_INT32(infoAdr->vdi_memsize);     /*[20] Gesamtsize Videomemory */
  atwInfo->vdi_vstride  = SWAP_INT16(infoAdr->vdi_vstride);     /*[24] Anzahl bytes je Zeile */
  atwInfo->vdi_bpp      = SWAP_INT16(infoAdr->vdi_bpp);         /*[26] bit per pixel 1(monochrom), 8, 15, 16 */
  atwInfo->vdi_width    = SWAP_INT16(infoAdr->vdi_width);       /*[28] breite, welche VDI nutzt*/
  atwInfo->vdi_height   = SWAP_INT16(infoAdr->vdi_height);      /*[30] h�he, welche VDI nutzt*/
  for(i=0;i<16;i++){
    atwInfo->vtg[i]     = SWAP_INT16(infoAdr->vtg[i]);         /*[32] Schatten-Register VTG, wie sie VDI setzt, dienen f�r Reinit nach nach Transputerprogramm */
  }
  atwInfo->endmagic     = SWAP_INT16(infoAdr->endmagic);        /*[64] beinhaltet 0x4711 Verfahren noch unbestimmt, soll verhindern, das korrupte daten genutzt werden, berechnung über [0] - [65]*/
  atwInfo->reserviert_1 = SWAP_INT16(infoAdr->reserviert_1); 


   if(atwInfo->kennung_a == KENN_ATW8 && (atwInfo->kennung_b == KENN_XVDI || atwInfo->kennung_b == KENN_NVDI )){
      return (unsigned int)infoAdr;
   }


   return 0;
}

unsigned int findAtwTPInfo(struct nanoxk_atwtp_info  * atwTPInfo) {
  int i;
  struct nanoxk_atwtp_info * infoAdr = (struct nanoxk_atwtp_info *)(VRAM_START_ADRESS + NANOxK_ATWINFO + sizeof(struct nanoxk_atwinfo));
   

  atwTPInfo->kennung_a    = SWAP_INT32(infoAdr->kennung_a);       
  atwTPInfo->kennung_b    = SWAP_INT32(infoAdr->kennung_b);       
  


  atwTPInfo->memstart = SWAP_INT32(infoAdr->memstart);    /*Startadresse Videomem */
  atwTPInfo->memsize  = SWAP_INT32(infoAdr->memsize);     /*Gesamtsize Videomemory */
  atwTPInfo->vstride  = SWAP_INT16(infoAdr->vstride);     /*Anzahl bytes je Zeile */
  atwTPInfo->bpp      = SWAP_INT16(infoAdr->bpp);         /*bit per pixel 1(monochrom), 8, 15, 16 */
  atwTPInfo->width    = SWAP_INT16(infoAdr->width);       /*breite, welche tp nutzt*/
  atwTPInfo->height   = SWAP_INT16(infoAdr->height);      /*h�he, welche tp nutzt*/

  atwTPInfo->endmagic     = SWAP_INT16(infoAdr->endmagic);        /*[64] beinhaltet 0x4711 Verfahren noch unbestimmt, soll verhindern, das korrupte daten genutzt werden, berechnung über [0] - [65]*/

  atwTPInfo->paint_stamp    = SWAP_UINT32(infoAdr->paint_stamp);

  atwTPInfo->flag_a = SWAP_INT16(infoAdr->flag_a);
  atwTPInfo->flag_b = SWAP_INT16(infoAdr->flag_b);

  atwTPInfo->res_a    = SWAP_UINT32(infoAdr->res_a);
  atwTPInfo->res_b    = SWAP_UINT32(infoAdr->res_b);
  atwTPInfo->res_c    = SWAP_UINT32(infoAdr->res_c);

  atwTPInfo->drawRectFlags = SWAP_INT16(infoAdr->drawRectFlags);
  atwTPInfo->drawRectCount = infoAdr->drawRectCount;

  atwTPInfo->mouseFlags   = infoAdr->mouseFlags;
  atwTPInfo->mouseX = SWAP_INT16(infoAdr->mouseX);
  atwTPInfo->mouseY = SWAP_INT16(infoAdr->mouseY);  

  atwTPInfo->offScrollX = SWAP_INT16(infoAdr->offScrollX);
  atwTPInfo->offScrollY = SWAP_INT16(infoAdr->offScrollY);  

  atwTPInfo->offDrawX      = SWAP_INT16(infoAdr->offDrawX);                /* Nullpunkt X Gemfenster */
  atwTPInfo->offDrawY      = SWAP_INT16(infoAdr->offDrawY);                /* Nullpunkt Y Gemfenster */
  for(i=0;i < atwTPInfo->drawRectCount && i < MAX_T425_DRAWRECTS;i++){
     atwTPInfo->drawRects[i].x = SWAP_INT16(infoAdr->drawRects[i].x); 
     atwTPInfo->drawRects[i].y = SWAP_INT16(infoAdr->drawRects[i].y); 
     atwTPInfo->drawRects[i].w = SWAP_INT16(infoAdr->drawRects[i].w); 
     atwTPInfo->drawRects[i].h = SWAP_INT16(infoAdr->drawRects[i].h); 

  }
  atwTPInfo->drawRectChangeCounter      = SWAP_INT16(infoAdr->drawRectChangeCounter); 
  if(atwTPInfo->kennung_a == KENN_T425 && atwTPInfo->kennung_b == KENN_2GEM){
      return (unsigned int)infoAdr;
   }
   return 0;
}
char checkATWTPInfo(unsigned int atwTPAdr){
   if(atwTPAdr){
      struct nanoxk_atwtp_info * atwTPInfo = (struct nanoxk_atwtp_info *)atwTPAdr;
      if(SWAP_INT32(atwTPInfo->kennung_a) != KENN_T425)      return 0;
      if(SWAP_INT32(atwTPInfo->kennung_b) != KENN_2GEM)      return 0;  
      if(SWAP_INT16(atwTPInfo->endmagic)  != ENDMAGIC_T425)  return 0;     
      return 1;
   }
   return 0;
}
int rect_intersect( AS_RECT *r1, AS_RECT *r2 )
{
   int x, y, w, h;

   x = MAXVAL( r2->x, r1->x );
   y = MAXVAL( r2->y, r1->y );
   w = MINVAL( r2->x + r2->w, r1->x + r1->w );
   h = MINVAL( r2->y + r2->h, r1->y + r1->h );

   r2->x = x;
   r2->y = y;
   r2->w = w - x;
   r2->h = h - y;

   return ( ((w > x) && (h > y) ) );
}
char is2GemRunning(struct nanoxk_atwtp_info * atwTPInfo){
    if(atwTPInfo)return 0;
    return ((SWAP_INT16(atwTPInfo->drawRectFlags) & ~DRAWRECTFLAGS_RUN) == DRAWRECTFLAGS_RUN);

}
char run2GemOk(struct nanoxk_atwtp_info * atwTPInfo){
   
   if(atwTPInfo==NULL)return 0;
   if( atwTPInfo->drawRectCount < 1) return 0;
   if ((SWAP_INT16(atwTPInfo->drawRectFlags) & DRAWRECTFLAGS_RUN) != DRAWRECTFLAGS_RUN) return 0;

   return 1;

}

char doDrawGemWinRects(int gemVidStart,int gemVidStride, unsigned int atwTPAdr, AS_RECT * clip){
   
   if(checkATWTPInfo(atwTPAdr)){
 
       int i;
       struct nanoxk_atwtp_info * atwTPInfo = (struct nanoxk_atwtp_info *)atwTPAdr;
       int rectCount = atwTPInfo->drawRectCount;
       int offDrawX  = SWAP_INT16(atwTPInfo->offDrawX);
       int offDrawY  = SWAP_INT16(atwTPInfo->offDrawY); 
       int t425VidStart = ((int)SWAP_INT32(atwTPInfo->memstart) + gemVidStart);
       int t425VidStride =  SWAP_INT16(atwTPInfo->vstride);
       int byteperPixel=1; /*todo berechnen */
#ifdef ATV_DEGUG       
       printf("    DrawRin %d\r\n",rectCount);
#endif       
       switch(SWAP_INT16(atwTPInfo->bpp)){
          case 15:
          case 16:
            byteperPixel=2;
            break;
          case 8:
            byteperPixel=1;       
            break;     
          default:
            byteperPixel=1;
            break;
       }
 
       /*
       printf("DrawGem %x %d -> %x %d bperp %d\r\n",t425VidStart,t425VidStride,gemVidStart,gemVidStride,byteperPixel);
       printf("  clip %d %d %d %d \r\n",clip->x,clip->y,clip->w,clip->h);
       */
      for(i=0;i < rectCount && i < MAX_T425_DRAWRECTS;i++){
            AS_RECT r;

            if(!run2GemOk(atwTPInfo)) {
#ifdef ATV_DEGUG               
               printf("    DrawRaus  A\r\n");
#endif               
               return 0;
            }
            r.x = SWAP_INT16(atwTPInfo->drawRects[i].x);
            r.y = SWAP_INT16(atwTPInfo->drawRects[i].y);
            r.w = SWAP_INT16(atwTPInfo->drawRects[i].w);
            r.h = SWAP_INT16(atwTPInfo->drawRects[i].h);

            

            if(rect_intersect(clip,&r)) {   
               int j;
               int zx = offDrawX +r.x;
               int zy = offDrawY +r.y;
               char * qAdr;
               char * zAdr;
#ifdef MIT_HW_BLITTER
               {
                  int * blt_register;
                  int srcA   =  SWAP_INT32(atwTPInfo->memstart) + (t425VidStride * r.y) + (r.x * byteperPixel );
                  int atwAdr = (zy * gemVidStride) +  (zx * byteperPixel);
                  blt_register=(int*)ATWBLIT_REGISTER;
                  /* warten bis frei*/
                  while((blt_register[COPY2D_CTRL] & 0x1) ==1){}      
                  blt_register[COPY2D_SRC_H] = ((srcA >> 16) & 0xFFFF);
                  blt_register[COPY2D_SRC_L] = ((srcA & 0xFFFF));
                  blt_register[COPY2D_SRC_STRIDE] = t425VidStride;

                  /*printf("SRC %x = %x %x %x \r\n",srcA,(srcA >> 16) & 0xFFFF, (srcA & 0xFFFF),AS_AWT_SCREEN_STRIDE);*/

                  blt_register[COPY2D_DST_H] = ((atwAdr >> 16) & 0xFFFF);
                  blt_register[COPY2D_DST_L] = ((atwAdr & 0xFFFF));
                  blt_register[COPY2D_DST_STRIDE] = gemVidStride;
                  /*printf("DST %x = %x %x %x \r\n\r\n",atwAdr,(atwAdr >> 16) & 0xFFFF, (atwAdr & 0xFFFF),AS_AWT_SCREEN_STRIDE);*/

                  blt_register[COPY2D_WIDTH]  = r.w*byteperPixel;
                  blt_register[COPY2D_HEIGHT] = r.h;

                  blt_register[COPY2D_CTRL] = 1;     
                  while((blt_register[COPY2D_CTRL] & 0x1) ==1){}                

               }


#else               
               qAdr = (char*)(t425VidStart + (t425VidStride * r.y) + (r.x * byteperPixel ) );
               zAdr = (char*)((gemVidStart + (zy * gemVidStride) ) +  (zx * byteperPixel) );
#ifdef ATV_DEGUG
               printf("    DrawRect[%d]   \t%d %d %d %d -> %d %d %d %d  0x%x 0x%x\r\n"  , i, r.x,r.y,r.w,r.h,zx,zy,r.w,r.h,qAdr,zAdr);
#endif

               for(j=0;j < r.h;j++){
                  
                  /*if(!run2GemOk(atwTPInfo)) {
#ifdef ATV_DEGUG                     
                    printf("    DrawRaus  B\r\n");
#endif                    
                    return 0;
                  }*/

                  memcpy((char*)zAdr,(char*)qAdr,(r.w*byteperPixel));
                  qAdr += t425VidStride ;
                  zAdr += gemVidStride;
               }
#endif               
            }        
       }
#ifdef ATV_DEGUG       
       printf("    DrawRaus  ok\r\n");
#endif       
       return 1;
   }    
#ifdef ATV_DEGUG   
   printf("    DrawRaus  D\r\n");
#endif   
   return 0;                
}

char drawGemWinRects(int gemVidStart,int gemVidStride, unsigned int atwTPAdr, AS_RECT * clip,int repeats){
   char ret;
#ifdef ATV_DEGUG
   printf("----DrawRect rin\r\n");
#endif
   ret=doDrawGemWinRects( gemVidStart, gemVidStride,  atwTPAdr,  clip);
   
   while(ret==0 && repeats > 0){

      /*ProcWait(1);*/
      /*TODO wait/sleep helios einbauen*/
      Delay(30);
#ifdef ATV_DEGUG    
      printf("+++DrawRect noch mal r:%d repeats:%d\r\n",ret,repeats); 
#endif     
      ret = doDrawGemWinRects( gemVidStart, gemVidStride,  atwTPAdr,  clip);

      repeats--;
   }
#ifdef ATV_DEGUG   
   printf("----DrawRect raus ret r:%d repeats:%d\r\n",ret,repeats);
#endif   
   return ret;
}
  
#endif

#endif
