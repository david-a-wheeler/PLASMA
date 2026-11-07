# Build the ccish compiler and run its tests.
#
#   make        build everything the tests need, and run the stale tests
#   make all    build ccish and its passes (ccpp, cccomp, ccgen, ccrel)
#   make small  build the small passes (-DCCISH_SMALL), for the host
#   make xref   write the cross tests' references with a host C compiler
#
# The tests are driven entirely by this Makefile. Each test kind is a
# directory of inputs; a wildcard finds the inputs, a % rule runs one of
# them, and the rule's target is a stamp file that exists only while the
# test passes. A stamp depends on its input, its expected result, the
# passes, and any script that runs it, so a change to any of those
# re-runs the test (no stale results). Adding a test is adding an input
# file (and its expected result); nothing here changes. The one-off
# checks that don't fit "input -> expected -> diff" are the scripts
# under tests/spec/.

CC ?= cc
CFLAGS ?= -std=c89 -pedantic -Wall -Wextra -O2
# The host C compiler that writes the cross tests' references (make xref).
XCC ?= $(shell command -v gcc 2>/dev/null || command -v clang 2>/dev/null)
XREF_FLAGS = -Wall -Wextra -Werror -funsigned-char -fwrapv -O2

.DEFAULT_GOAL := test

# The sources of the passes, and the headers they include.
COMMON = src/diag.c src/bio.c
PP     = src/ppname.c src/pplex.c src/ppmac.c src/ppif.c src/ppout.c src/pp.c
NAM    = src/nam.c
FRONT  = src/tokin.c src/name.c src/type.c src/parse.c src/pexpr.c \
         src/pstmt.c src/ptop.c src/irout.c
