# Linux I/O server. Compile the maintained sources directly; keep all products
# under BUILD_DIR. CC may also select a Linux cross-compiler.
.DEFAULT_GOAL := all

LINUX_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SERVER_DIR := $(abspath $(LINUX_DIR)/..)
BUILD_DIR ?= $(LINUX_DIR)/build
BUILD_DIR := $(abspath $(BUILD_DIR))
OBJ_DIR := $(BUILD_DIR)/obj
OUT ?= $(BUILD_DIR)/ioserver

CFLAGS ?= -O2 -g
CPPFLAGS ?= -D_FORTIFY_SOURCE=2

TLS ?= 0
SSH ?= 0
SSHD ?= 0
TLS_LIBS ?= -lssl -lcrypto
SSH_LIBS ?= -lssh2
SSHD_LIBS ?= -lssh
LINK_SOURCES ?= $(LINUX_DIR)/linkext.c
LIB_DEPS ?=

# i486V4 selects the shared Unix adapter, not the host instruction set.
PORT_CPPFLAGS := -DLINUX -Di486V4 -D__GNU__=1 -DETC_DIR \
                -I$(LINUX_DIR) -I$(SERVER_DIR)
PORT_CFLAGS := -std=gnu89 -fcommon -fno-strict-aliasing -fsigned-char \
               -fno-aggressive-loop-optimizations -fwrapv

# The shared vintage sources produce these warnings. Pointer/word conversion
# errors remain enabled, since the host may be wider than the Helios protocol.
PORT_CFLAGS += -Wall -Wno-pointer-sign -Wno-unused-variable \
               -Wno-unused-but-set-variable -Wno-unused-function \
               -Wno-unused-result -Wno-misleading-indentation \
               -Wno-char-subscripts -Wno-parentheses \
               -Wno-maybe-uninitialized -Wno-stringop-truncation \
               -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast \
               -Werror=int-conversion -Werror=incompatible-pointer-types

ifeq ($(TLS),1)
PORT_CPPFLAGS += -DIOS_TLS $(TLS_CFLAGS)
PORT_CFLAGS += -pthread
TLS_LINK := $(TLS_LIBS) -pthread -ldl
endif
ifeq ($(SSH),1)
PORT_CPPFLAGS += -DIOS_SSH $(SSH_CFLAGS)
PORT_CFLAGS += -pthread
SSH_LINK := $(SSH_LIBS) -pthread
endif
ifeq ($(SSHD),1)
PORT_CPPFLAGS += -DIOS_SSHD $(SSHD_CFLAGS)
PORT_CFLAGS += -pthread
SSHD_LINK := $(SSHD_LIBS) -pthread
endif

# Track libraries named directly. For libraries selected with -l or other
# linker options, LIB_DEPS can supply the corresponding files.
LIBRARY_FILES := $(filter %.a %.so,$(SSH_LINK) $(SSHD_LINK) $(TLS_LINK) $(LDLIBS))

SOURCES := server.c files.c devices.c tload.c cofuns.c debug.c terminal.c \
           linkio.c tcp.c sun/sunlocal.c sun/linklib.c \
           linux/t4link.c linux/colib_linux.c linux/termcap_lite.c \
           linux/compat.c linux/hostio.c linux/multiwait.c \
           linux/net_compat.c linux/net_tls.c linux/net_ssh.c linux/ssh_server.c
OBJECTS := $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o))
DRIVER_OBJECTS := $(addprefix $(OBJ_DIR)/drivers/,$(notdir $(LINK_SOURCES:.c=.o)))
OBJECTS += $(DRIVER_OBJECTS)
DEPENDENCIES := $(OBJECTS:.o=.d)

# Changing features, compiler or flags must invalidate existing objects too.
# Quote definitions literally when recording them, including embedded quotes.
quote = '$(subst ','"'"',$(1))'
CONFIG := $(BUILD_DIR)/build.conf

.PHONY: all clean FORCE
all: $(OUT)

$(CONFIG): FORCE
	@mkdir -p "$(@D)"
	@printf '%s\n' $(call quote,CC=$(CC)) \
	  $(call quote,CPPFLAGS=$(CPPFLAGS) $(PORT_CPPFLAGS)) \
	  $(call quote,CFLAGS=$(CFLAGS) $(PORT_CFLAGS)) \
	  $(call quote,LDFLAGS=$(LDFLAGS)) \
	  $(call quote,LDLIBS=$(SSH_LINK) $(SSHD_LINK) $(TLS_LINK) $(LDLIBS)) \
	  $(call quote,LIB_DEPS=$(LIB_DEPS)) \
	  $(call quote,LINK_SOURCES=$(LINK_SOURCES)) > "$@.tmp"
	@if cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

$(OUT): $(OBJECTS) $(CONFIG) $(LIBRARY_FILES) $(LIB_DEPS)
	@mkdir -p "$(@D)"
	$(CC) $(CFLAGS) $(PORT_CFLAGS) $(LDFLAGS) -o "$@" $(OBJECTS) -lm \
	  $(SSH_LINK) $(SSHD_LINK) $(TLS_LINK) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SERVER_DIR)/%.c $(CONFIG) $(LINUX_DIR)/Makefile
	@mkdir -p "$(@D)"
	$(CC) $(CPPFLAGS) $(PORT_CPPFLAGS) $(CFLAGS) $(PORT_CFLAGS) \
	  -MMD -MP -c "$<" -o "$@"

# Hardware backends stay outside the core tree. Use distinct source basenames
# when selecting more than one backend through LINK_SOURCES.
define driver_rule
$(OBJ_DIR)/drivers/$(notdir $(1:.c=.o)): $(1) $(CONFIG) $(LINUX_DIR)/Makefile
	@mkdir -p "$$(@D)"
	$$(CC) $$(CPPFLAGS) $$(PORT_CPPFLAGS) $$(CFLAGS) $$(PORT_CFLAGS) \
	  -MMD -MP -c "$$<" -o "$$@"
endef
$(foreach source,$(LINK_SOURCES),$(eval $(call driver_rule,$(source))))

clean:
	$(RM) $(OBJECTS) $(DEPENDENCIES) $(CONFIG) $(CONFIG).tmp $(OUT)

-include $(DEPENDENCIES)
