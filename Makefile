# Makefile for nlagent project
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS = -lpthread

# directory structure
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)
TEST_DIR = tests

# source and object files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = $(BIN_DIR)/nlagent

.PHONY: all clean install uninstall debug run tree help asan asan-run test valgrind

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

# uninstallation
uninstall:
	systemctl stop nlagent 2>/dev/null || true
	rm -f /usr/local/bin/nlagent

# related to debugging
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# ── Address Sanitizer (ASan) + Undefined Behavior Sanitizer (UBSan) ──
asan: CFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -O1
asan: LDFLAGS += -fsanitize=address -fsanitize=undefined
asan: clean all

# build ASan version and run with a quick smoke test
asan-run: asan
	@echo "Starting agent with ASan (will run 5 seconds for smoke test)..."
	sudo timeout 5 ./$(TARGET) || true

# running the program
run: $(TARGET)
	sudo $(TARGET)

# ── Stress test ──
test: $(TARGET)
	@echo "Starting stress test..."
	sudo bash $(TEST_DIR)/stress_test.sh

# ── Valgrind ──
valgrind: $(TARGET)
	@echo "Running agent under valgrind for 10 seconds..."
	sudo valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes timeout 10 ./$(TARGET) || true

# show project
tree:
	@tree -I 'build|.git'

# help information
help:
	@echo "Available targets:"
	@echo "  all       - Build the project (default)"
	@echo "  clean     - Clean build artifacts"
	@echo "  debug     - Build debug version (-O0)"
	@echo "  asan      - Build with ASan + UBSan"
	@echo "  asan-run  - Build ASan version and smoke test 5s"
	@echo "  valgrind  - Run under valgrind for 10s"
	@echo "  test      - Run stress test"
	@echo "  run       - Run the program (requires sudo)"
	@echo "  install   - Install to the system"
	@echo "  uninstall - Uninstall from the system"
	@echo "  tree      - Show project structure"
	@echo "  help      - Show this help"
