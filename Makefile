# Makefile for OrderCache

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.
DEBUG_FLAGS = -g -O0 -DDEBUG
RELEASE_FLAGS = -O3 -DNDEBUG

SRC_DIR = OrderCache
TOOLS_DIR = tools
BUILD_DIR = build

# Source files
SOURCES = $(SRC_DIR)/OrderCache.cpp \
          $(SRC_DIR)/OrderCacheTest.cpp \
          $(SRC_DIR)/OrderCacheTestRunner.cpp \
          $(SRC_DIR)/OrderCacheStressTest.cpp \
          $(SRC_DIR)/OrderCacheBenchmark.cpp

# Targets
TEST_TARGET = test_runner
DEBUG_TARGET = test_runner_debug

.PHONY: all clean test debug help generate-example

all: $(TEST_TARGET)

# Build test runner
$(TEST_TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(RELEASE_FLAGS) -o $@ $^

# Build debug version
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $@ $^

# Run tests
test: $(TEST_TARGET)
	@echo "Running tests..."
	@./$(TEST_TARGET)

# Clean build artifacts
clean:
	rm -f $(TEST_TARGET) $(DEBUG_TARGET)
	rm -rf $(BUILD_DIR)
	rm -rf $(TOOLS_DIR)/generated
	rm -f $(TOOLS_DIR)/command_definition.yaml
	rm -f $(TOOLS_DIR)/command_definition.json

# Generate example command definition
generate-example:
	cd $(TOOLS_DIR) && python3 generate_command.py --example

# Generate code from definition (requires command_definition.yaml)
generate-code:
	cd $(TOOLS_DIR) && python3 generate_command.py --config command_definition.yaml --output-dir generated

# Interactive code generation
generate-interactive:
	cd $(TOOLS_DIR) && python3 generate_command.py --interactive

# Help
help:
	@echo "OrderCache Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all                  - Build release version (default)"
	@echo "  debug                - Build debug version"
	@echo "  test                 - Build and run tests"
	@echo "  clean                - Remove build artifacts"
	@echo "  generate-example     - Generate example command definition"
	@echo "  generate-code        - Generate code from command_definition.yaml"
	@echo "  generate-interactive - Run code generator in interactive mode"
	@echo "  help                 - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                           # Build release version"
	@echo "  make test                      # Build and run tests"
	@echo "  make generate-example          # Create example definition"
	@echo "  make generate-code             # Generate code from definition"
	@echo "  make clean                     # Clean build artifacts"

# Dependencies
$(SRC_DIR)/OrderCache.o: $(SRC_DIR)/OrderCache.cpp $(SRC_DIR)/OrderCache.h $(SRC_DIR)/OrderCacheInterface.h
$(SRC_DIR)/OrderCacheTest.o: $(SRC_DIR)/OrderCacheTest.cpp $(SRC_DIR)/OrderCache.h $(SRC_DIR)/OrderCacheTestUtils.h
$(SRC_DIR)/OrderCacheTestRunner.o: $(SRC_DIR)/OrderCacheTestRunner.cpp $(SRC_DIR)/OrderCacheTestUtils.h
$(SRC_DIR)/OrderCacheStressTest.o: $(SRC_DIR)/OrderCacheStressTest.cpp $(SRC_DIR)/OrderCache.h
$(SRC_DIR)/OrderCacheBenchmark.o: $(SRC_DIR)/OrderCacheBenchmark.cpp $(SRC_DIR)/OrderCache.h
