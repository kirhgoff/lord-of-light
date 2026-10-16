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

SIM_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/sim/*.c))
VIEW_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/view/*.c))
TEST_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard tests/*.c))
GAME_OBJECTS := $(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS) $(SIM_OBJECTS)
HEADLESS_OBJECTS := $(BUILD)/obj/src/main_headless.o $(SIM_OBJECTS)

.PHONY: game headless test run clean

game: $(BUILD)/game
headless: $(BUILD)/headless

run: game
	$(BUILD)/game

test: $(BUILD)/tests
	$(BUILD)/tests

$(BUILD)/game: $(GAME_OBJECTS) $(RAYLIB_LIB)
	$(CC) $^ $(FRAMEWORKS) -o $@

$(BUILD)/headless: $(HEADLESS_OBJECTS)
	$(CC) $^ -o $@

$(BUILD)/tests: $(TEST_OBJECTS) $(SIM_OBJECTS)
	$(CC) $^ -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STRICT) -MMD -MP -c $< -o $@

$(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS): STRICT += -I$(RAYLIB_DIR)

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

-include $(shell find $(BUILD)/obj -name "*.d" 2>/dev/null)
