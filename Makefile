CXX      ?= x86_64-w64-mingw32-g++
OUT      ?= dist/EWSL.exe

SRCS     := src/main.cpp src/terminal.cpp src/render.cpp src/wsl.cpp \
            src/fs.cpp src/lang.cpp src/catalog.cpp src/download.cpp \
            src/editor.cpp src/ui.cpp
HDRS     := src/terminal.h src/render.h src/wsl.h src/fs.h src/lang.h \
            src/catalog.h src/download.h src/editor.h src/ui.h

CPPFLAGS := -DUNICODE -D_UNICODE -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE
LDFLAGS  := -Wl,--subsystem,windows -static -static-libgcc -static-libstdc++
LDLIBS   := -lgdi32 -luser32 -lkernel32 -lgdiplus -ldwmapi -lole32 -lshell32 \
            -lwinhttp -ladvapi32

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRCS) $(HDRS)
	@mkdir -p $(dir $(OUT))
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf dist
