/*
 *
 * Copyright (c) 1989, 1990, 1991 by Perihelion Software Ltd.
 *
 * All Rights Reserved 
 */

/*
 * this header defines the structures and functions used
 * by the device driver that initilises the video display
 * hardware attached to a given processor
 */

#ifndef HARDWARE_H
#define HARDWARE_H

#ifndef __device_h
#include <device.h>
#endif

/* structure used to pass information to open device */

typedef struct Info
{
	const char * ( *	get_var)( /* const char * pbuffer, const char * pvar */ );
	const char *		pbuffer;

} Info;


/*
 * X requests
 */

#define X_INSTALL_COLOURS	1
#define MAX_X_REQ_ID		2 	/* 1 more than current maximum value for X requests */


typedef struct ColormapReq
{
	DevReq			DevReq;		/* DevReq.Request == X_INSTALL_COLOURS */
	unsigned long		index;		/* pixel value of first pixel to install */
	unsigned int		count;		/* number of (contiguous) pixels to install */
	unsigned short *	rmap;		/* array of red values */
	unsigned short *	gmap;		/* array of green values */
	unsigned short *	bmap;		/* array of blue values */

} ColormapReq;


/*
 * hardware specfic information
 */
 
typedef struct
{
	DCB 			dcb;			/* device control block (Helios system specific) */
	unsigned char *		VRAM_base_address;	/* address of top left hand pixel of displayed screen */
	unsigned long		VRAM_width;		/* width of displayed screen in pixels */
	unsigned long		VRAM_height;		/* height of displayed screen in pixels */
	unsigned long		VRAM_depth;		/* number of bits in one pixel (== 8) */
	signed   long		VRAM_vertical_stride;	/* distance (in bytes) between one screen row and the next row down the screen */
	signed   long		VRAM_horizontal_stride;	/* distance (in bytes) between one screen pixel and the next pixel to the right (== 1) */
	bool			VRAM_monochrome;	/* true iff the display is monochrome in nature */
	unsigned long		CLUT_depth;		/* number of significant bits per entry in CLUT */
	unsigned long		CLUT_length;		/* number of entries in the CLUT */
	void *			pprivate;		/* private pointer for use by device driver */
	
} Hardware_Device;

#endif /* HARDWARE_H */
