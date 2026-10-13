"""Asserted port edits applied only to the disposable build/source copy."""


def replace(root, name, old, new):
    path = root / name
    text = path.read_text()
    if text.count(old) != 1:
        raise RuntimeError('upstream changed: ' + name + ': ' + repr(old[:70]))
    path.write_text(text.replace(old, new))


def prepare(mit):
    # Native input implements software repeat; upstream's device-neutral
    # defaults disable both the global switch and every individual key.
    replace(mit, 'server/include/site.h',
            '#define DEFAULT_AUTOREPEAT\tFALSE',
            '#define DEFAULT_AUTOREPEAT\tTRUE')
    replace(mit, 'server/include/site.h',
            '#define DEFAULT_AUTOREPEATS\t{\\\n' +
            '\t0, 0, 0, 0, 0, 0, 0, 0,\\\n' * 3 +
            '\t0, 0, 0, 0, 0, 0, 0, 0 }',
            '#define DEFAULT_AUTOREPEATS\t{\\\n' +
            '\t255, 255, 255, 255, 255, 255, 255, 255,\\\n' * 3 +
            '\t255, 255, 255, 255, 255, 255, 255, 255 }')
    replace(mit, 'server/dix/dispatch.c',
            'result = (* client->requestVector[MAJOROP])(client);',
            '''{
                        HTRACE("request.begin", client->index, MAJOROP, client->sequence, 0);
                        result = (* client->requestVector[MAJOROP])(client);
                        HTRACE("request.end", result, 0, 0, 0);
                    }''')
    replace(mit, 'server/dix/dispatch.c', '\t    FlushAllOutput();',
            '''            HTRACE("output.begin", 0, 0, 0, 0);
\t    FlushAllOutput();
            HTRACE("output.end", 0, 0, 0, 0);''')
    replace(mit, 'server/dix/window.c',
            '    /* if this is a root window, can\'t be moved */',
            '''    HTRACE("move.begin", pWin->drawable.id, x, y, WasViewable);
    /* if this is a root window, can't be moved */''')
    replace(mit, 'server/dix/window.c',
            '    ResizeChildrenWinSize(pWin, x - oldpt.x, y - oldpt.y, 0, 0);',
            '''    HTRACE("move.children", pWin->drawable.id, x, y, 0);
    ResizeChildrenWinSize(pWin, x - oldpt.x, y - oldpt.y, 0, 0);
    HTRACE("move.mark", pWin->drawable.id, 0, 0, 0);''')
    replace(mit, 'server/dix/window.c',
            '''\t    (* pScreen->ValidateTree)(pParent, NullWindow, kind);
\t    (* pWin->drawable.pScreen->CopyWindow)(pWin, oldpt, oldRegion);
\t    (* pScreen->RegionDestroy)(oldRegion);''',
            '''            HTRACE("move.validate", pWin->drawable.id, 0, 0, 0);
\t    (* pScreen->ValidateTree)(pParent, NullWindow, kind);
            HTRACE("move.copy", pWin->drawable.id, oldpt.x, oldpt.y, 0);
\t    (* pWin->drawable.pScreen->CopyWindow)(pWin, oldpt, oldRegion);
            HTRACE("move.expose", pWin->drawable.id, 0, 0, 0);
\t    (* pScreen->RegionDestroy)(oldRegion);''')
    replace(mit, 'server/dix/window.c',
            '''    if (pWin->realized)
\tWindowsRestructured ();
}

void
GravityTranslate''',
            '''    if (pWin->realized)
    {
        HTRACE("move.restructure", pWin->drawable.id, 0, 0, 0);
\tWindowsRestructured ();
    }
    HTRACE("move.end", pWin->drawable.id, 0, 0, 0);
}

void
GravityTranslate''')
    replace(mit, 'server/dix/events.c',
            '''WindowsRestructured()
{
    (void) CheckMotion((xEvent *)NULL);
}''',
            '''WindowsRestructured()
{
    HTRACE("restructure.begin", 0, 0, 0, 0);
    (void) CheckMotion((xEvent *)NULL);
    HTRACE("restructure.end", 0, 0, 0, 0);
}''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '''    pScreenPriv->isUp = FALSE;
    if (!(*pScreenPriv->funcs->RestoreUnderCursor)''',
            '''    HTRACE("cursor.remove.begin", pScreenPriv->saved.x1,
           pScreenPriv->saved.y1, pScreenPriv->saved.x2, pScreenPriv->saved.y2);
    pScreenPriv->isUp = FALSE;
    if (!(*pScreenPriv->funcs->RestoreUnderCursor)''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '''\tpScreenPriv->isUp = TRUE;
    }
}

/*
 * Called from the block handler''',
            '''\tpScreenPriv->isUp = TRUE;
    }
    HTRACE("cursor.remove.end", pScreenPriv->isUp, 0, 0, 0);
}

/*
 * Called from the block handler''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '    miSpriteComputeSaved (pScreen);',
            '''    HTRACE("cursor.restore.begin", 0, 0, 0, 0);
    miSpriteComputeSaved (pScreen);''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '''    if ((*pScreenPriv->funcs->SaveUnderCursor) (pScreen,''',
            '''    HTRACE("cursor.save.begin", pScreenPriv->saved.x1,
           pScreenPriv->saved.y1, pScreenPriv->saved.x2, pScreenPriv->saved.y2);
    if ((*pScreenPriv->funcs->SaveUnderCursor) (pScreen,''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '''\tif (pScreenPriv->checkPixels)
\t    miSpriteFindColors (pScreen);
\tif ((*pScreenPriv->funcs->PutUpCursor)''',
            '''        HTRACE("cursor.save.end", 0, 0, 0, 0);
\tif (pScreenPriv->checkPixels)
\t    miSpriteFindColors (pScreen);
        HTRACE("cursor.paint.begin", x, y, pCursor->bits->width, pCursor->bits->height);
\tif ((*pScreenPriv->funcs->PutUpCursor)''')
    replace(mit, 'server/ddx/mi/misprite.c',
            '''\t    pScreenPriv->isUp = TRUE;
    }
}

/*
 * compute the desired area''',
            '''\t    pScreenPriv->isUp = TRUE;
        HTRACE("cursor.paint.end", pScreenPriv->isUp, 0, 0, 0);
    }
    HTRACE("cursor.restore.end", pScreenPriv->isUp, 0, 0, 0);
}

/*
 * compute the desired area''')
    replace(mit, 'server/os/4.2bsd/WaitFor.c',
            '\t/* keep this check close to select() call to minimize race */',
            '''        HTRACE("select.begin", LastSelectMask[0], LastSelectMask[1], 0, 0);
\t/* keep this check close to select() call to minimize race */''')
    replace(mit, 'server/os/4.2bsd/WaitFor.c',
            '\tselecterr = errno;',
            '''\tselecterr = errno;
        HTRACE("select.end", i, selecterr, 0, 0);''')
    # COPY2D consumes rectangles AFTER upstream CFB has ordered their bands
    # and source points for overlap. All non-screen/non-GXcopy cases stay CFB.
    replace(mit, 'server/ddx/cfb/cfbbitblt.c',
            '#include\t"fastblt.h"', '#include\t"fastblt.h"\n#include "atw_copy2d.h"')
    replace(mit, 'server/ddx/cfb/cfbbitblt.c',
            '    int tmpSrc;\n', '    unsigned int tmpSrc;\n    int atwResult;\n')
    replace(mit, 'server/ddx/cfb/cfbbitblt.c',
            '''    /* special case copy */
    if (alu == GXcopy && (planemask & PMSK) == PMSK)
    {
\twhile(nbox--)
\t{
\t    w = pbox->x2 - pbox->x1;
\t    h = pbox->y2 - pbox->y1;''',
            '''    /* special case copy */
    if (alu == GXcopy && (planemask & PMSK) == PMSK)
    {
\twhile(nbox--)
\t{
\t    w = pbox->x2 - pbox->x1;
\t    h = pbox->y2 - pbox->y1;
            if (heliosAtwEnabled) {
                atwResult = heliosAtwCopy(psrcBase, pdstBase, pptSrc->x, pptSrc->y,
                                         pbox->x1, pbox->y1, w, h);
                if (atwResult < 0)
                    FatalError("ATW COPY2D timeout: cannot safely replay copy");
                if (atwResult) { ++pbox; ++pptSrc; continue; }
            }''')
    # Solid boxes are already clipped by PolyFillRect / PaintWindow.
    # cfbValidateGC selects this path for GXcopy only with full planes;
    # its XOR/invert variant has isCopy false. Pixmaps remain CPU-rendered.
    replace(mit, 'server/ddx/cfb/cfbfillrct.c',
            '#include "cfbmskbits.h"', '#include "cfbmskbits.h"\n#include "atw_copy2d.h"')
    replace(mit, 'server/ddx/cfb/cfbfillrct.c',
            '    int\t\t    w;\n', '    int\t\t    w;\n    int atwResult;\n')
    replace(mit, 'server/ddx/cfb/cfbfillrct.c',
            '\tw = pBox->x2 - pBox->x1;\n#if PPW == 4',
            '''\tw = pBox->x2 - pBox->x1;
        if (heliosAtwEnabled && isCopy && pDrawable->type == DRAWABLE_WINDOW) {
            atwResult = heliosAtwFill(pdstBase, pBox->x1, pBox->y1, w, h, pixel);
            if (atwResult < 0)
                FatalError("ATW COPY2D timeout: cannot safely replay fill");
            if (atwResult) continue;
        }
#if PPW == 4''')
    # Padded/rotated 32-bit tiles are four pixels wide, with origin zero.
    # General tiles retain their original width, stride and screen origin.
    replace(mit, 'server/ddx/cfb/cfbfillrct.c',
            '    int srcy;\t\t/* current tile position */',
            '    int srcy;\t\t/* current tile position */\n    int atwResult;')
    replace(mit, 'server/ddx/cfb/cfbfillrct.c',
            '\th = pBox->y2 - pBox->y1;\n\ty = pBox->y1;',
            '''\th = pBox->y2 - pBox->y1;
        if (heliosAtwEnabled && pDrawable->type == DRAWABLE_WINDOW) {
            atwResult = heliosAtwTile(pbits, pBox->x1, pBox->y1, w, h,
                (unsigned char *)psrc, 4, tileHeight, 4, 0, 0);
            if (atwResult < 0)
                FatalError("ATW COPY2D timeout: cannot safely replay tile");
            if (atwResult) { ++pBox; continue; }
        }
\ty = pBox->y1;''')
    replace(mit, 'server/ddx/cfb/cfbtileodd.c',
            '#include "cfbmskbits.h"', '#include "cfbmskbits.h"\n#include "atw_copy2d.h"')
    replace(mit, 'server/ddx/cfb/cfbtileodd.c',
            '    int tileWidth;\t/* width of tile in pixels */',
            '    int atwResult;\n    int tileWidth;\t/* width of tile in pixels */')
    replace(mit, 'server/ddx/cfb/cfbtileodd.c',
            '\tw = pBox->x2 - pBox->x1;\n\th = pBox->y2 - pBox->y1;',
            '''\tw = pBox->x2 - pBox->x1;
\th = pBox->y2 - pBox->y1;
        if (heliosAtwEnabled && pDrawable->type == DRAWABLE_WINDOW) {
            atwResult = heliosAtwTile(pDstBase, pBox->x1, pBox->y1, w, h,
                (unsigned char *)tile->devPrivate.ptr, tile->drawable.width,
                tile->drawable.height, tile->devKind, xrot, yrot);
            if (atwResult < 0)
                FatalError("ATW COPY2D timeout: cannot safely replay tile");
            if (atwResult) { ++pBox; continue; }
        }''')
    # Large 8-bit pixmaps can use an explicitly reserved offscreen pool.
    # CFB's header stays in normal RAM. Other pixmaps keep its contiguous
    # header+pixels allocation, and reference-counted destruction is unchanged.
    replace(mit, 'server/ddx/cfb/cfbpixmap.c',
            '#include "cfb.h"', '#include "cfb.h"\n#include "atw_copy2d.h"')
    replace(mit, 'server/ddx/cfb/cfbpixmap.c',
            '    register PixmapPtr pPixmap;\n    int size;',
            '    register PixmapPtr pPixmap;\n    int size;\n    void *atwPixels;')
    replace(mit, 'server/ddx/cfb/cfbpixmap.c',
            '''    pPixmap = (PixmapPtr)xalloc(sizeof(PixmapRec) + (height * size));
    if (!pPixmap)
\treturn NullPixmap;''',
            '''    atwPixels = heliosAtwPixmapAlloc(width, height, size, depth);
    pPixmap = (PixmapPtr)xalloc(sizeof(PixmapRec) + (atwPixels ? 0 : height * size));
    if (!pPixmap) {
        heliosAtwPixmapFree(atwPixels);
\treturn NullPixmap;
    }''')
    replace(mit, 'server/ddx/cfb/cfbpixmap.c',
            '    pPixmap->devPrivate.ptr = (pointer)(pPixmap + 1);',
            '    pPixmap->devPrivate.ptr = atwPixels ? (pointer)atwPixels : (pointer)(pPixmap + 1);')
    replace(mit, 'server/ddx/cfb/cfbpixmap.c',
            '    xfree(pPixmap);',
            '    heliosAtwPixmapFree(pPixmap->devPrivate.ptr);\n    xfree(pPixmap);')
    # Retain empty-region handling without bypassing the upstream word blitter.
    # Its overlap-direction check otherwise dereferences an absent source point.
    replace(mit, 'server/ddx/cfb/cfbbitblt.c',
            '    nbox = REGION_NUM_RECTS(prgnDst);\n\n    pboxNew1 = NULL;',
            '''    nbox = REGION_NUM_RECTS(prgnDst);
    if (!nbox) return;

    pboxNew1 = NULL;''')
    # PFILL replicates this mask into the high byte of a 32-bit word.
    # Keep those shifts unsigned (0xff << 24 is undefined for signed int).
    replace(mit, 'server/ddx/cfb/cfbmskbits.h', '#define PMSK\t0xFF', '#define PMSK\t0xFFU')
    replace(mit, 'server/os/4.2bsd/connection.c', 'char *display;', 'extern char *display;')
    replace(mit, 'server/ddx/mi/mispritest.h',
            '#define LINE_SORT(x1,y2,x2,y2)', '#define LINE_SORT(x1,y1,x2,y2)')
    replace(mit, 'server/os/4.2bsd/connection.c', 'FNDELAY);', 'O_NONBLOCK);')
    for name in ['AuLock.c', 'AuUnlock.c']:
        replace(mit, 'lib/Xau/' + name, '<sys/errno.h>', '<errno.h>')
    # Native Helios sockets are namespace objects named X0, X1, etc., not
    # filesystem socket nodes. This matches MakeHeliosSocketConnection in lib/x.
    replace(mit, 'server/os/4.2bsd/connection.c',
            '    unlink (unsock.sun_path);', '    /* bind owns the namespace entry */')
    # MarshalAddr in historical SysLib does not consistently include a trailing
    # NUL for the short BSD length. The original Xhelios passes all 112 bytes.
    replace(mit, 'server/os/4.2bsd/connection.c',
            'strlen(unsock.sun_path)+2)', 'sizeof(unsock))')
    replace(mit, 'server/os/4.2bsd/connection.c',
            '#ifdef UNIXCONN\n    if (unixDomainConnection != -1)',
            '#if defined(UNIXCONN) && !defined(helios)\n    if (unixDomainConnection != -1)')
    # The installed DOS filesystem has the 8.3 spelling of fonts.alias. Try
    # that first: on the ATW800 the I/O Server hands names to GEMDOS, where
    # the long name fails with an error other than ENOENT, and X11R4 then
    # rejected the whole font directory ("could not open default font").
    # A missing alias file means no aliases, as in X11R4.
    replace(mit, 'server/os/4.2bsd/osfonts.c',
            '''    file = fopen(alias_file, "r");
    if (!file)
	return ((errno == ENOENT) ? Success : BadValue);''',
            '''    if (!isFile) {
        char *dot = strrchr(alias_file, '.');
        alias_file[strlen(alias_file) - 2] = 0; /* fonts.alias -> fonts.ali */
        file = fopen(alias_file, "r");
        if (!file && dot) {
            strcpy(dot, ".alias");
            file = fopen(alias_file, "r");
        }
        if (!file)
            return Success;
    } else
        file = fopen(alias_file, "r");
    if (!file)
	return ((errno == ENOENT) ? Success : BadValue);''')
    # Font files and the authorization file are binary. Helios stdio treats a
    # text-mode stream on an I/O Server that marks files Flags_MSdos (the PC
    # and Atari ST servers, unless Unix_fileio is configured) as an MS-DOS
    # file: it drops a CR before LF and stops at the first 0x1A byte.
    # 6x13.snf has one at offset 93, so on the ATW800 every SNF load failed
    # ("could not open default font 'fixed'"). The Linux I/O Server does not
    # set the flag, which is why t4 never showed it.
    replace(mit, 'server/os/4.2bsd/osfonts.c',
            'if ((fp = fopen (pathName, "r")) == (FILE *)NULL)',
            'if ((fp = fopen (pathName, "rb")) == (FILE *)NULL)')
    replace(mit, 'server/os/4.2bsd/auth.c',
            'f = fopen (authorization_file, "r");',
            'f = fopen (authorization_file, "rb");')
    # Helios uses EAGAIN for zero-timeout pipe reads/writes. BSD distinguishes
    # it numerically from EWOULDBLOCK in the installed headers.
    path = mit / 'server/os/4.2bsd/io.c'
    text = path.read_text()
    if text.count('errno == EWOULDBLOCK') != 3:
        raise RuntimeError('upstream changed: nonblocking I/O checks')
    path.write_text(text.replace('errno == EWOULDBLOCK',
                                '(errno == EWOULDBLOCK || errno == EAGAIN)'))
    # Multi-Buffering (X11R4 extensions/server/multibuf.c) has two latent bugs
    # that the old compilers hid. Window moves took the vertical offset from x,
    # and selecting buffer events for the buffer's owner fell off the end of
    # an int function whose result SetBufferAttributes checks against Success.
    replace(mit, 'extensions/server/multibuf.c',
            'dy = pWin->drawable.x - pMultibuffers->y;',
            'dy = pWin->drawable.y - pMultibuffers->y;')
    replace(mit, 'extensions/server/multibuf.c',
            '''	RecalculateMultibufferOtherEvents (pMultibuffer);
    }
}''', '''	RecalculateMultibufferOtherEvents (pMultibuffer);
    }
    return Success;
}''')
    # MIT-SHM without System V IPC: the ddx offers one region (VRAM with
    # COPY2D) whose address is the shmid. Shared images and pixmaps in
    # VRAM become COPY2D surfaces.
    shm = 'extensions/server/shm.c'
    replace(mit, shm, '#include <sys/ipc.h>\n#include <sys/shm.h>\n', '#include "atw_copy2d.h"\n')
    replace(mit, shm, 'char *shmat();', 'extern char *heliosShmAttach();')
    replace(mit, shm, '    struct shmid_ds buf;\n    ShmDescPtr shmdesc;\n    REQUEST(xShmAttachReq);',
            '    ShmDescPtr shmdesc;\n    REQUEST(xShmAttachReq);')
    replace(mit, shm, '''	shmdesc->addr = shmat(stuff->shmid, 0,
			      stuff->readOnly ? SHM_RDONLY : 0);
	if ((shmdesc->addr == ((char *)-1)) ||
	    shmctl(stuff->shmid, IPC_STAT, &buf))
	{''', '''	shmdesc->addr = heliosShmAttach(stuff->shmid, &shmdesc->size);
	if (!shmdesc->addr)
	{''')
    replace(mit, shm, '\tshmdesc->size = buf.shm_segsz;\n', '')
    replace(mit, shm, '    shmdt(shmdesc->addr);\n', '')
    replace(mit, shm, '''	else
	    (void)(*pGC->ops->CopyArea)(&FakePixmap, dst, pGC,
					sx, sy, sw, sh, dx, dy);''', '''	else
	{
	    int atw = depth == 8 &&
		heliosAtwSurfaceAdd(data, w, h, (int)FakePixmap.devKind);
	    (void)(*pGC->ops->CopyArea)(&FakePixmap, dst, pGC,
					sx, sy, sw, sh, dx, dy);
	    if (atw)
		heliosAtwPixmapFree(data);
	}''')
    # cfbDestroyPixmap releases the surface with heliosAtwPixmapFree.
    replace(mit, shm, '    pPixmap->devPrivate.ptr = (pointer)addr;\n',
            '    pPixmap->devPrivate.ptr = (pointer)addr;\n'
            '    if (depth == 8)\n'
            '\theliosAtwSurfaceAdd(addr, width, height, (int)pPixmap->devKind);\n')