BACK   = src/irin.c src/lower.c src/peep.c src/emit.c src/helper.c
LINK   = src/link.c
HEADERS= $(wildcard src/*.h)
PLVM   = ../src/plvm
PLVMBIN= $(abspath $(PLVM))
# The 6502 VM that runs the compiler-bootstrap stages 2 and 3
# (bootstrap, below).
BOOTSTRAP_VM ?= $(PLVMBIN)
OUT    = tests/out
# PLASMA's cross-compiler, and the assembler it writes for, which build
# the ccstat driver that tools/ccrun runs a module through to get its
# exit status (approach.md issue 22).
XPLASM = ../src/xplasm
ACME  ?= acme
CCSTAT = $(OUT)/ccstat
CCRUN  = CCSTAT=$(abspath $(CCSTAT)) PLVM=$(PLVMBIN) $(abspath tools/ccrun)

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

# The small build (approach.md decision 21), for the host: the tests check
# that it writes the same objects and modules as the full build.
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

$(XPLASM):
	$(MAKE) -C ../src xplasm

$(CCSTAT): tools/ccstat.pla ../src/inc/cmdsys.plh $(XPLASM)
	@mkdir -p $(dir $@)
	$(XPLASM) -AMOWS tools/ccstat.pla > $@.a
	$(ACME) --setpc 4094 -o $@ $@.a

# The passes a test may run; a stamp depends on these, so a rebuilt pass
# re-runs the tests.
PASSES := ccish ccpp cccomp ccgen ccrel \
          ccpp-small cccomp-small ccgen-small ccrel-small $(PLVM) \
          $(CCSTAT) tools/ccrun

# --- The test kinds. Each is a directory of inputs; the stamp for input
# tests/<kind>/<stem> is tests/out/<kind>/<stem>.stamp.

# run: compile, run under plvm (through tools/ccrun); stdout must equal
# the .expected, the exit status the number in .status (or 0 if there
# is none), the stderr .stderr.expected (or be empty if there is none),
# and the source's listing directives (tests/check_listing.sh) must
# hold.
RUN := $(wildcard tests/run/*.c)
$(OUT)/run/%.stamp: tests/run/%.c tests/run/%.expected \
                    $(wildcard tests/run/*.stderr.expected) \
                    $(wildcard tests/run/*.status) \
                    $(PASSES) tests/check_listing.sh
	@mkdir -p $(dir $@)
	@./ccish -l -o $(dir $@)$* tests/run/$*.c >$(dir $@)$*.lst 2>$(dir $@)$*.stderr
	@(cd $(dir $@) && { $(CCRUN) -t 10 $(notdir $*) 2>&1; echo $$? > $(notdir $*).status; } | tr -d '\r' > $*.actual)
	@diff -u tests/run/$*.expected $(dir $@)$*.actual
	@want=$$(cat tests/run/$*.status 2>/dev/null || echo 0); got=$$(cat $(dir $@)$*.status); \
	 [ "$$got" = "$$want" ] || { echo "run $*: exit status $$got, expected $$want"; exit 1; }
	@if [ -f tests/run/$*.stderr.expected ]; then diff -u tests/run/$*.stderr.expected $(dir $@)$*.stderr; elif [ -s $(dir $@)$*.stderr ]; then echo "run: unexpected stderr"; cat $(dir $@)$*.stderr; exit 1; fi
	@tests/check_listing.sh tests/run/$*.c $(dir $@)$*.lst
	@touch $@

# gen: ccish -d (machine ops after the peephole stage) must equal the
# .exp, and the module must run under plvm (through tools/ccrun, so a
# crash or timeout fails; the exit status is the program's business).
GEN := $(wildcard tests/gen/*.c)
$(OUT)/gen/%.stamp: tests/gen/%.c tests/gen/%.exp $(PASSES)
	@mkdir -p $(dir $@)
	@./ccish -d -o $(dir $@)$* tests/gen/$*.c >$@.actual 2>$@.err
	@test ! -s $@.err || { echo "gen: unexpected messages"; cat $@.err; exit 1; }
	@diff -u tests/gen/$*.exp $@.actual
	@(cd $(dir $@) && $(CCRUN) -t 10 $(notdir $*) >/dev/null 2>&1; s=$$?; \
	  [ $$s -lt 124 ] || { echo "gen $*: plvm status $$s"; exit 1; })
	@touch $@

# xrun: the same source must print the same under ccish+plvm as under
# the host oracle, whose output make xref cached in the .ref, and exit
# with status 0, as the oracle must (make xref fails otherwise).
XRUN := $(wildcard tests/xrun/*.c)
$(OUT)/xrun/%.stamp: tests/xrun/%.c tests/xrun/%.ref $(PASSES)
	@mkdir -p $(dir $@)
	@./ccish -I inc -o $(dir $@)$* tests/xrun/$*.c 2>$(dir $@)$*.err
	@test ! -s $(dir $@)$*.err || { echo "xrun: unexpected messages"; cat $(dir $@)$*.err; exit 1; }
	@(cd $(dir $@) && { $(CCRUN) -t 10 $(notdir $*) 2>&1; echo $$? > $(notdir $*).status; } | tr -d '\r' > $*.actual)
	@diff -u tests/xrun/$*.ref $(dir $@)$*.actual
	@got=$$(cat $(dir $@)$*.status); [ "$$got" = 0 ] || { echo "xrun $*: exit status $$got"; exit 1; }
	@touch $@

# errors: must fail to compile, and the message (stderr) must contain
# the .stderr.expected pattern.
ERR := $(wildcard tests/errors/*.c)
$(OUT)/err/%.stamp: tests/errors/%.c tests/errors/%.stderr.expected $(PASSES)
	@mkdir -p $(dir $@)
	@! ./ccish -o $(dir $@)$* tests/errors/$*.c 2>$@.actual
	@grep -qF -f tests/errors/$*.stderr.expected $@.actual
	@touch $@

# pp and link are specialized (a checksum; several inputs per test): a
# small script does the compare, the rule keeps the dependencies clear.

# pp: ccpp's token stream and name file must equal the .tok and the
# .nam, with the checksum filled in for the "ssss" placeholders.
PP_TESTS := $(wildcard tests/pp/*.c)
$(OUT)/pp/%.stamp: tests/pp/%.c tests/pp/%.nam tests/pp/%.tok \
                   ccpp tests/pp_test.sh tests/lib.sh
	@mkdir -p $(dir $@) && tests/pp_test.sh $* && touch $@

# link: compile the directory's .c files (name order) and link them;
# with "expected" the module's output must equal it, with "expect-error"
# the link must fail with its message.
LINK_TESTS := $(wildcard tests/link/*/expected tests/link/*/expect-error)
LINK_STAMPS := $(patsubst tests/link/%/expected,$(OUT)/link/%.stamp,$(LINK_TESTS)) \
               $(patsubst tests/link/%/expect-error,$(OUT)/link/%.stamp,$(LINK_TESTS))
$(OUT)/link/%.stamp: $(wildcard tests/link/*/*.c) $(LINK_TESTS) $(PASSES) \
                     tests/link_test.sh tests/lib.sh tests/check_listing.sh
	@mkdir -p $(dir $@) && tests/link_test.sh $* && touch $@

