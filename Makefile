CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BUILD_DIR := build
TARGET := $(BUILD_DIR)/rubiks
SOURCES := src/main.cpp src/cube.cpp src/dataset.cpp
OBJECTS := $(SOURCES:src/%.cpp=$(BUILD_DIR)/%.o)

.PHONY: all clean viewer

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $@ $^

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

viewer:
	cmake -S . -B build-viewer -DRUBIKS_BUILD_VIEWER=ON
	cmake --build build-viewer
