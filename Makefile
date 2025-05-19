# libwaylyrics.so 的编译

TARGET = libwaylyrics.so
BUILD_DIR = build
DESTDIR = ~/.config/cffi/
# DESTDIR = /apps/libs/

ALL = $(TARGET)

$(TARGET):
	meson setup $(BUILD_DIR) --prefix=$(DESTDIR)
	meson compile -C $(BUILD_DIR)
	if [ ! -d $(DESTDIR) ]; then \
		mkdir -p $(DESTDIR); \
	fi
	cp $(BUILD_DIR)/${TARGET} $(DESTDIR)
		

clean:
	rm -f $(BUILD_DIR)/${TARGET}

purge:
	rm -rf $(DESTDIR)