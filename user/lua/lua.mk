# dmaOS Lua Subsystem Makefile Module

LUA_DIR := user/lua
LUA_UPSTREAM := third_party/lua

ifeq ($(ARCH),riscv32)
  LUA_ARCH_CFLAGS := -DARCH_RISCV32
else
  LUA_ARCH_CFLAGS := -DARCH_RISCV64
endif

LUA_CFLAGS := $(user_cflags) -I$(LUA_UPSTREAM) -I$(LUA_DIR) -I$(USER_DIR)/lib -I$(SHARED_DIR) $(LUA_ARCH_CFLAGS)

LUA_OBJS := $(BUILD_DIR)/user/lua/onelua.o \
            $(BUILD_DIR)/user/lua/socket.o \
            $(BUILD_DIR)/user/lua/main.o

$(BUILD_DIR)/user/lua/onelua.o: $(LUA_UPSTREAM)/onelua.c
	@mkdir -p $(dir $@)
	$(cc) $(LUA_CFLAGS) -DMAKE_LIB -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/lua/%.o: $(LUA_DIR)/%.c
	@mkdir -p $(dir $@)
	$(cc) $(LUA_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/lua.elf: $(LUA_OBJS) $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/lua.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc
