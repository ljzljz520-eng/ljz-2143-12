CC ?= gcc
CSTD := -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE
SRC_DIR := src
TARGET := visual-window-app

# SDL2：优先 sdl2-config；无 root 环境可用 SDL_PREFIX=/tmp/local/usr
SDL_PREFIX ?=
ifneq ($(SDL_PREFIX),)
  SDL_CFLAGS := -I$(SDL_PREFIX)/include/aarch64-linux-gnu -I$(SDL_PREFIX)/include
  SDL_LIBDIR := $(SDL_PREFIX)/lib/aarch64-linux-gnu
  SDL_LIBS := -L$(SDL_LIBDIR) -lSDL2 -Wl,-rpath,$(SDL_LIBDIR) -Wl,--allow-shlib-undefined
else
  SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null) -lSDL2_image
  IMAGE_LIBS :=
endif

# 图片解码统一使用 libpng（不再需要 SDL2_image）
PNG_CFLAGS := $(shell pkg-config --cflags libpng16 2>/dev/null || pkg-config --cflags libpng 2>/dev/null)
PNG_LIBS := $(shell pkg-config --libs libpng16 2>/dev/null || pkg-config --libs libpng 2>/dev/null || echo -lpng16 -lz)
PTHREAD_LIBS := -lpthread

CORE_SOURCES := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/window.c \
	$(SRC_DIR)/scene.c \
	$(SRC_DIR)/geometry.c \
	$(SRC_DIR)/events.c \
	$(SRC_DIR)/texcache.c \
	$(SRC_DIR)/json_mini.c \
	$(SRC_DIR)/layout.c \
	$(SRC_DIR)/image_png.c \
	$(SRC_DIR)/http_client.c \
	$(SRC_DIR)/reporter.c \
	$(SRC_DIR)/sim.c \
	$(SRC_DIR)/rotate_blit.c

LIB_SOURCES := \
	$(SRC_DIR)/geometry.c \
	$(SRC_DIR)/events.c \
	$(SRC_DIR)/json_mini.c \
	$(SRC_DIR)/texcache.c \
	$(SRC_DIR)/layout.c

CFLAGS := $(CSTD) $(SDL_CFLAGS) $(PNG_CFLAGS)
LDLIBS := $(SDL_LIBS) $(PNG_LIBS) $(PTHREAD_LIBS) -lm

.PHONY: all clean run test unit e2e backend-test web-test

all: $(TARGET)

$(TARGET): $(CORE_SOURCES)
	$(CC) $(CFLAGS) $(CORE_SOURCES) -o $@ $(LDLIBS)

# ---- 单元测试（纯 C，无 SDL 依赖的几何/事件/JSON 模块 + 纹理缓存桩） ----
TEST_BIN := tests/unit/unit_tests
SDL_PC_CFLAGS := -I$(SDL_PREFIX)/include/aarch64-linux-gnu -I$(SDL_PREFIX)/include
SDL_PC_LIBS := -L$(SDL_PREFIX)/lib/aarch64-linux-gnu -lSDL2 -Wl,-rpath,$(SDL_PREFIX)/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined
unit:
	$(CC) $(CSTD) $(if $(SDL_PREFIX),$(SDL_PC_CFLAGS),$(shell sdl2-config --cflags)) -Itests -I$(SRC_DIR) $(PNG_CFLAGS) \
		tests/unit/test_main.c tests/unit/test_geometry.c tests/unit/test_events.c \
		tests/unit/test_json.c tests/unit/test_layout.c tests/unit/test_texcache.c \
		$(SRC_DIR)/geometry.c $(SRC_DIR)/events.c $(SRC_DIR)/json_mini.c \
		$(SRC_DIR)/layout.c $(SRC_DIR)/texcache.c \
		$(if $(SDL_PREFIX),$(SDL_PC_LIBS),$(shell sdl2-config --libs)) $(PNG_LIBS) -lm -o $(TEST_BIN)
	SDL_VIDEODRIVER=dummy $(TEST_BIN)

backend-test:
	python3 -m unittest discover -s server -p 'test_*.py' -v

web-test:
	node tests/unit/geometry.test.js
	node tests/unit/events.test.js

test: unit backend-test web-test e2e

e2e:
	python3 tests/e2e/e2e_runner.py

clean:
	rm -f $(TARGET) $(TEST_BIN)
