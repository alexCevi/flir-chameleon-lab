CC = cc
CFLAGS = -O2 -Wall -Wextra

DC1394_PKG = PKG_CONFIG_PATH="$(HOME)/.local/libdc1394-usb/lib/pkgconfig:$(shell brew --prefix libusb)/lib/pkgconfig"

.PHONY: all live flea3 chameleon

all: flea3-live camera-live

flea3: flea3-live

chameleon: chameleon-live

FLEA3_SRCS = flea3-live.c flea3-view.c flea3-controls.c flea3-save.c flea3-input.c

flea3-live: $(FLEA3_SRCS)
	$(CC) $(CFLAGS) $(FLEA3_SRCS) -o flea3-live \
		$$(pkg-config --cflags --libs aravis-0.8) \
		$$(sdl2-config --cflags --libs)

camera-live: camera-live.c
	$(CC) $(CFLAGS) camera-live.c -o camera-live \
		$$(pkg-config --cflags --libs aravis-0.8)

chameleon-live: chameleon-live.c
	$(CC) $(CFLAGS) chameleon-live.c -o chameleon-live \
		$$($(DC1394_PKG) pkg-config --cflags --libs libdc1394-2) \
		$$(sdl2-config --cflags --libs)

live: flea3-live camera-live
	./camera-live