# spec: the specialized checks, each a self-contained script that exits
# nonzero on failure; the ones that read test inputs or ccish.msg depend
# on them too. The bootstrap test (below) is a spec script but is kept
# out of SPEC, so the regular suite does not run it.
SPEC := $(filter-out tests/spec/bootstrap.sh,$(wildcard tests/spec/*.sh))
$(OUT)/spec/%.stamp: tests/spec/%.sh tests/lib.sh $(PASSES)
	@mkdir -p $(dir $@) && sh $< && touch $@

$(OUT)/spec/small.stamp: $(wildcard tests/run/*.c tests/xrun/*.c \
                         tests/gen/*.c tests/link/*/*.c)
$(OUT)/spec/front.stamp: tests/run/hello.c tests/errors/const_assign.c
$(OUT)/spec/back.stamp: tests/run/hello.c tests/run/global_init.c
$(OUT)/spec/ccrel.stamp: tests/run/hello.c tests/run/global_init.c \
                         tests/link/helpers/a.c tests/link/helpers/b.c
$(OUT)/spec/msgs.stamp: ccish.msg

# The compiler bootstrap test: the seed, this source's $(CC) build,
# compiles the compiler's source into a VM module (stage 1); stage 1,
# run under $(BOOTSTRAP_VM), compiles the same source into stage 2;
# stage 2 does it again into stage 3; and the test is that stage 2 and
# stage 3 are byte-for-byte the same module. It's its own target, not
# in the regular suite (full-test runs both), because it's slow. The
# script is being rewritten (doc/bootstrap-test.md, R1); until then it
# fails.
$(OUT)/spec/bootstrap.stamp: tests/spec/bootstrap.sh $(PASSES) tests/lib.sh
	@mkdir -p $(dir $@) && BOOTSTRAP_VM="$(BOOTSTRAP_VM)" sh $< && touch $@

bootstrap: $(OUT)/spec/bootstrap.stamp
full-test: test $(OUT)/spec/bootstrap.stamp

# Everything the tests need, built but not run: the passes, the small
# passes, the VM, and the cross references. The test stamps hang off this,
# so nothing runs against a stale pass.
testbuild: all small $(PLVM) $(CCSTAT) xref

# The whole suite: a stamp for every test, then one that depends on all
# of them and reports the count (only when something actually ran).
ALL_STAMPS := $(patsubst tests/run/%.c,$(OUT)/run/%.stamp,$(RUN)) \
              $(patsubst tests/gen/%.c,$(OUT)/gen/%.stamp,$(GEN)) \
              $(patsubst tests/xrun/%.c,$(OUT)/xrun/%.stamp,$(XRUN)) \
              $(patsubst tests/errors/%.c,$(OUT)/err/%.stamp,$(ERR)) \
              $(patsubst tests/pp/%.c,$(OUT)/pp/%.stamp,$(PP_TESTS)) \
              $(LINK_STAMPS) \
              $(patsubst tests/spec/%.sh,$(OUT)/spec/%.stamp,$(SPEC))

test: testbuild $(ALL_STAMPS) $(OUT)/ALL.stamp

$(OUT)/ALL.stamp: $(ALL_STAMPS)
	@echo "passed: $$(ls $(OUT)/*/*.stamp 2>/dev/null | grep -v ALL.stamp | wc -l) tests"
	@touch $@

# The cross tests' references: tests/xrun/NAME.ref is NAME.c's stdout
# under XCC, LF only; NAME.flags (possibly empty) holds extra XCC flags
# for that one test. Make regenerates a .ref when its .c, its .flags or
# inc/ptest.h is newer, so nothing runs against a stale reference.
XRUN_REF := $(patsubst tests/xrun/%.c,tests/xrun/%.ref,$(XRUN))
xref: $(XRUN_REF)

tests/xrun/%.ref: tests/xrun/%.c tests/xrun/%.flags inc/ptest.h
	@test -n "$(XCC)" || { echo "xref: no gcc or clang found"; exit 1; }
	@"$(XCC)" $(XREF_FLAGS) $(shell cat tests/xrun/$*.flags) -Iinc -o $@.bin $< \
	 && timeout 10 $@.bin > $@ && rm -f $@.bin

measure: all
	python3 tools/measure.py

clean:
	rm -f ccish ccpp cccomp ccgen ccrel
	rm -f ccpp-small cccomp-small ccgen-small ccrel-small
	rm -rf $(OUT) tools/out bootstrap

.PHONY: all small test testbuild xref measure bootstrap full-test clean
