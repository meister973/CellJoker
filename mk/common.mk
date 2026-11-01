#---------------------------------------------------------------------------------
# Shared PSL1GHT project rules. A project Makefile sets TARGET, SOURCES, LIBS and
# optionally TITLE, APPID, EXTRA_INCLUDES, then includes this file.
#
# The real build runs in a sub-make inside build/, which is why OFILES, VPATH and
# OUTPUT are exported. Bootable output is build/$(TARGET).elf: only that copy goes
# through sprxlinker.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "PSL1GHT is not set. Build through docker/ps3.sh.")
endif

include $(PSL1GHT)/ppu_rules

BUILD     := build
TITLE     ?= $(TARGET)
APPID     ?= BLTR00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000

# Recursive on purpose: $@ and INCLUDE are only meaningful at use time in the sub-make.
CFLAGS    = -O2 -Wall -mcpu=cell -std=gnu99 $(MACHDEP) $(INCLUDE)
LDFLAGS   = $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT    := $(CURDIR)/$(TARGET)
export VPATH     := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR   := $(CURDIR)/$(BUILD)
export BUILDDIR  := $(CURDIR)/$(BUILD)

CFILES           := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
VCGFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.vcg)))
FCGFILES         := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.fcg)))

export LD        := $(CC)
export OFILES    := $(addsuffix .o,$(VCGFILES:.vcg=.vpo)) \
                    $(addsuffix .o,$(FCGFILES:.fcg=.fpo)) \
                    $(CFILES:.c=.o)

export INCLUDE   := -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2 \
                    $(foreach dir,$(EXTRA_INCLUDES),-I$(CURDIR)/$(dir)) \
                    $(LIBPSL1GHT_INC) \
                    -I$(CURDIR)/$(BUILD)

export LIBPATHS  := -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).self $(TARGET).fake.self

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

%.vpo.o: %.vpo
	@$(bin2o)

%.fpo.o: %.fpo
	@$(bin2o)

-include $(DEPENDS)

endif
