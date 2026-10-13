
#include <stdio.h>
#include <setjmp.h>
/* Host port: the types come from ttypes.h on every host (it was the	*/
/* pre-ANSI branch; the ANSI one included Helios's <helios.h>, whose	*/
/* word is a long), plus the C library headers the sources use.	*/
/* <queue.h> is no longer included: the List/Node functions were	*/
/* declared but never used.						*/
#include "ttypes.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Helios: the C library has no strdup(); asm.c has one (helios_strdup) */
#ifdef __HELIOS
extern char *helios_strdup(const char *s);
#define strdup helios_strdup
#endif


#ifdef MWC
#define ATARI 1
#endif


#ifdef IBMPC
#define LITTLE_ENDIAN
#define VERBOSE 1
#endif

#ifdef __TRAN
#define LITTLE_ENDIAN
#define VERBOSE 0
#endif

extern FILE *outfd;
extern FILE *verfd;

#ifdef print
#undef print
#endif
#define print(zz) dbg("zz = %d[%8x]",zz,zz);

#ifdef New
#undef New
#endif
#define New(_type) (_type *)alloc((WORD)sizeof(_type))


#ifdef MWC
#define locase(c) (isupper(c)?_tolower(c):c)
#else
#define locase(c) (isupper(c)?tolower(c):c)
#endif


/* flags for trace */

extern WORD traceflags;
#define db_lex       0x001
#define db_syn       0x002
#define db_growcode  0x004
#define db_gencode   0x008
#define db_genimage  0x010
#define db_expr      0x020
#define db_eval      0x040
#define db_preasm    0x080
#define db_vm        0x100
#define db_kludge    0x200
#define db_files     0x400
#define db_mem	     0x800
#define db_sym	     0x1000
#define db_modules   0x2000
#define db_refs	     0x4000

/* warning flags */
extern WORD warnflags;
#define warn_unref	0x0001
#define warn_undef	0x0002
/* Fix: undefined symbols and duplicate definitions are errors; -wu	*/
/* and -wd make them warnings again, as in 1.3.	*/
#define warn_dup	0x0004

/* Host port: "ellipsis" stays empty (old-style declarations) for the	*/
/* file-local functions; the exported ones have full prototypes below,	*/
/* so that every call is checked against them.				*/
#define ellipsis

/* printf-style checking of the message functions' arguments		*/
#ifdef __GNUC__
#define PRINTFLIKE(f,a)	__attribute__((format(printf,f,a)))
#define NORETURN	__attribute__((noreturn))
#else
#define PRINTFLIKE(f,a)
#define NORETURN
#endif

#ifdef VM

#include "vm.h"

#ifdef __STDC__
extern VMRef VMNew(int size);
#else
extern VMRef VMNew();
#endif

#else

typedef long	VMRef;

#define VMlock(x)

#define VMunlock(x)

#define VMAddr(type,x) ((type *)(x))

#define VMNew(_type) (_type *)alloc((WORD)sizeof(_type))

#define VMDirty(x)

#define VMalloc alloc

#endif


/* general purpose union */
/* Host port: on an LP64 host the char * makes this 8 bytes, of which	*/
/* w, v and b share the first 4. A pointer stored in c must be read	*/
/* back through c, never through w. A Value is never written to an	*/
/* output file as a whole.						*/

typedef union Value
  {
    WORD	w;
    VMRef	v;
    UBYTE	b[ 4 ];
    char *	c;
  }
Value;

/* Host port: a Value with every byte zero. A Value is 8 bytes on LP64	*/
/* (it holds a char *); setting only .w or .v left the other bytes of	*/
/* the code entries undefined (never read, but not deterministic).	*/
#ifndef __GNUC__
#define inline		/* Helios C (Norcroft) has no inline */
#endif
static inline Value valzero(void)
{
	Value v;
	memset(&v,0,sizeof(v));
	return v;
}

/* keyword dictionary */

#define GLOBAL_HASHSIZE 	541
#define LOCAL_HASHSIZE 		31

