# Sample programs for ccish. Run make in ccish/ first, which builds the
# compiler and ../src/plvm.
#
#   make         the Apple II modules: rod, rodsimple and rodconio
#   make check   run test builds under plvm and compare their screens
#                with rodref, the original algorithm compiled with gcc
#   make disk    rods.po, a ProDOS disk of them and PLASMA's own ROD
#
# A module runs on an Apple II under PLASMA (copy it to a disk as a
# REL file); these draw on the real screen, so not under plvm, whose
# heap covers the screen's memory.

CCISH = ../ccish
CCPP  = ../ccpp
CCCOMP = ../cccomp
CCGEN = ../ccgen
PLVM  = ../../src/plvm
CC    = gcc
T     = test

# Rounds of w (3 to 50) each test runs: rod skips work across frames,
# so it runs more, over several wraps of w; rodsimple is slower.
ROD_ROUNDS = 20
SIMPLE_ROUNDS = 2

all: rod rodsimple rodconio

lores.obj: lores.c lores.h
	$(CCPP) -o lores lores.c && $(CCCOMP) lores && $(CCGEN) lores

rod: rod.c lores.h conio.h lores.obj
	$(CCISH) -o rod rod.c lores

rodsimple: rodsimple.c lores.h conio.h lores.obj
	$(CCISH) -o rodsimple rodsimple.c lores

rodconio: rodconio.c lores.h conio.h lores.obj
	$(CCISH) -o rodconio rodconio.c lores

$(T)/lores.obj: lores.c lores.h
	@mkdir -p $(T)
	$(CCPP) -DLORES_TEST -o $(T)/lores lores.c && $(CCCOMP) $(T)/lores \
	  && $(CCGEN) $(T)/lores

$(T)/rod: rod.c lores.h $(T)/lores.obj
	$(CCISH) -DROD_TEST=$(ROD_ROUNDS) -DROD_SUMS -o $@ rod.c $(T)/lores

$(T)/rodsimple: rodsimple.c lores.h $(T)/lores.obj
	$(CCISH) -DROD_TEST=$(SIMPLE_ROUNDS) -DROD_SUMS -o $@ rodsimple.c \
	  $(T)/lores

$(T)/rodconio: rodconio.c lores.h conio.h $(T)/lores.obj
	$(CCISH) -DROD_TEST=$(SIMPLE_ROUNDS) -DROD_SUMS -o $@ rodconio.c \
	  $(T)/lores

$(T)/rodref: rodref.c
	@mkdir -p $(T)
	$(CC) -O2 -Wall -o $@ rodref.c

# Each run's checksums must match rodref's for the same rounds.
check: $(T)/rod $(T)/rodsimple $(T)/rodconio $(T)/rodref
	cd $(T) && ../$(PLVM) rod | tr -d '\r' > rod.out
	$(T)/rodref $(ROD_ROUNDS) | cmp - $(T)/rod.out
	cd $(T) && ../$(PLVM) rodsimple | tr -d '\r' > rodsimple.out
	$(T)/rodref $(SIMPLE_ROUNDS) | cmp - $(T)/rodsimple.out
	cd $(T) && ../$(PLVM) rodconio | tr -d '\r' > rodconio.out
	$(T)/rodref $(SIMPLE_ROUNDS) | cmp - $(T)/rodconio.out
	@echo "rod, rodsimple and rodconio match rodref"

# A 140K ProDOS disk, rods.po (volume RODS), of Rod's Colors four
# ways, to run on an Apple II (or tools/a2run) beside a PLASMA boot
# disk, whose SYS/CONIO they all use: RODPLA, PLASMA's own (rod.pla, as
# built for PLASMA 2.20's demo disk), and ccish's ROD, RODSIMPLE and
# RODCONIO. Run one at PLASMA's prompt with +/RODS/ROD (and so on). AC
# is AppleCommander's command-line tool; PLASMA_DEMOS, the disk ROD is
# taken from.
AC = ../../bin/ac-linux-aarch64-15.2
PLASMA_DEMOS = ../../images/apple/PLEIADES-2.20-DEMOS.po
DISK = rods.po

disk: $(DISK)

$(DISK): rod rodsimple rodconio $(PLASMA_DEMOS)
	rm -f $@
	$(AC) -pro140 $@ RODS
	$(AC) -g $(PLASMA_DEMOS) DEMOS/ROD | $(AC) -p $@ RODPLA REL
	$(AC) -p $@ ROD REL < rod
	$(AC) -p $@ RODSIMPLE REL < rodsimple
	$(AC) -p $@ RODCONIO REL < rodconio
	$(AC) -l $@

clean:
	rm -rf $(T) rod rodsimple rodconio lores.obj lores.syms $(DISK)

.PHONY: all check clean disk
