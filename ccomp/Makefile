# Build the ccish compiler and run its tests.
#
#   make        build ccish, and its two passes as programs of their own:
#               cccomp (the compile pass) and cclink (the final pass)
#   make test   build them and plvm, then run the tests

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2

COMMON  = src/diag.c src/bio.c
COMPILE = src/lex.c src/name.c src/type.c src/parse.c src/lower.c \
          src/peep.c src/emit.c
LINK    = src/link.c
PLVM = ../src/plvm

all: ccish cccomp cclink

ccish: src/main.c $(COMMON) $(COMPILE) $(LINK) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/main.c $(COMMON) $(COMPILE) $(LINK)

cccomp: src/cccomp.c $(COMMON) $(COMPILE) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/cccomp.c $(COMMON) $(COMPILE)

cclink: src/cclink.c $(COMMON) $(LINK) src/ccish.h
	$(CC) $(CFLAGS) -o $@ src/cclink.c $(COMMON) $(LINK)

$(PLVM):
	$(MAKE) -C ../src plvm

test: all $(PLVM)
	./tests/run.sh

clean:
	rm -f ccish cccomp cclink
	rm -rf tests/out

.PHONY: all test clean
