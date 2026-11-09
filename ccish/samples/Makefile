# Sample programs for ccish. Run make in ccish/ first, which builds the
# compiler and ../src/plvm.
#
#   make         the Apple II modules: rod and rodsimple
#   make check   run test builds under plvm and compare their screens
#                with rodref, the original algorithm compiled with gcc
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

all: rod rodsimple

lores.obj: lores.c lores.h
	$(CCPP) -o lores lores.c && $(CCCOMP) lores && $(CCGEN) lores

rod: rod.c lores.h conio.h lores.obj
	$(CCISH) -o rod rod.c lores

rodsimple: rodsimple.c lores.h conio.h lores.obj
	$(CCISH) -o rodsimple rodsimple.c lores

$(T)/lores.obj: lores.c lores.h
	@mkdir -p $(T)
	$(CCPP) -DLORES_TEST -o $(T)/lores lores.c && $(CCCOMP) $(T)/lores \
	  && $(CCGEN) $(T)/lores

$(T)/rod: rod.c lores.h $(T)/lores.obj
	$(CCISH) -DROD_TEST=$(ROD_ROUNDS) -DROD_SUMS -o $@ rod.c $(T)/lores

$(T)/rodsimple: rodsimple.c lores.h $(T)/lores.obj
	$(CCISH) -DROD_TEST=$(SIMPLE_ROUNDS) -DROD_SUMS -o $@ rodsimple.c \
	  $(T)/lores

$(T)/rodref: rodref.c
	@mkdir -p $(T)
	$(CC) -O2 -Wall -o $@ rodref.c

# Each run's checksums must match rodref's for the same rounds.
check: $(T)/rod $(T)/rodsimple $(T)/rodref
	cd $(T) && ../$(PLVM) rod | tr -d '\r' > rod.out
	$(T)/rodref $(ROD_ROUNDS) | cmp - $(T)/rod.out
	cd $(T) && ../$(PLVM) rodsimple | tr -d '\r' > rodsimple.out
	$(T)/rodref $(SIMPLE_ROUNDS) | cmp - $(T)/rodsimple.out
	@echo "rod and rodsimple match rodref"

clean:
	rm -rf $(T) rod rodsimple lores.obj lores.syms

.PHONY: all check clean
