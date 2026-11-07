# Builds the Linux dashboard (build/kitchensink) and runs the tests.
# Needs g++, SDL3, libcurl and pkg-config. Run from the repo root:
#   make -f linux/Makefile -j4
CXX ?= g++
PKGS := sdl3 libcurl
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wno-unused-parameter -Wno-missing-field-initializers -MMD -MP \
	-Isrc -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends_linux $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lpthread -ldl

OUT := build/linux
# The shared UI and model, and the Linux side. The Windows-only files
# (main.cpp, media.cpp, sysinfo.cpp, launcher.cpp, lick.cpp, grabber.cpp,
# upload.cpp, platform_win.cpp) are left out.
SRC := src/app.cpp src/cat.cpp src/config.cpp src/grab.cpp src/images.cpp src/local.cpp src/model.cpp \
	src/net.cpp src/retro.cpp src/sky.cpp src/sse.cpp src/theme.cpp src/weather_icon.cpp \
	linux/main_linux.cpp linux/http_curl.cpp linux/platform_linux.cpp linux/stubs.cpp \
	third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
	third_party/imgui/imgui_widgets.cpp \
	third_party/imgui/backends_linux/imgui_impl_sdl3.cpp third_party/imgui/backends_linux/imgui_impl_opengl3.cpp
OBJ := $(SRC:%.cpp=$(OUT)/%.o)

TEST_SRC := tests/tests.cpp src/model.cpp src/sse.cpp src/local.cpp src/grab.cpp src/cat.cpp src/weather_icon.cpp \
	src/retro.cpp src/sky.cpp third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp \
	third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
TEST_OBJ := $(TEST_SRC:%.cpp=$(OUT)/test/%.o)

all: build/kitchensink test

build/kitchensink: $(OBJ)
	$(CXX) -o $@ $^ $(LDLIBS)

$(OUT)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/test/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/tests-linux: $(TEST_OBJ)
	$(CXX) -o $@ $^ -lpthread

test: build/tests-linux
	./build/tests-linux

clean:
	rm -rf $(OUT) build/kitchensink build/tests-linux

.PHONY: all test clean
-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)
