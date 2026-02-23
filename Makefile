# Makefile for RetroGun
#
# Targets
# -------
#   make          - Cross-compile on Linux using DJGPP (produces RETROGN.EXE)
#   make native   - Compile on DOS with DJGPP natively
#   make clean    - Remove build artefacts
#
# Prerequisites (Linux cross-compile)
#   - DJGPP cross-compiler:  i586-pc-msdosdjgpp-gcc
#     Ubuntu/Debian: sudo apt-get install gcc-djgpp
#     Or build from source: https://github.com/andrewwutw/build-djgpp
#
# Prerequisites (DOS native)
#   - DJGPP 2.x installed on DOS/DOSBox
#   - CWSDPMI.EXE (bundled with DJGPP, or from https://sandmann.de/cwsdpmi/)

# ---- Cross-compile settings -----------------------------------------------
CROSS_CC  = i586-pc-msdosdjgpp-gcc
CFLAGS    = -O2 -Wall -Wextra -std=gnu99

SRC       = src/RETROGN.C
TARGET    = RETROGN.EXE

.PHONY: all native clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CROSS_CC) $(CFLAGS) -o $@ $<
	@echo "Built $@ (DOS executable)"
	@echo "Copy RETROGN.EXE and CWSDPMI.EXE to a DOS system or DOSBox to run."

# ---- Native DOS build (run inside DOS / DOSBox) ----------------------------
native:
	gcc $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
