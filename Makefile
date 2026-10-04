CC := gcc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror
SRC_DIR := src
TARGET := visual-window-app

PURE_SOURCES := $(SRC_DIR)/geometry.c $(SRC_DIR)/layout.c $(SRC_DIR)/json.c \
                $(SRC_DIR)/layout_store.c
SDL_SOURCES := $(SRC_DIR)/main.c $(SRC_DIR)/window.c $(SRC_DIR)/renderer.c \
               $(SRC_DIR)/texture_cache.c $(SRC_DIR)/client_api.c \
               $(SRC_DIR)/http_client.c $(SRC_DIR)/app_config.c
SOURCES := $(SDL_SOURCES) $(PURE_SOURCES)

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)
LDLIBS := $(SDL_LIBS) -lSDL2_image -lm

.PHONY: all clean run test server web pattern acceptance

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

# 不依赖 SDL 的纯逻辑测试（三套坐标变换/布局/编辑会话/JSON）
test: $(SRC_DIR)/test_core
	./$(SRC_DIR)/test_core
	@node tools/geo_parity.js

$(SRC_DIR)/test_core: $(SRC_DIR)/test_core.c $(PURE_SOURCES)
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(SRC_DIR)/test_core.c $(PURE_SOURCES) -lm -o $@

run: $(TARGET)
	./$(TARGET)

server:
	python3 server/layout_server.py

pattern:
	python3 tools/make_test_pattern.py assets/background.png

# 端到端验收（需在容器内：Xvfb/xdotool/imagemagick/SDL）
acceptance: $(TARGET)
	python3 tools/acceptance.py

clean:
	rm -f $(TARGET) $(SRC_DIR)/test_core