struct keyentry
  {
    INT     next;           /* offset of next entry in chain 	*/
    INT     type;           /* type of entry                 	*/
    INT     value;          /* value of instruction, or subtype */
    char    name[ 1 ];      /* larger in practice           	*/
  };

/* Modules */

typedef struct STEntry
  {
    VMRef	head;
    int		entries;
  }
STEntry;

typedef struct asm_Module
  {
    VMRef	next;           	/* link in modlist              */
    VMRef	start;			/* code start and end...	*/
    VMRef	end;
    char *	file;			/* ptr to file name		*/
    VMRef	refs;			/* list of references		*/
    WORD	id;			/* module table slot            */
    WORD	linked;			/* true if added to link	*/
    STEntry	symtab[ LOCAL_HASHSIZE ];/* module symbol table		*/
  }
asm_Module;

typedef struct Ref
  {
    VMRef	next;			/* next ref in list	*/
    VMRef	sym;			/* referenced symbol	*/
  }
Ref;

/* Symbol table */

#define UINT	unsigned int
#define NEW_REF

typedef struct Symbol
  {
    VMRef	next;		/* next symbol in hash chain	*/
    VMRef	prev;		/* prev symbol in hash chain	*/
    INT		type;           /* symbol type                  */
#ifdef NEVER
    WORD	global;		/* true if global, else false	*/
    WORD	referenced;	/* true if referenced		*/
#else
    UINT	global:1,	/* true if global, else false	*/
    		referenced:1;	/* true if referenced		*/
#endif
#ifdef NEW_REF
    VMRef	fwd_ref;	/* link into forward ref chain	*/
#endif
    VMRef	module;		/* defining module		*/
    Value	def;            /* its definition               */
    char	name[ 1 ];      /* symbol (actually variable)   */
  }
Symbol;

/* expression tree nodes */

typedef struct unode
  {
    Value	expr;  		/* expression                   */
    UBYTE       ntype; 		/* node type                    */
    UBYTE       etype; 		/* expression type              */
  }
Unode;

typedef struct bnode
  {
    Value     	lexp;   	/* left expression              */
    Value     	rexp;   	/* right expression             */
    UBYTE       ntype;  	/* node type                    */
    UBYTE       ltype;  	/* left expression type         */
    UBYTE       rtype;  	/* right expression type        */
  }
Bnode;

/* code structures      */

typedef struct code
  {
    UBYTE       type;   /* see below                            */
    UBYTE       size;   /* number of bytes occupied in code     */
    UBYTE       vtype;  /* type of value                        */
    UBYTE       flags;	/* some flag bits			*/
    WORD        loc;    /* current code location                */
    Value	value;
#ifdef LINENO
    WORD        line;   /* source line which generated this code*/
#endif
  }
Code;

#define codeflags_fixed		1	/* size set by size directive	*/

#define maxtok 1024		/* max token length			*/

/* Transputer op codes  */

#define f_pfix          0x20l
#define f_nfix          0x60l

#define DefaultDataSize    4l          /* allows 512k addressability */

#define i_program       0x12345678L

#define i_helios        'h'
#define i_csc           'c'

/* lexical types        */

/* 0..15 - transputer primaries */

#define s_direct        0x80
#define s_oper          0x90
#define s_oper1         (s_oper+1)
#define s_oper2         (s_oper+2)
#define s_oper3         (s_oper+3)
#define s_oper4         (s_oper+4)

#define s_unbound      0xa0        /* unknown symbol type  */

/* the following values are chosen carefully */
/* their ordering relative to eachother and to s_number is important */

#define s_modnum       0xa1


#define s_codesymb     0xa6
#define s_coderef      0xa7
#define s_datasymb     0xa8
#define s_datadone     0xa9
#define s_commsymb     0xaa
#define s_commdone     0xab

#define islabel(x) ((s_codesymb<=(x))&&((x)<=s_commdone))
#define deftype(x) ((x)&0xfel)

#define s_directive     0xae
#define s_labdef        0xaF        /* from lex only        */
#define s_token         0xb0

/* types < labdef are in symbol table	*/
/* types >= token are not		*/

