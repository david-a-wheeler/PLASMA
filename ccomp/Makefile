# Build the ccish compiler and run its tests.
#
#   make        build ccish, and its passes as programs of their own:
#               ccpp (the preprocessor), cccomp (the front end), ccgen
#               (the back end) and ccrel (the final pass)
#   make small  build ccpp-small, cccomp-small, ccgen-small and
#               ccrel-small: the small build (-DCCISH_SMALL), for the host
#   make test   build them all and plvm, refresh the cross-test
#               references (make xref), then run the tests
#   make xref   write the cross tests' reference output (tests/xrun/
#               NAME.ref) with a host C compiler, XCC (gcc or clang)
#   make measure  compile ccish's own source with ccish and link its
#               Apple II programs, reporting sizes (tools/measure.py)

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2
# The host C compiler that writes the cross tests' references: gcc by
# default, clang for "make test XCC=clang" (the flags are the same).
XCC ?= $(shell command -v gcc 2>/dev/null || command -v clang 2>/dev/null)
# -fwrapv makes signed overflow wrap to two's complement (as the 16-bit
# VM does) instead of undefined behavior, so the host oracle agrees with
# ccish on wrapping arithmetic; int is still a different width.
XREF_FLAGS = -Wall -Wextra -Werror -funsigned-char -fwrapv -O2

COMMON = src/diag.c src/bio.c
PP     = src/ppname.c src/pplex.c src/ppmac.c src/ppif.c src/ppout.c src/pp.c
NAM    = src/nam.c
FRONT  = src/tokin.c src/name.c src/type.c src/parse.c src/pexpr.c src/pstmt.c src/ptop.c src/irout.c
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

test: all small $(PLVM) xref
	./tests/run.sh

# The cross tests' references (tests/xrun): NAME.ref is NAME.c's
# stdout under XCC, LF only. Rewritten when NAME.c, inc/ptest.h or
# NAME.flags is newer (or NAME.ref is missing); an up-to-date set of
# references needs no compiler at all, so XCC is required only to
# regenerate. NAME.flags (optional) holds extra XCC flags for that
# one test: -Wno-... entries that admit valid-but-discouraged
# constructs (mixed precedence, a dead store) that ccish must still
# handle; the .c is compiled unchanged by both compilers.
xref:
	@mkdir -p tests/out
	@bad=0; \
	for src in tests/xrun/*.c; do \
	  n=$${src##*/}; n=$${n%.c}; \
	  ref="tests/xrun/$$n.ref"; \
	  if [ ! -f "$$ref" ] || [ "$$src" -nt "$$ref" ] \
	     || [ inc/ptest.h -nt "$$ref" ] \
	     || { [ -f "tests/xrun/$$n.flags" ] \
	         && [ "tests/xrun/$$n.flags" -nt "$$ref" ]; }; then \
	    if [ -z "$(XCC)" ]; then \
	      echo "xref: $$src: no gcc or clang to write $$ref"; bad=1; \
	      continue; \
	    fi; \
	    echo "xref: $$n (with $(XCC))"; \
	    extra=""; \
	    [ -f "tests/xrun/$$n.flags" ] && extra=$$(cat "tests/xrun/$$n.flags"); \
	    if ! "$(XCC)" $(XREF_FLAGS) $$extra -Iinc -o tests/out/xc_$n "$$src"; then \
	      bad=1; continue; \
	    fi; \
	    if ! timeout 10 tests/out/xc_$n > "$$ref"; then \
	      echo "xref: $$n: the $(XCC) run failed or hung"; bad=1; \
	    fi; \
	  fi; \
	done; \
	rm -f tests/out/xc_*; \
	[ "$$bad" -eq 0 ]

measure: all
	python3 tools/measure.py

clean:
	rm -f ccish ccpp cccomp ccgen ccrel
	rm -f ccpp-small cccomp-small ccgen-small ccrel-small
	rm -rf tests/out tools/out

.PHONY: all small test xref measure clean
