name := bsnes-heat-fe

# Binaries are meant to run on machines other than this one. Setting local here
# is too late -- bsnes/GNUmakefile tests it before it includes this file -- and
# clang on arm64 rejects -march=native outright, so drop the flag instead.
local := false
flags := $(filter-out -march=native,$(flags))

# SDL3 builds through its own cmake, the same way gekkonet and weyvelength do.
sdl3.path := ../sdl
sdl3.build := ../sdl/build
sdl3.flags := -I$(sdl3.path)/include
sdl3.cmake.args := -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF
sdl3.cmake.args += -DCMAKE_BUILD_TYPE=Release
# nothing here opens a camera, and its macOS backend drags in AVFoundation and
# CoreMedia, which this hand-written framework list would otherwise have to track
sdl3.cmake.args += -DSDL_CAMERA=OFF
ifeq ($(platform),windows)
  sdl3.lib := $(sdl3.build)/libSDL3.a
  sdl3.cmake.args += -G "MinGW Makefiles"
  sdl3.options := -lopengl32 -lwinmm -limm32 -lversion -lsetupapi -lcfgmgr32 -ladvapi32
  sdl3.options += -loleaut32 -lgdi32 -luser32 -lkernel32 -lshell32 -luuid
else ifeq ($(platform),macos)
  sdl3.lib := $(sdl3.build)/libSDL3.a
  sdl3.options := -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
  sdl3.options += -framework CoreAudio -framework AudioToolbox -framework Carbon
  sdl3.options += -framework ForceFeedback -framework GameController -framework Metal
  sdl3.options += -framework QuartzCore -framework CoreHaptics -framework UniformTypeIdentifiers
else
  # X11, Wayland, ALSA and PulseAudio are dlopened by SDL at runtime, so only
  # the core system libraries need linking here
  sdl3.lib := $(sdl3.build)/libSDL3.a
  sdl3.options := -lGL -lm -ldl -lpthread -lrt
endif

# imgui ships no build files, so its sources are compiled straight into the target
imgui.path := ../imgui
imgui.flags := -I$(imgui.path) -I$(imgui.path)/backends

lua.path := ../lua
lua.flags := -I$(lua.path)

flags += $(sdl3.flags) $(imgui.flags) $(lua.flags)

# filter drives the CPU video filters; lzma unpacks .7z game archives
objects += ui-main ui-util ui-settings ui-shell ui-app ui-draw ui-inputmap ui-states
objects += ui-shader ui-stbimage
objects += ui-cheats ui-cheat-window
objects += ui-tests
objects += ui-scripting lua
objects += core-main core-media core-vfs core-video core-info core-memory
objects += ui-video ui-audio ui-input ui-hotkeys ui-emulator ui-enhance
objects += ui-custom ui-paths ui-windows ui-state-manager ui-manifest ui-compat
objects += imgui imgui-draw imgui-tables imgui-widgets imgui-sdl3 imgui-opengl3
ifeq ($(platform),windows)
  objects += ui-resource
endif
objects := $(objects:%=obj/%.o)

obj/ui-main.o: $(ui)/main.cpp
obj/ui-util.o: $(ui)/util.cpp
obj/ui-settings.o: $(ui)/settings.cpp
obj/ui-shell.o: $(ui)/shell.cpp
obj/ui-shader.o: $(ui)/shader.cpp
obj/ui-stbimage.o: $(ui)/stbimage.cpp
# CRT-Royale's mask LUTs are PNGs; SDL already vendors a decoder for its own use
obj/ui-stbimage.o: flags += -I$(sdl3.path)/src/video -Wno-unused-function
obj/ui-app.o: $(ui)/app.cpp
obj/ui-draw.o: $(ui)/ui/ui.cpp
obj/ui-inputmap.o: $(ui)/inputmap.cpp
obj/ui-states.o: $(ui)/states.cpp
obj/ui-cheats.o: $(ui)/cheats.cpp
obj/ui-cheat-window.o: $(ui)/ui/cheats.cpp
obj/ui-tests.o: $(ui)/tests.cpp
obj/ui-scripting.o: $(ui)/scripting.cpp
obj/lua.o: $(lua.path)/onelua.c
obj/lua.o: flags += -DMAKE_LIB $(if $(findstring g++,$(compiler)),-Wno-stringop-overflow)

$(lua.path)/onelua.c:
	git -C .. submodule update --init lua

obj/ui-scripting.o obj/lua.o: | $(lua.path)/onelua.c

obj/core-main.o: $(ui)/emucore/core.cpp
obj/core-media.o: $(ui)/emucore/media.cpp
obj/core-vfs.o: $(ui)/emucore/vfs.cpp
obj/core-video.o: $(ui)/emucore/video.cpp
obj/core-info.o: $(ui)/emucore/info.cpp
obj/core-memory.o: $(ui)/emucore/memory.cpp

obj/ui-video.o: $(ui)/ui/video.cpp
obj/ui-audio.o: $(ui)/ui/audio.cpp
obj/ui-input.o: $(ui)/ui/input.cpp
obj/ui-hotkeys.o: $(ui)/ui/hotkeys.cpp
obj/ui-emulator.o: $(ui)/ui/emulator.cpp
obj/ui-enhance.o: $(ui)/ui/enhance.cpp
obj/ui-custom.o: $(ui)/ui/custom.cpp
obj/ui-paths.o: $(ui)/ui/paths.cpp
obj/ui-windows.o: $(ui)/ui/windows.cpp
obj/ui-state-manager.o: $(ui)/ui/state-manager.cpp
obj/ui-manifest.o: $(ui)/ui/manifest.cpp
obj/ui-compat.o: $(ui)/ui/compat.cpp

obj/imgui.o: $(imgui.path)/imgui.cpp
obj/imgui-draw.o: $(imgui.path)/imgui_draw.cpp
obj/imgui-tables.o: $(imgui.path)/imgui_tables.cpp
obj/imgui-widgets.o: $(imgui.path)/imgui_widgets.cpp
obj/imgui-sdl3.o: $(imgui.path)/backends/imgui_impl_sdl3.cpp
obj/imgui-opengl3.o: $(imgui.path)/backends/imgui_impl_opengl3.cpp

# windows takes the window and taskbar icon from the exe's icon resource
obj/ui-resource.o: $(ui)/app.rc
	$(info Compiling $< ...)
	@$(windres) -I $(ui) $< $@

$(sdl3.lib):
ifeq ($(wildcard ../sdl/CMakeLists.txt),)
	git -C .. submodule update --init sdl imgui
endif
	cmake -B $(sdl3.build) -S $(sdl3.path) $(sdl3.cmake.args)
	+cmake --build $(sdl3.build) --target SDL3-static

# order-only prerequisite: SDL's headers must exist before anything includes them
$(objects): | $(sdl3.lib)

all: $(objects)
	$(info Linking out/$(name) ...)
	+@$(compiler) -o out/$(name) $(objects) $(sdl3.lib) $(sdl3.options) $(options)

verbose: nall.verbose all;
