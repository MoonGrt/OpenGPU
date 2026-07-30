.DEFAULT_GOAL := app

WORK_DIR := $(shell pwd)
BUILD_DIR := $(WORK_DIR)/build
OBJ_DIR := $(BUILD_DIR)/obj-$(NAME)
BINARY := $(BUILD_DIR)/$(NAME)

LD := $(CXX)
INCLUDES := $(addprefix -I,$(INC_PATH))
ifeq ($(CONFIG_WAVE),y)
CFLAGS += -DWAVEOUT=\"$(BUILD_DIR)\"
endif
CFLAGS := -MMD -Wall -Werror $(INCLUDES) $(CFLAGS)
CXXFLAGS += $(CFLAGS)
LDFLAGS := -O2 $(LDFLAGS)

OBJS := $(SRCS:%.c=$(OBJ_DIR)/%.o) $(CXXSRC:%.cc=$(OBJ_DIR)/%.o)

$(OBJ_DIR)/%.o: %.c
	@echo + CC $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.cc
	@echo + CXX $<
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(OBJS:.o=.d)

app: $(BINARY)

$(BINARY): $(OBJS)
	@echo + LD $@
	@$(LD) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: app clean
