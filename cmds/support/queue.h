/****************************************************************/
/* queue.h                                                      */
/*                                                              */
/* Queuing structures                                           */
/*                                                              */
/****************************************************************/
/* RcsId: $Id: queue.h,v 1.2 1991/10/21 09:42:23 paul Exp $ Copyright (C) Perihelion Software Ltd.	*/

#ifndef __queueh
#define __queueh

typedef struct Node {
        struct Node *Next;      /* next node in list            */
        struct Node *Prev;      /* previous node in list        */
} Node;

typedef struct List {
        struct Node *Head;      /* head node on list            */
        struct Node *Earth;     /* always NULL                  */
        struct Node *Tail;      /* tail node on list            */
} List;

/* Host port: prototypes of queue.c (callers such as c.c declared none, */
/* so Remove()'s pointer result was an implicit int)                    */
extern void		InitList( struct List * );
extern void		AddTail( struct List *, struct Node * );
extern void		AddHead( struct List *, struct Node * );
extern struct Node *	RemHead( struct List * );
extern struct Node *	RemTail( struct List * );
extern void		PreInsert( struct Node *, struct Node * );
extern void		PostInsert( struct Node *, struct Node * );
extern struct Node *	Remove( struct Node * );

#endif
