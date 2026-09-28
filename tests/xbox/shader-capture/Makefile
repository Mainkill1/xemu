TOPOLOGY_COVERAGE ?= 0
ifeq ($(TOPOLOGY_COVERAGE),1)
XBE_TITLE = shader-capture-topology-32-uses
GEN_XISO = shader-capture-topology-32-uses.iso
OUTPUT_DIR ?= bin-topology
NXDK_CFLAGS += -DTOPOLOGY_COVERAGE=1
else ifeq ($(TOPOLOGY_COVERAGE),0)
XBE_TITLE = shader-capture-32-uses
GEN_XISO = shader-capture-32-uses.iso
else
$(error TOPOLOGY_COVERAGE must be 0 or 1)
endif
SRCS = $(CURDIR)/main.c
SHADER_OBJS = capture-pixel.inl capture-vertex.inl
NXDK_CFLAGS += -Wall -Wextra -Werror
include $(NXDK_DIR)/Makefile

# This fixture's object must be rebuilt when switching its compile-time mode.
# Keep SDK libraries incremental and prevent a stale default object in the XBE.
.PHONY: fixture-mode
$(CURDIR)/main.obj: fixture-mode
