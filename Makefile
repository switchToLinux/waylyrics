# libwaylyrics.so 的编译

TARGET = libwaylyrics.so
BUILD_DIR = build
# DESTDIR = ~/.config/cffi/
DESTDIR = /apps/libs/

ALL = $(TARGET)

$(TARGET):
	@meson setup $(BUILD_DIR) -Dcpp_args=-DERROR_ENABLED
	@meson compile -C $(BUILD_DIR) waylyricsv2
	@echo "Build complete!"

debug:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) waylyricsv2
	@echo "Build complete!"

demo:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) demo

playerDemo:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) playerDemo
sigDemo:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) sigDemo

install:
	@if [ ! -d $(DESTDIR) ]; then \
		mkdir -p $(DESTDIR); \
	fi
	@if [ -f $(DESTDIR)/${TARGET} ]; then \
	    mv $(DESTDIR)/${TARGET} $(DESTDIR)/${TARGET}.bak; \
	fi
	cp $(BUILD_DIR)/${TARGET} $(DESTDIR)
	@echo "Install complete!"

clean:
	rm -f $(BUILD_DIR)/${TARGET}

purge:
	rm -rf $(BUILD_DIR)