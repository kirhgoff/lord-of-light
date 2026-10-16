CC := clang
BUILD := build
SAN :=
STD := -std=c11
STRICT := $(STD) -O2 -Wall -Wextra -Werror -Ivendor/cjson $(SAN)
VENDOR_FLAGS := $(STD) -w -O2 $(SAN)

RAYLIB_DIR := vendor/raylib/src
RAYLIB_SOURCES := rcore rshapes rtextures rtext rmodels raudio utils
RAYLIB_DEFINES := -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33
RAYLIB_OBJECTS := $(RAYLIB_SOURCES:%=$(BUILD)/raylib/%.o) $(BUILD)/raylib/rglfw.o
RAYLIB_LIB := $(BUILD)/libraylib.a
FRAMEWORKS := -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL

CJSON_OBJECT := $(BUILD)/vendor/cjson.o
SIM_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/sim/*.c))
VIEW_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/view/*.c))
TEST_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard tests/*.c))
GAME_OBJECTS := $(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS) $(SIM_OBJECTS) $(CJSON_OBJECT)
HEADLESS_OBJECTS := $(BUILD)/obj/src/main_headless.o $(SIM_OBJECTS) $(CJSON_OBJECT)
BALANCE_OBJECTS := $(BUILD)/obj/src/main_balance.o $(SIM_OBJECTS) $(CJSON_OBJECT)

.PHONY: game headless test run clean asan balance

game: $(BUILD)/game
headless: $(BUILD)/headless

run: game
	$(BUILD)/game

test: $(BUILD)/tests
	$(BUILD)/tests

$(BUILD)/game: $(GAME_OBJECTS) $(RAYLIB_LIB)
	$(CC) $^ $(FRAMEWORKS) -o $@

$(BUILD)/headless: $(HEADLESS_OBJECTS)
	$(CC) $^ $(SAN) -o $@

balance: $(BUILD)/balance
	$(BUILD)/balance

$(BUILD)/balance: $(BALANCE_OBJECTS)
	$(CC) $^ $(SAN) -o $@

$(BUILD)/tests: $(TEST_OBJECTS) $(SIM_OBJECTS) $(CJSON_OBJECT)
	$(CC) $^ $(SAN) -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STRICT) -MMD -MP -c $< -o $@

$(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS): STRICT += -I$(RAYLIB_DIR)

$(CJSON_OBJECT): vendor/cjson/cJSON.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) -c $< -o $@

$(RAYLIB_LIB): $(RAYLIB_OBJECTS)
	ar rcs $@ $^

$(BUILD)/raylib/rglfw.o: $(RAYLIB_DIR)/rglfw.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) $(RAYLIB_DEFINES) -x objective-c -I$(RAYLIB_DIR)/external/glfw/include -c $< -o $@

$(BUILD)/raylib/%.o: $(RAYLIB_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) $(RAYLIB_DEFINES) -I$(RAYLIB_DIR)/external/glfw/include -c $< -o $@

ASAN_FLAGS := -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -g
ASAN_SEEDS := 0 1 2 3 4 5 6 7 8 9 4294967295

asan:
	$(MAKE) BUILD=build/asan SAN="$(ASAN_FLAGS)" test headless
	set -e; for seed in $(ASAN_SEEDS); do \
		build/asan/headless --seed $$seed --ticks 300 --dump build/asan/mid.json > /dev/null; \
		build/asan/headless --load build/asan/mid.json --ticks 300 > /dev/null; \
	done

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD)/obj -name "*.d" 2>/dev/null)