#define s_lbra          0xb1
#define s_rbra          0xb2
#define s_space         0xb3
#define s_tab           0xb4
#define s_nl            0xb5
#define s_other         0xb6
#define s_comma         0xb7
#define s_number        0xb8
#define s_eof           0xb9
#define s_quote         0xba
#define s_plus          0xbb
#define s_minus         0xbc
#define s_mul           0xbd
#define s_div           0xbe
#define s_colon         0xbf
#define s_rem           0xc0
#define s_dot           0xc1
#define s_shl           0xc2
#define s_shr           0xc3
#define s_and           0xc4
#define s_or            0xc5
#define s_xor           0xc6
#define s_not           0xc7
#define s_expr          0xc8
#define s_string        0xc9
#define s_monadic       0xcb
#define s_semic         0xcc
#define s_at            0xcd
#define s_cr		0xce

/* code and other symbols */
#define s_label         0xd2
#define s_code          0xd5
#define s_literal       0xd6
#define s_bss           0xd8
#define s_end           0xd9
#define s_newfile	0xda
#define s_newseg	0xdb

/* directives */
#define s_firstdir      0xe0
#define s_blkb          0xe1
#define s_blkw          0xe2
#define s_init          0xe3
#define s_align         0xe4
#define s_word          0xe5
#define s_byte          0xe6
#define s_module        0xe7
#define s_global        0xe8
#define s_data          0xe9
#define s_common	0xea
#define s_size		0xeb
#define s_ref		0xec

/* special characters */

#define c_lbra          '('
#define c_rbra          ')'
#define c_space         ' '
#define c_tab           '\t'
#define c_nl            '\n'
#define c_comma         ','
#define c_quote         '\"'
#define c_plus          '+'
#define c_minus         '-'
#define c_mul           '*'
#define c_div           '/'
#define c_or            '|'
#define c_hash          '#'
#define c_dot           '.'
#define c_and           '&'
#define c_xor           '^'
#define c_shl           '<'
#define c_shr           '>'
#define c_rem           '%'
#define c_not           '~'
#define c_colon         ':'
#define c_backslash     '\\'
#define c_semic         ';'
#define c_at            '@'
#define c_cr		'\r'
#define c_bell		07
#define c_bs		8
#define c_vt		11
#define c_ff		12

#define TargetBytesPerWord 4


