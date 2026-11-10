# Sample programs for ccish. Run make in ccish/ first, which builds the
# compiler.
#
#   make         the Apple II modules: rod (the plain one) and rodfast
#   make disk    rods.po, a ProDOS disk of them and PLASMA's own rod
#
# A module runs on an Apple II under PLASMA (copy it to a disk as a
# REL file, as "make disk" does). These draw on the real screen, so
# they don't run under plvm, whose heap covers the screen's memory;
# use an Apple II or tools/a2run.

CCISH = ../ccish
CCPP  = ../ccpp
CCCOMP = ../cccomp
CCGEN = ../ccgen

all: rod rodfast

lores.obj: lores.c lores.h
	$(CCPP) -o lores lores.c && $(CCCOMP) lores && $(CCGEN) lores

rodfast: rodfast.c lores.h conio.h lores.obj
	$(CCISH) -o rodfast rodfast.c lores

rod: rod.c conio.h
	$(CCISH) -o rod rod.c

# A 140K ProDOS disk, rods.po (volume RODS), of Rod's Colors three
# ways, to run on an Apple II (or tools/a2run) beside a PLASMA boot
# disk, whose SYS/CONIO they all use: RODPLA, PLASMA's own (rod.pla, as
# built for PLASMA 2.20's demo disk), and ccish's ROD and RODFAST.
# Run one at PLASMA's prompt with +/RODS/ROD (and so on). AC is
# AppleCommander's command-line tool; PLASMA_DEMOS, the disk RODPLA is
# taken from (as DEMOS/ROD).
AC = ../../bin/ac-linux-aarch64-15.2
PLASMA_DEMOS = ../../images/apple/PLEIADES-2.20-DEMOS.po
DISK = rods.po

disk: $(DISK)

$(DISK): rod rodfast $(PLASMA_DEMOS)
	rm -f $@
	$(AC) -pro140 $@ RODS
	$(AC) -g $(PLASMA_DEMOS) DEMOS/ROD | $(AC) -p $@ RODPLA REL
	$(AC) -p $@ ROD REL < rod
	$(AC) -p $@ RODFAST REL < rodfast
	$(AC) -l $@

clean:
	rm -rf rod rodfast lores.obj lores.syms $(DISK)

.PHONY: all clean disk
