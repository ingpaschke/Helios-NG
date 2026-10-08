/* $Id: vm.h,v 1.3 1991/03/25 11:17:45 nick Exp $ */


#define VMmask		0xf
#define VMswapped	1
#define VMdirty		2
#define VMtouched	8

#define VMagebit	0x40000000

typedef struct VMpage {
	int		status;
	char		*addr;
	int		filepage;
	int		left;
	int		locks;
	int		age;
	int		swapsin;
	int		swapsout;
} VMpage;

#ifdef __STDC__

typedef struct VMRef {
	unsigned 	offset : 16;
	unsigned	page   : 16;
} VMRef;

#define offset_(x)	((x).offset)
#define page_(x)	((x).page)

/* Host port: a VMRef as the 32-bit number page<<16|offset, for	*/
/* printing with %x (a struct cannot be passed to printf)		*/
#define VMWord(x)	(((unsigned)page_(x) << 16) | (unsigned)offset_(x))

#define VMSame(x,y) ((page_(x) == page_(y)) && (offset_(x) == offset_(y)))

#define MakeVMRef(r,p,o) ((r).page = p, (r).offset = o)

#else

typedef long	VMRef;

#define offset_(x)	((x)&0xffff)
#define page_(x)	((x)>>16)

#define VMSame(x,y)	((x) == (y))

#define MakeVMRef(r,p,o) ((r) = ((p)<<16) | (o))

#endif

#ifdef VM_DEBUG

#define VMAddr(type,x) ((type *)VMswap(x))

#else

#define VMAddr(type,x) \
((type *)(((VMTable[page_(x)].status|=VMtouched)&VMswapped)==VMswapped? \
VMswap(x):VMTable[page_(x)].addr+offset_(x)))

#endif
		
/* Host port: VM allocations are rounded up to the alignment of a	*/
/* pointer (and at least to 4 bytes, the transputer word): structures	*/
/* held in VM pages contain a char * on an LP64 host.			*/
#define VMgrain		(sizeof(char *) > 4 ? sizeof(char *) : 4)
#define VMwordlen(s)	((int)(((s) + VMgrain - 1) & ~(VMgrain - 1)))

/* Host port: VMWord(), not *(int *)&v: a VMRef is a bit-field struct	*/
/* (also in NullRef, VMInit, VMswap)					*/
#define VMcheck(v) (((int)VMWord(v) > 0) && (page_(v) > 0) && (page_(v) < VMTabUpb) && (offset_(v) < VMPageSize))

#define VMDirty(x) {if(VMTable[page_(x)].status&VMswapped)VMswap(x);VMTable[page_(x)].status|=VMdirty;}

#define NullRef(x) (VMWord(x) == 0)

extern VMpage *VMTable;
extern char *VMfilename;
extern VMRef NullVMRef;
extern int VMTabUpb;
extern int VMPageSize;
extern int VMLeave;

#ifdef __STDC__
extern APTR VMswap(VMRef v);
extern VMRef VMalloc(int size, VMRef v);
extern int VMInit(char *vmfile,int pagesize);
extern VMRef VMPage(void);
extern int VMleft(VMRef v);
extern void VMlock(VMRef v);
extern void VMunlock(VMRef v);
extern VMRef VMnext(VMRef v);
extern void VMLRU(void);
extern void VMTidy(void);
extern void VMStats(void);
extern void showvm(void);
#else
extern APTR VMswap();
extern VMRef VMalloc();
extern int VMInit();
extern VMRef VMPage();
extern int VMleft();
extern void VMlock();
extern void VMunlock();
extern VMRef VMnext();
extern void VMLRU();
extern void VMTidy();
extern void VMStats();
extern void showvm();
#endif
