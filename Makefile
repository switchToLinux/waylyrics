# libwaylyrics.so 的编译

TARGET = libwaylyrics.so
BUILD_DIR = build

# 通过参数设置 DESTDIR
# 例如：make install DESTDIR=/apps/libs/
DESTDIR = ~/.config/cffi/

ALL = $(TARGET)

$(TARGET):
	@meson setup $(BUILD_DIR)
	@meson compile -C $(BUILD_DIR) waylyrics
	@echo "Build complete!"

debug:
	@meson setup $(BUILD_DIR) -Dcpp_args=-DDEBUG_ENABLED
	@meson compile -C $(BUILD_DIR) waylyrics
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