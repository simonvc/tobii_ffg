CC ?= cc
CFLAGS ?= -O2 -Wall
BINDIR ?= $(HOME)/.local/bin

all: tobii_ffg tobii_gaze tobii_focus

# Combined "focus follows gaze" tool (config in ~/.config/tobii_ffg).
tobii_ffg: src/tobii_ffg.c src/init_seq.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/tobii_ffg.c -lusb-1.0

# Standalone pieces (gaze reader + Hyprland focus daemon), composable via a pipe.
tobii_gaze: src/tobii_gaze.c src/init_seq.h
	$(CC) $(CFLAGS) -Isrc -o $@ src/tobii_gaze.c -lusb-1.0

tobii_focus: src/tobii_focus.c
	$(CC) $(CFLAGS) -o $@ src/tobii_focus.c

# Allow non-root access to the tracker.
install-udev:
	echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="2104", ATTRS{idProduct}=="0313", MODE="0666"' \
		| sudo tee /etc/udev/rules.d/99-tobii5.rules
	sudo udevadm control --reload && sudo udevadm trigger

install: tobii_ffg
	install -Dm755 tobii_ffg $(BINDIR)/tobii_ffg

clean:
	rm -f tobii_ffg tobii_gaze tobii_focus

.PHONY: all install install-udev clean