extern FILE *	infd;
extern FILE *	outfd;
extern FILE *	verfd;
extern jmp_buf 	error_level;
extern WORD 	errors;
extern WORD 	traceflags;
extern WORD 	preasm;
extern WORD 	hexopt;
extern WORD 	defopt;
extern WORD 	fastopt;
extern WORD 	verbose;
extern WORD 	inlib;
extern FILE *	deffd;
extern WORD 	vmpagesize;
extern WORD 	instr_size;
extern char *	infile;		/* Fix: was BYTE infile[128] */
extern char *	curfile;
extern WORD 	waste;
extern void 	error(BYTE *str, ...) NORETURN PRINTFLIKE(1,2);
extern void 	warn(BYTE *str, ...) PRINTFLIKE(1,2);
extern void 	report(BYTE *str, ...) PRINTFLIKE(1,2);
extern void 	_trace(BYTE *str, ...) PRINTFLIKE(1,2);
extern void 	recover(BYTE *str, ...) NORETURN PRINTFLIKE(1,2);
extern void 	recoverwarn(BYTE *str, ...) NORETURN PRINTFLIKE(1,2);
extern void 	linkerror(BYTE *str, ...) PRINTFLIKE(1,2);
extern void 	asmerror(BYTE *str, ...) PRINTFLIKE(1,2);
extern WORD 	min(WORD a, WORD b);
extern WORD 	max(WORD a, WORD b);
extern WORD 	pfsize(WORD n);
extern void 	encode(WORD op, WORD arg, void (*pbytefn)());
extern UBYTE *	codevec;
extern WORD 	codepos;
extern void 	initcode(void);
extern void 	gencode(UBYTE *code, WORD size);
extern void 	genbyte(UWORD value);
extern void 	gendirect(UWORD op, UWORD vtype, WORD value);
extern void 	copycode(void);
extern VMRef 	deflabel(void);
extern void 	genglobal(VMRef label);
extern void 	genref(VMRef label);
extern void 	genlabdef(VMRef label);
extern void 	gendata(VMRef label, WORD size);
extern void 	gencommon(VMRef label, WORD size);
extern void 	geninit(void);
extern void 	genmodule(WORD mod);
extern void 	genalign(void);
extern void 	genword(WORD vtype, WORD value);
extern void 	genblkw(WORD type, WORD size);
extern void 	genblkb(WORD type, WORD size);
extern void 	genend(void);
extern void 	gensize(int size);
extern void 	newfile(void);
extern WORD 	asize(WORD value);
extern void 	genimage(void);
extern void 	growdata(void);
extern WORD 	simPc;
extern Code *	code;
extern WORD 	iteration;
extern WORD 	oldloc;
extern void 	growcode(void);
extern WORD 	eval(WORD type, WORD earg, WORD loc);
extern WORD 	dodyadic(WORD op, WORD lexp, WORD rexp);
extern WORD 	domonadic(WORD op, WORD value);
extern INT 	symb;
extern BYTE 	token[ maxtok + 16 ];
extern Symbol *	toksym;
extern VMRef 	toksymref;
extern struct keyentry *	tokkey;
extern INT 	toksize;
extern Value 	tokval;
extern bool 	textinput;	/* Fix: the input is .s text */
extern WORD 	tokcount;	/* Fix: tokens read so far */
extern bool 	toknegovf;	/* Fix: number > 0x80000000 */
extern WORD 	tempval;
/* Host port: keytab.c (written by keyform) defines the table as	*/
/* UWORD words; it was declared UBYTE here, a type mismatch between	*/
/* the files. lex.c addresses it in bytes. keyform.c has its own byte	*/
/* array of that name.							*/
#ifndef KEYFORM
extern UWORD 	keytab[];
#endif
extern WORD 	dicttab[];
extern void 	nextsym(void);
extern WORD 	codesize;
extern WORD 	heapsize;
extern STEntry 	Symtab[ GLOBAL_HASHSIZE ];
extern WORD 	symsize;
extern WORD 	exprsize;
extern WORD 	codeflags;
extern UBYTE * 	alloc(INT n);
extern void 	initmem(void);
extern VMRef 	unary(WORD ntype, WORD etype, WORD expr);
extern VMRef 	binary(WORD ntype, WORD ltype, WORD rtype, WORD lexp, WORD rexp);
extern VMRef 	newcode(WORD type, WORD size, WORD vtype, WORD loc, WORD value);
extern VMRef 	codeptr(void);
extern VMRef 	module0;
extern VMRef 	curmod;
extern VMRef 	firstmodule;
extern VMRef 	tailmodule;
extern word 	nmodules;
extern void 	setmodules(void);
extern VMRef 	startmodule(WORD mod);
extern void 	endmodule(void);
extern void 	refsymbol_def(VMRef v);
extern void 	refsymbol_nondef(VMRef v);
extern void 	modstats(void);
extern void 	parsepreasm(WORD ch);
extern void 	genpreasm(void);
extern void 	initsym(void);
extern VMRef 	lookup(BYTE *name);
extern VMRef 	insert(BYTE *name, WORD local);
extern void 	movesym(VMRef sym);
extern WORD 	lineno;
extern WORD 	etype;
extern void 	initasm(void);
extern void 	assemble(void);
extern void 	objed(FILE *fd, char *name, long stacksize, long heapsize);


extern void 	initmodules(void);
extern void 	show_unref(void);
extern void 	printtab(STEntry *tab);
extern char *	exprlabel(WORD type, WORD value);	/* Fix: new */

extern WORD filepos;

#ifdef RDCH_FN
extern WORD asm_rdch(ellipsis);
extern WORD unrdch(ellipsis);
#else
/* macro versions of asm_rdch & unrdch	*/
extern WORD uch;
extern WORD unreadready;
#define asm_rdch() (unreadready?(unreadready=FALSE,uch):getc(infd))
#define unrdch(c) (unreadready=TRUE,uch=c)
#endif

#define eqs(a,b) (strcmp(a,b)==0)
