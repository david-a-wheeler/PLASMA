# Build the ccish compiler and run its tests.
#
#   make        build ccish
#   make test   build ccish and plvm, then run the tests

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2

SRCS = src/main.c src/lex.c src/parse.c src/lower.c src/peep.c src/asm.c
PLVM = ../src/plvm

ccish: $(SRCS) src/ccish.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(PLVM):
	$(MAKE) -C ../src plvm

test: ccish $(PLVM)
	./tests/run.sh

clean:
	rm -f ccish
	rm -rf tests/out

.PHONY: test clean
