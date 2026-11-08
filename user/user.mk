#
# dmaOS Userland Makefile Module
#

# User programs, assets, and configs to stage
uprogs := cat doom gfxtest hello kilo less ls lua memtest mkdir nettest nslookup rm snake write
utxts := hello.txt lorem.txt meow.txt test.lua httpd.lua www/index.html www/style.css doom1.wad #doom2.wad doom3.wad
uconfigs := dmash.cfg net.cfg

# Toolchain flags for userland
user_cflags := $(cflags) -isystem $(SYSROOT)/include \
               -include user/lib/user.h \
               -ffunction-sections -fdata-sections -flto \
               -D_POSIX_C_SOURCE=200809L
user_ldflags := $(ldflags) -Wl,--gc-sections -s -flto

# ----------------- User Library -----------------

USER_LIB_OBJS := $(addprefix $(BUILD_DIR)/user/lib/, user.o)

$(BUILD_DIR)/user/lib/%.o: $(USER_DIR)/lib/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

# ----------------- Shell -----------------

$(BUILD_DIR)/user/sh/shell.o: $(USER_DIR)/sh/shell.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/sh/shell.elf: $(BUILD_DIR)/user/sh/shell.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/sh/shell.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

$(BUILD_DIR)/user/sh/shell.bin: $(BUILD_DIR)/user/sh/shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

$(BUILD_DIR)/user/sh/shell.bin.o: $(BUILD_DIR)/user/sh/shell.bin
	cd $(BUILD_DIR)/user/sh && $(objcopy) -Ibinary -O$(OBJCOPY_ELF_TARGET) shell.bin shell.bin.o

# ----------------- User Programs & Packages -----------------

# Kilo Text Editor
$(BUILD_DIR)/user/bin/kilo.o: third_party/kilo/kilo.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -Wno-char-subscripts -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

# Lua 5.4 Interpreter Subsystem
include $(USER_DIR)/lua/lua.mk

# DOOM (doomgeneric)
DOOM_DIR := third_party/doomgeneric/doomgeneric
DOOM_EXCLUDE_SRCS := $(DOOM_DIR)/doomgeneric_%.c \
                $(DOOM_DIR)/i_allegromusic.c \
                $(DOOM_DIR)/i_allegrosound.c \
                $(DOOM_DIR)/i_sdlmusic.c \
                $(DOOM_DIR)/i_sdlsound.c \
                $(DOOM_DIR)/i_winmusic.c \
                $(DOOM_DIR)/i_cdmus.c \
                $(DOOM_DIR)/i_sound.c

DOOM_SRCS := $(filter-out $(DOOM_EXCLUDE_SRCS), $(wildcard $(DOOM_DIR)/*.c))
DOOM_SRCS += user/lib/doomgeneric_dmaos.c
DOOM_CFLAGS := $(user_cflags) -I$(DOOM_DIR) -Wno-error

DOOM_OBJS := $(patsubst $(DOOM_DIR)/%.c, $(BUILD_DIR)/user/bin/doom/%.o, $(filter $(DOOM_DIR)/%.c, $(DOOM_SRCS)))
DOOM_OBJS += $(BUILD_DIR)/user/lib/doomgeneric_dmaos.o

$(BUILD_DIR)/user/bin/doom/%.o: $(DOOM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(cc) $(DOOM_CFLAGS) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/lib/doomgeneric_dmaos.o: $(USER_DIR)/lib/doomgeneric_dmaos.c
	@mkdir -p $(dir $@)
	$(cc) $(DOOM_CFLAGS) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/doom.elf: $(DOOM_OBJS) $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/doom.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

# Generic Standalone User Binary Rules
$(BUILD_DIR)/user/bin/%.o: $(USER_DIR)/bin/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/%.elf: $(BUILD_DIR)/user/bin/%.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/$*.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

# Rootfs Binary Packaging
$(BUILD_DIR)/root/bin/%: $(BUILD_DIR)/user/elf/%.elf
	@mkdir -p $(dir $@)
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@
