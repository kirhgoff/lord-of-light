CC := clang
BUILD := build
SAN :=
STD := -std=c11
STRICT := $(STD) -O2 -Wall -Wextra -Wpedantic -Wshadow -Wcast-align -Wconversion -Wsign-conversion -Werror -Ivendor/cjson $(SAN)
VENDOR_FLAGS := $(STD) -w -O2 $(SAN)

RAYLIB_DIR := vendor/raylib/src
RAYLIB_SOURCES := rcore rshapes rtextures rtext rmodels raudio utils
RAYLIB_DEFINES := -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33
RAYLIB_OBJECTS := $(RAYLIB_SOURCES:%=$(BUILD)/raylib/%.o) $(BUILD)/raylib/rglfw.o
RAYLIB_LIB := $(BUILD)/libraylib.a

ifeq ($(shell uname -s),Darwin)
TIDY_PLATFORM_FLAGS := -isysroot $(shell xcrun --show-sdk-path)
PLATFORM_LIBS := -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
RGLFW_LANGUAGE := objective-c
RGLFW_DEFINES :=
else
TIDY_PLATFORM_FLAGS :=
PLATFORM_LIBS := -lGL -lm -lpthread -ldl -lrt -lX11
RGLFW_LANGUAGE := c
RGLFW_DEFINES := -D_GLFW_X11
STRICT += -D_POSIX_C_SOURCE=200809L
VENDOR_FLAGS += -D_GNU_SOURCE
endif

CJSON_OBJECT := $(BUILD)/vendor/cjson.o
SIM_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/sim/*.c))
VIEW_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/view/*.c))
VIEW_PURE_OBJECTS := $(BUILD)/obj/src/view/atlas.o $(BUILD)/obj/src/view/scene.o $(BUILD)/obj/src/view/saves.o $(BUILD)/obj/src/view/sound_mix.o $(BUILD)/obj/src/view/report.o $(BUILD)/obj/src/view/frame_text.o
TEST_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard tests/*.c))
LAYA_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(wildcard src/laya/*.c))
SETUP_OBJECT := $(BUILD)/obj/src/decider_setup.o
GAME_OBJECTS := $(BUILD)/obj/src/main_game.o $(VIEW_OBJECTS) $(SIM_OBJECTS) $(LAYA_OBJECTS) $(SETUP_OBJECT) $(CJSON_OBJECT)
HEADLESS_OBJECTS := $(BUILD)/obj/src/main_headless.o $(SIM_OBJECTS) $(LAYA_OBJECTS) $(SETUP_OBJECT) $(CJSON_OBJECT)
BALANCE_OBJECTS := $(BUILD)/obj/src/main_balance.o $(SIM_OBJECTS) $(CJSON_OBJECT)

FORMAT_FILES := $(shell find src tests -name '*.c' -o -name '*.h')
CLANG_FORMAT := uv tool run --exclude-newer 2026-10-15 clang-format==22.1.8

TIDY_FILES := $(shell find src -name '*.c')
TIDY_VIEW_FILES := $(filter src/main_game.c src/view/%,$(TIDY_FILES))
TIDY_CORE_FILES := $(filter-out $(TIDY_VIEW_FILES),$(TIDY_FILES))
CLANG_TIDY := uv tool run --exclude-newer 2026-10-15 clang-tidy==22.1.8
TIDY_CORE_FLAGS := $(STRICT) $(TIDY_PLATFORM_FLAGS)
TIDY_VIEW_FLAGS := $(TIDY_CORE_FLAGS) $(RAYLIB_DEFINES) -I$(RAYLIB_DIR) -isystem vendor/clay

.PHONY: print-clang-format cppcheck tidy game headless test run clean asan balance screenshots laya-local format format-check

game: $(BUILD)/game
headless: $(BUILD)/headless

run: game
	$(BUILD)/game

test: $(BUILD)/tests
	$(BUILD)/tests

$(BUILD)/game: $(GAME_OBJECTS) $(RAYLIB_LIB)
	$(CC) $^ $(PLATFORM_LIBS) -lcurl -o $@

$(BUILD)/headless: $(HEADLESS_OBJECTS)
	$(CC) $^ $(SAN) -lm -lcurl -o $@

laya-local:
	LAYA_HOST=127.0.0.1 LAYA_PORT=8765 LAYA_DEVICE=cpu LAYA_THREADS=8 LAYA_MODELS=english LAYA_DEFAULT_MODEL=english \
		uv tool run --exclude-newer 2026-10-15 --python 3.12 --from 'laya[serve]==0.4.7' laya-serve

balance: $(BUILD)/balance
	$(BUILD)/balance

$(BUILD)/balance: $(BALANCE_OBJECTS)
	$(CC) $^ $(SAN) -lm -o $@

$(BUILD)/tests: $(TEST_OBJECTS) $(SIM_OBJECTS) $(VIEW_PURE_OBJECTS) $(BUILD)/obj/src/laya/laya.o $(CJSON_OBJECT)
	$(CC) $^ $(SAN) -lm -o $@

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(STRICT) -MMD -MP -c $< -o $@

$(BUILD)/obj/src/main_game.o $(filter-out $(VIEW_PURE_OBJECTS),$(VIEW_OBJECTS)): STRICT += -I$(RAYLIB_DIR)

$(BUILD)/obj/src/view/ui.o: STRICT += -isystem vendor/clay

$(CJSON_OBJECT): vendor/cjson/cJSON.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) -c $< -o $@

$(RAYLIB_LIB): $(RAYLIB_OBJECTS)
	ar rcs $@ $^

$(BUILD)/raylib/rglfw.o: $(RAYLIB_DIR)/rglfw.c
	@mkdir -p $(@D)
	$(CC) $(VENDOR_FLAGS) $(RAYLIB_DEFINES) -x $(RGLFW_LANGUAGE) $(RGLFW_DEFINES) -I$(RAYLIB_DIR)/external/glfw/include -c $< -o $@

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
		build/asan/headless --replay build/asan/mid.json > /dev/null; \
	done

SHOT_SEEDS := 1 2 3
SHOT_TICKS := 0 60 200 100000

screenshots: game
	mkdir -p $(BUILD)/shots
	set -e; for seed in $(SHOT_SEEDS); do for ticks in $(SHOT_TICKS); do \
		$(BUILD)/game --seed $$seed --ticks $$ticks --screenshot $(BUILD)/shots/s$${seed}_t$${ticks}.png; \
	done; done
	$(BUILD)/game --seed 1 --ticks 60 --xray --screenshot $(BUILD)/shots/s1_t60_xray.png
	$(BUILD)/game --seed 1 --ticks 60 --xray --select 0 --screenshot $(BUILD)/shots/s1_t60_xray_select0.png

print-clang-format:
	@echo $(CLANG_FORMAT)

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

tidy:
	$(CLANG_TIDY) $(TIDY_CORE_FILES) -- $(TIDY_CORE_FLAGS)
	$(CLANG_TIDY) $(TIDY_VIEW_FILES) -- $(TIDY_VIEW_FLAGS)

cppcheck:
	cppcheck --enable=warning,performance,portability --error-exitcode=1 --quiet --std=c11 -Ivendor/cjson --suppress=missingInclude '-DCLAY(...)=if (1)' src

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD)/obj -name "*.d" 2>/dev/null)
