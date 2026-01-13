# Makefile for nlagent project
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# directory structure
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)

# source and object files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/nlagent

.PHONY: all clean install uninstall debug run tree help

all: $(TARGET)

# create necessary directories
$(OBJ_DIR) $(BIN_DIR):
	@mkdir -p $@

# linking the final executable
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# compiling source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# clean target
clean:
	rm -rf $(BUILD_DIR)

# installation
install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/nlagent
# 	install -D -m 644 systemd/nlagent.service /etc/systemd/system/nlagent.service
# 	install -D -m 644 conf/nlagent.conf /etc/nlagent.conf
# 	systemctl daemon-reload

# uninstallation
uninstall:
	systemctl stop nlagent 2>/dev/null || true
	rm -f /usr/local/bin/nlagent
# 	rm -f /etc/systemd/system/nlagent.service
# 	rm -f /etc/nlagent.conf
# 	systemctl daemon-reload

# related to debugging
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# running the program
run: $(TARGET)
	sudo $(TARGET)

# show project
tree:
	@tree -I 'build|.git'

# help information
help:
	@echo "Available targets:"
	@echo "  all     - Build the project (default)"
	@echo "  clean   - Clean build artifacts"
	@echo "  install - Install to the system"
	@echo "  uninstall - Uninstall from the system"
	@echo "  debug   - Build debug version"
	@echo "  run     - Run the program (requires sudo)"
	@echo "  tree    - Show project structure"
	@echo "  help    - Show this help"
