CC ?= cc
CPPFLAGS := -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := .build
OBJ_DIR := $(BUILD_DIR)/obj
DEP_DIR := $(BUILD_DIR)/dep
BIN_DIR := $(BUILD_DIR)/bin
TARGET := $(BIN_DIR)/phase1_runner

SOURCES := phase1.c
OBJECTS := $(OBJ_DIR)/phase1.o
DEPS := $(DEP_DIR)/phase1.d

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

$(OBJ_DIR)/phase1.o: phase1.c | $(OBJ_DIR) $(DEP_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/phase1.d -c $< -o $@

$(OBJ_DIR) $(DEP_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
