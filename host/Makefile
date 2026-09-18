CC ?= cc
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := .build
OBJ_DIR := $(BUILD_DIR)/obj
DEP_DIR := $(BUILD_DIR)/dep
BIN_DIR := $(BUILD_DIR)/bin
TARGET := $(BIN_DIR)/phase1_host

SOURCES := $(sort $(wildcard src/*.c))
OBJECTS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(patsubst src/%.c,$(DEP_DIR)/%.d,$(SOURCES))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR) $(DEP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

$(OBJ_DIR) $(DEP_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
