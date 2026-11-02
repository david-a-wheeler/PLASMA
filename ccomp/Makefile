# Build the ccish compiler and run its tests.
#
#   make        build ccish, and its passes as programs of their own:
#               ccpp (the preprocessor), cccomp (the front end), ccgen
#               (the back end) and ccrel (the final pass)
#   make small  build ccpp-small, cccomp-small, ccgen-small and
#               ccrel-small: the small build (-DCCISH_SMALL), for the host
#   make test   build them all and plvm, then run the tests
#   make measure  compile ccish's own source with ccish and link its
#               Apple II programs, reporting sizes (tools/measure.py)

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2

COMMON = src/diag.c src/bio.c
PP     = src/ppname.c src/pplex.c src/ppmac.c src/ppif.c src/ppout.c src/pp.c
NAM    = src/nam.c
FRONT  = src/tokin.c src/name.c src/type.c src/parse.c src/ptop.c src/irout.c
BACK   = src/irin.c src/lower.c src/peep.c src/emit.c src/helper.c
LINK   = src/link.c
PLVM = ../src/plvm

HEADERS = $(wildcard src/*.h)

all: ccish ccpp cccomp ccgen ccrel

ccish: src/main.c $(COMMON) $(PP) $(NAM) $(FRONT) $(BACK) $(LINK) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/main.c $(COMMON) $(PP) $(NAM) $(FRONT) $(BACK) $(LINK)

ccpp: src/ccpp.c $(COMMON) $(PP) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/ccpp.c $(COMMON) $(PP)

cccomp: src/cccomp.c $(COMMON) $(NAM) $(FRONT) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/cccomp.c $(COMMON) $(NAM) $(FRONT)

ccgen: src/ccgen.c $(COMMON) $(NAM) $(BACK) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/ccgen.c $(COMMON) $(NAM) $(BACK)

ccrel: src/ccrel.c $(COMMON) $(LINK) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/ccrel.c $(COMMON) $(LINK)

# The small build (approach.md decision 21), built for the host so the
# tests can check that it writes the same objects and modules.
small: ccpp-small cccomp-small ccgen-small ccrel-small

ccpp-small: src/ccpp.c $(COMMON) $(PP) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/ccpp.c $(COMMON) $(PP)

cccomp-small: src/cccomp.c $(COMMON) $(NAM) $(FRONT) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/cccomp.c $(COMMON) $(NAM) $(FRONT)

ccgen-small: src/ccgen.c $(COMMON) $(NAM) $(BACK) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/ccgen.c $(COMMON) $(NAM) $(BACK)

ccrel-small: src/ccrel.c $(COMMON) $(LINK) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/ccrel.c $(COMMON) $(LINK)

$(PLVM):
	$(MAKE) -C ../src plvm

test: all small $(PLVM)
	./tests/run.sh

measure: all
	python3 tools/measure.py

clean:
	rm -f ccish ccpp cccomp ccgen ccrel
	rm -f ccpp-small cccomp-small ccgen-small ccrel-small
	rm -rf tests/out tools/out

.PHONY: all small test measure clean
