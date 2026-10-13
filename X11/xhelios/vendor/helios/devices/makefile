# makefile for X device drivers

.SUFFIXES:
.SUFFIXES: .o .c

# macros

CC 	   	= 	c
CP	   	=	cp

INC        	= 	-I.. 

CCFLAGS    	= 	-T4 -FA $(INC)
ASMFLAGS   	= 	-O
OBJEDFLAGS 	=	
AMPPFLAGS	=	-y-dhelios.TRAN -y1
DEFINES		=	# defined -DDEBUG to enable debugging
LIBS		=	-B -L/helios/lib -lkernel -lsys -lutil -lposix -lc

#PROGRAM         = 	newsplash
#PROGRAM        = 	newCGD
#PROGRAM        = 	newGDS
PROGRAM        = 	newATW
#PROGRAM        = 	newg300

#rules

.c.o:
	$(CC) $(CCFLAGS) -r $(DEFINES) -c $*.c

.a.o:
	$(CC) $(AMPPFLAGS) -c $*.a


#targets

#all:	$(PROGRAM).d harness mouse.d keyboard.d
all:	$(PROGRAM).d 

device.o : device.a
	$(CC) $(AMPPFLAGS) -c device.a

$(PROGRAM).d: device.o $(PROGRAM).o 
	$(CC) device.o $(PROGRAM).o /helios/lib/modend.o $(LIBS) -o $(PROGRAM).d
	$(CP) $(PROGRAM).d /helios/lib

$(PROGRAM).o: $(PROGRAM).c

harness.o: harness.c ../mdutils.c
	$(CC) $(CCFLAGS) -c harness.c

reset: reset.c ../mdutils.c
	$(CC) $(CCFLAGS) $(ASMFLAGS) $*.c -o $*

screen.o: screen.c
	$(CC) $(CCFLAGS) -c screen.c

harness: harness.o screen.o
	$(CC) $(ASMFLAGS) harness.o screen.o -o harness

mouse.d: device.o mouse.o 
	$(CC) device.o mouse.o /helios/lib/modend.o $(LIBS) -o mouse.d
	$(CP) mouse.d /helios/lib

mouse.o: mouse.c
	$(CC) $(CCFLAGS) -r -wA \
	-I../../../include -I../../../../X11 -I../../../.. -I../../mi \
	-DNOSTDHDRS=0 -c mouse.c


keyboard.d: device.o keyboard.o 
	$(CC) device.o keyboard.o /helios/lib/modend.o $(LIBS) -o keyboard.d
	$(CP) keyboard.d /helios/lib

keyboard.o: keyboard.c
	$(CC) $(CCFLAGS) -r -wA \
	-I../../../include -I../../../../X11 -I../../../.. -I../../mi \
	-DNOSTDHDRS=0 -c keyboard.c

