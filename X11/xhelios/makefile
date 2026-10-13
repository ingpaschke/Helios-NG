#------------------------------------------------------------------------------
# Helios generic make system - X11/xhelios: the X11R4 server for Helios
# (xhelios) and its device drivers, built with clang (llvm-t800).
#
# The server is built by tools/server_build.py from ../X11R4/mit, the
# drivers (newATW.d for the ATW800/2, mouse.d, keyboard.d) by build.py;
# both run with this tree's headers, libraries and host tools.
#------------------------------------------------------------------------------

ifndef InProcMake
# This part of the makefile is run in the components source directory

# Switch to processor specific directory
ifndef HPROC
include ../../makeinc/DFLT.mak
endif

.PHONY: notarget
notarget:
	@$(MAKE) -C $(HPROC)

.DEFAULT:
	@$(MAKE) -C $(HPROC) $@

else
#-----------------------------------------------------------------------------
# This part of the makefile is always run in a processor specific subdirectory.

COMPONENT = X11/xhelios

include $(HSRC)/makeinc/$(HHOST).mak	# Default host system variables
include $(HSRC)/makeinc/$(HPROC).mak	# Default processor rules and vars.

.PHONY: default clean

XSRC	:= $(HSRC)/$(COMPONENT)
# the compiler and runtime of hcc (tools/build-t800/build.sh sets these)
HCC_CLANG ?= clang
XRTDIR	:= $(dir $(HCC_RTLIB))
XBUILD	:= $(CURDIR)/build

default: xhelios drivers

xhelios:
	python3 $(XSRC)/tools/server_build.py --cpu t425 --sysroot $(HPROD) \
	  --clang $(HCC_CLANG) --asm $(HHOSTBIN)/asm \
	  --rtlib $(XRTDIR)llvmrt-t4.lib --upstream $(HSRC)/X11/X11R4/mit \
	  --work $(XBUILD)/server
	cp $(XBUILD)/server/t425/server/xhelios $@

VPATH	:= $(XSRC)

# the end of a driver module, as filesys/devs/raw has it
drivers: modend.p
	python3 $(XSRC)/build.py --cpu t425 --sysroot $(HPROD) \
	  --clang $(HCC_CLANG) --asm $(HHOSTBIN)/asm --ampp $(HHOSTBIN)/ampp \
	  --modend $(CURDIR)/modend.p --work $(XBUILD)/drivers
	$(TOUCH) $@

install: default
	$(CP) xhelios $(HPROD)/bin
	$(CP) $(XBUILD)/drivers/t425/newatw.d $(HPROD)/lib/newATW.d
	$(CP) $(XBUILD)/drivers/t425/mouse.d $(XBUILD)/drivers/t425/keyboard.d $(HPROD)/lib
	$(CP) $(XSRC)/vendor/helios/devices/newxrc $(HPROD)/etc
	$(TOUCH) install

clean:
	-$(RM) -r xhelios drivers modend.p install $(XBUILD)

endif #InProcMake	# This line must be at EOF
