# libwaylyrics.so 的编译

TARGET = libwaylyrics.so
BUILD_DIR = build
# DESTDIR = ~/.config/cffi/
DESTDIR = /apps/libs/

ALL = $(TARGET)

$(TARGET):
	@meson setup $(BUILD_DIR) --prefix=$(DESTDIR)
	@meson compile -C $(BUILD_DIR)
	@if [ ! -d $(DESTDIR) ]; then \
		mkdir -p $(DESTDIR); \
	fi
	@if [ -f $(DESTDIR)/${TARGET} ]; then \
	    mv $(DESTDIR)/${TARGET} $(DESTDIR)/${TARGET}.bak; \
	fi
	@cp $(BUILD_DIR)/${TARGET} $(DESTDIR)
	@echo "Build complete!"

clean:
	rm -f $(BUILD_DIR)/${TARGET}

purge:
	rm -rf $(DESTDIR)