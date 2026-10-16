CC := clang
BUILD := build
STD := -std=c11
STRICT := $(STD) -Wall -Wextra -Werror
VENDOR_FLAGS := $(STD) -w -O2

RAYLIB_DIR := vendor/raylib/src
RAYLIB_SOURCES := rcore rshapes rtextures rtext rmodels raudio utils
RAYLIB_DEFINES := -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33
RAYLIB_OBJECTS := $(RAYLIB_SOURCES:%=$(BUILD)/raylib/%.o) $(BUILD)/raylib/rglfw.o
RAYLIB_LIB := $(BUILD)/libraylib.a
FRAMEWORKS := -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL

SIM_SOURCES := $(wildcard src/sim/*.c)
VIEW_SOURCES := $(wildcard src/view/*.c)
TEST_SOURCES := $(wildcard tests/*.c)

.PHONY: game headless test run clean

game: $(BUILD)/game
headless: $(BUILD)/headless

run: game
	$(BUILD)/game

test: $(BUILD)/tests
	$(BUILD)/tests

$(BUILD)/game: src/main_game.c $(SIM_SOURCES) $(VIEW_SOURCES) $(RAYLIB_LIB)
	$(CC) $(STRICT) -I$(RAYLIB_DIR) $^ $(FRAMEWORKS) -o $@

$(BUILD)/headless: src/main_headless.c $(SIM_SOURCES)
	@mkdir -p $(BUILD)
	$(CC) $(STRICT) $^ -o $@

$(BUILD)/tests: $(TEST_SOURCES) $(SIM_SOURCES)
	@mkdir -p $(BUILD)
	$(CC) $(STRICT) $^ -o $@

$(RAYLIB_LIB): $(RAYLIB_OBJECTS)
	ar rcs $@ $^

$(BUILD)/raylib/rglfw.o: $(RAYLIB_DIR)/rglfw.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) $(RAYLIB_DEFINES) -x objective-c -I$(RAYLIB_DIR)/external/glfw/include -c $< -o $@

$(BUILD)/raylib/%.o: $(RAYLIB_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) $(RAYLIB_DEFINES) -I$(RAYLIB_DIR)/external/glfw/include -c $< -o $@

clean:
	rm -rf $(BUILD)
