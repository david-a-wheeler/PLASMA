# Build the ccish compiler and run its tests.
#
#   make        build ccish, and its passes as programs of their own:
#               cccomp (the front end), ccgen (the back end) and ccrel
#               (the final pass)
#   make small  build cccomp-small, ccgen-small and ccrel-small: the
#               small build (-DCCISH_SMALL), for the host
#   make test   build them all and plvm, then run the tests
#   make measure  compile ccish's own source with ccish and link its
#               Apple II programs, reporting sizes (tools/measure.py)

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2

COMMON = src/diag.c src/bio.c
FRONT  = src/lex.c src/name.c src/type.c src/parse.c src/irout.c
BACK   = src/irin.c src/lower.c src/peep.c src/emit.c
LINK   = src/link.c
PLVM = ../src/plvm

all: ccish cccomp ccgen ccrel

ccish: src/main.c $(COMMON) $(FRONT) $(BACK) $(LINK) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/main.c $(COMMON) $(FRONT) $(BACK) $(LINK)

cccomp: src/cccomp.c $(COMMON) $(FRONT) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/cccomp.c $(COMMON) $(FRONT)

ccgen: src/ccgen.c $(COMMON) $(BACK) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/ccgen.c $(COMMON) $(BACK)

ccrel: src/ccrel.c $(COMMON) $(LINK) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/ccrel.c $(COMMON) $(LINK)

# The small build (approach.md decision 21), built for the host so the
# tests can check that it writes the same objects and modules.
small: cccomp-small ccgen-small ccrel-small

cccomp-small: src/cccomp.c $(COMMON) $(FRONT) src/ccish.h
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/cccomp.c $(COMMON) $(FRONT)

ccgen-small: src/ccgen.c $(COMMON) $(BACK) src/ccish.h
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/ccgen.c $(COMMON) $(BACK)

ccrel-small: src/ccrel.c $(COMMON) $(LINK) src/ccish.h
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ src/ccrel.c $(COMMON) $(LINK)

$(PLVM):
	$(MAKE) -C ../src plvm

test: all small $(PLVM)
	./tests/run.sh

measure: all
	python3 tools/measure.py

clean:
	rm -f ccish cccomp ccgen ccrel cccomp-small ccgen-small ccrel-small
	rm -rf tests/out tools/out

.PHONY: all small test measure clean
