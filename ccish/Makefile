# Build the ccish compiler and run its tests.
# Requires GNU Make
#
#   make        build everything the tests need, and run the stale tests
#   make all    build ccish and its passes (ccpp, cccomp, ccgen, ccrel),
#               and doc/limits.md
#   make small  build the small passes (-DCCISH_SMALL), for the host
#   make xref   write the cross tests' references with a host C compiler
#   make limits regenerate doc/limits.md, the table of ccish's limits
#   make measure regenerate doc/measure.txt, ccish's sizes and peaks
#               measured on its own source, if stale, and print it
#   make demo-disk  samples/rods.po, a ProDOS disk of Rod's Colors three
#               ways: PLASMA's rod.pla, and ccish's rod.c and rodfast.c
#   make demo-run   boot PLASMA in an emulated 64K Apple IIe with that
#               disk in the second drive (samples/Makefile has the
#               variables for another disk tool or emulator)
#
# The tests are driven entirely by this Makefile. Each test kind is a
# directory of inputs; a wildcard finds the inputs, a % rule runs one of
# them, and the rule's target is a stamp file that exists only while the
# test passes. A stamp depends on its input, its expected result, the
# passes, and any script that runs it, so a change to any of those
# re-runs the test (no stale results). Adding a test is adding an input
# file (and its expected result); nothing here changes. The one-off
# checks that don't fit "input -> expected -> diff" are the scripts
# under tests/special/.

CC ?= cc
CFLAGS ?= -Wall -Wextra -O2
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
BACK   = src/irin.c src/irloop.c src/lower.c src/peep.c src/emit.c src/helper.c
LINK   = src/link.c
# Each Apple II program's sources, in link order (also the bootstrap's,
# below).
CCPP_SRC   = src/ccpp.c $(COMMON) $(PP)
CCCOMP_SRC = src/cccomp.c $(COMMON) $(NAM) $(FRONT)
CCGEN_SRC  = src/ccgen.c $(COMMON) $(NAM) $(BACK)
CCREL_SRC  = src/ccrel.c $(COMMON) $(LINK)
HEADERS := $(wildcard src/*.h)
SOURCES := $(wildcard src/*.h src/*.c)
# The library's headers: ccpp's default include directory, compiled in
# for the host (on the VM it's <syspath>ccinc; src/pp.c).
CCINC  = -DCCISH_INC='"$(abspath lib/include)"'
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

all: ccish ccpp cccomp ccgen ccrel doc/limits.md

ccish: src/main.c $(COMMON) $(PP) $(NAM) $(FRONT) $(BACK) $(LINK) $(HEADERS)
	$(CC) $(CFLAGS) $(CCINC) -o $@ src/main.c $(COMMON) $(PP) $(NAM) $(FRONT) $(BACK) $(LINK)

ccpp: $(CCPP_SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(CCINC) -o $@ $(CCPP_SRC)

cccomp: $(CCCOMP_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(CCCOMP_SRC)

ccgen: $(CCGEN_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(CCGEN_SRC)

ccrel: $(CCREL_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(CCREL_SRC)

# The small build (approach.md decision 21), for the host: the tests check
# that it writes the same objects and modules as the full build.
small: ccpp-small cccomp-small ccgen-small ccrel-small

ccpp-small: $(CCPP_SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(CCINC) -DCCISH_SMALL -o $@ $(CCPP_SRC)

cccomp-small: $(CCCOMP_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ $(CCCOMP_SRC)

ccgen-small: $(CCGEN_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ $(CCGEN_SRC)

ccrel-small: $(CCREL_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -DCCISH_SMALL -o $@ $(CCREL_SRC)

$(PLVM):
	$(MAKE) -C ../src plvm

$(XPLASM):
	$(MAKE) -C ../src xplasm

$(CCSTAT): tools/ccstat.pla ../src/inc/cmdsys.plh $(XPLASM)
	@mkdir -p $(dir $@)
	$(XPLASM) -AMOWS tools/ccstat.pla > $@.a
	$(ACME) --setpc 4094 -o $@ $@.a

# The C library (lib/, doc/bootstrap-test.md R4): one object per
# source, $(OUT)/lib/NAME.obj and .syms, compiled by the host passes;
# and tools/plvmexit.c's exit, for running ccish's own passes under
# plvm (not part of the library).
LIBSRC := $(wildcard lib/*.c)
LIBOBJS:= $(patsubst lib/%.c,$(OUT)/lib/%.obj,$(LIBSRC)) \
          $(OUT)/lib/plvmexit.obj
# The library and plvmexit's sources, for the bootstrap and measure.py.
CCLIB_SRC = $(LIBSRC) tools/plvmexit.c
$(OUT)/lib/%.obj: lib/%.c $(wildcard lib/include/*.h) ccpp cccomp ccgen
	@mkdir -p $(dir $@)
	@./ccpp -o $(OUT)/lib/$* $< && ./cccomp -o $(OUT)/lib/$* $(OUT)/lib/$* \
	 && ./ccgen $(OUT)/lib/$*
$(OUT)/lib/plvmexit.obj: tools/plvmexit.c $(wildcard lib/include/*.h) \
                         ccpp cccomp ccgen
	@mkdir -p $(dir $@)
	@./ccpp -o $(OUT)/lib/plvmexit $< \
	 && ./cccomp -o $(OUT)/lib/plvmexit $(OUT)/lib/plvmexit \
	 && ./ccgen $(OUT)/lib/plvmexit

# XLOAD (doc/xload.md), the loader for ccish's images (ccish -x): its
# second part, an image, held in its first part as data
# (tools/img2h.py), both built with the host's ccish, for plvm and the
# Apple II alike.
XLOAD_DIR := $(OUT)/xload
XLOAD     := $(XLOAD_DIR)/xload
$(XLOAD_DIR)/xload2: xload/xload2.c xload/xlcore.c ccish
	@mkdir -p $(dir $@)
	@./ccish -x -o $@ xload/xload2.c
$(XLOAD_DIR)/xl2img.h: $(XLOAD_DIR)/xload2 tools/img2h.py
	@python3 tools/img2h.py $< xl2_image > $@
$(XLOAD): xload/xload.c xload/xlcore.c $(XLOAD_DIR)/xl2img.h ccish
	@./ccish -I $(XLOAD_DIR) -o $@ xload/xload.c
xload: $(XLOAD)

# The passes a test may run; a stamp depends on these, so a rebuilt pass
# re-runs the tests.
PASSES := ccish ccpp cccomp ccgen ccrel \
          ccpp-small cccomp-small ccgen-small ccrel-small $(PLVM) \
          $(CCSTAT) tools/ccrun $(LIBOBJS) $(XLOAD)

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

# img: each run test again as an XLOAD image (ccish -x), run through
# XLOAD under plvm, from a directory that holds XLOAD: its stdout and
# exit status as for run (tests/imgrun.sh; its listing and compile-time
# stderr are run's to check).
$(OUT)/img/xload: $(XLOAD)
	@mkdir -p $(dir $@) && cp $< $@
$(OUT)/img/%.stamp: tests/run/%.c tests/run/%.expected tests/imgrun.sh \
                    $(wildcard tests/run/*.status) $(PASSES) $(OUT)/img/xload
	@sh tests/imgrun.sh $* && touch $@

# gen: ccish -d (machine ops after the peephole stage) must equal the
# .exp, and the module must run under plvm (through tools/ccrun) and
# exit with the status in .status (or 0 if there is none).
GEN := $(wildcard tests/gen/*.c)
$(OUT)/gen/%.stamp: tests/gen/%.c tests/gen/%.exp \
                    $(wildcard tests/gen/*.status) $(PASSES)
	@mkdir -p $(dir $@)
	@./ccish -d -o $(dir $@)$* tests/gen/$*.c >$@.actual 2>$@.err
	@test ! -s $@.err || { echo "gen: unexpected messages"; cat $@.err; exit 1; }
	@diff -u tests/gen/$*.exp $@.actual
	@(cd $(dir $@) && $(CCRUN) -t 10 $(notdir $*) >/dev/null 2>&1; echo $$? > $(notdir $*).status)
	@want=$$(cat tests/gen/$*.status 2>/dev/null || echo 0); got=$$(cat $(dir $@)$*.status); \
	 [ "$$got" = "$$want" ] || { echo "gen $*: exit status $$got, expected $$want"; exit 1; }
	@touch $@

# xrun: the same source must print the same under ccish+plvm as under
# the host oracle, whose output make xref cached in the .ref, and exit
# with status 0, as the oracle must (make xref fails otherwise). A
# NAME.lib file names the library objects to link (the oracle uses the
# host's C library).
XRUN := $(wildcard tests/xrun/*.c)
$(OUT)/xrun/%.stamp: tests/xrun/%.c tests/xrun/%.ref \
                     $(wildcard tests/xrun/*.lib) $(PASSES)
	@mkdir -p $(dir $@)
	@./ccish -I inc -o $(dir $@)$* tests/xrun/$*.c \
	 $$(for o in $$(cat tests/xrun/$*.lib 2>/dev/null); do echo $(OUT)/lib/$$o; done) \
	 2>$(dir $@)$*.err
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
# with "expected" the module's output must equal it, and its exit status
# the number in "status" (or 0 if there is none); with "expect-error"
# the link must fail with its message.
LINK_TESTS := $(wildcard tests/link/*/expected tests/link/*/expect-error)
LINK_STAMPS := $(sort $(patsubst tests/link/%/,$(OUT)/link/%.stamp,\
                 $(dir $(LINK_TESTS))))
$(OUT)/link/%.stamp: $(wildcard tests/link/*/*.c) $(LINK_TESTS) \
                     $(wildcard tests/link/*/status) $(PASSES) \
                     tests/link_test.sh tests/lib.sh tests/check_listing.sh
	@mkdir -p $(dir $@) && tests/link_test.sh $* && touch $@

# special: the specialized checks, each a self-contained script that exits
# nonzero on failure; the ones that read test inputs or ccish.msg depend
# on them too. The bootstrap test (below) is a special script but is kept
# out of SPECIAL, so the regular suite does not run it.
SPECIAL := $(filter-out tests/special/bootstrap.sh,$(wildcard tests/special/*.sh))
$(OUT)/special/%.stamp: tests/special/%.sh tests/lib.sh $(PASSES)
	@mkdir -p $(dir $@) && sh $< && touch $@

$(OUT)/special/small.stamp: $(wildcard tests/run/*.c tests/xrun/*.c tests/xrun/*.lib \
                         tests/gen/*.c tests/link/*/*.c)
$(OUT)/special/front.stamp: tests/run/hello.c tests/errors/const_assign.c
$(OUT)/special/back.stamp: tests/run/hello.c tests/run/global_init.c src/ir.h
$(OUT)/special/ccrel.stamp: tests/run/hello.c tests/run/global_init.c \
                         tests/link/helpers/a.c tests/link/helpers/b.c
$(OUT)/special/msgs.stamp: ccish.msg
$(OUT)/special/smalldiag.stamp: tests/run/const.c
$(OUT)/special/tables.stamp: tests/special/tables.c src/irin.c src/lower.c src/ir.h \
                          src/mop.h
$(OUT)/special/enumvals.stamp: $(SOURCES)
$(OUT)/special/fit.stamp: doc/measure.txt
$(OUT)/special/xload_self.stamp: doc/measure.txt ccrel-small $(XLOAD) \
                         tests/run/hello.c $(wildcard src/*.c src/*.h)
$(OUT)/special/xload.stamp: ccrel-small $(XLOAD) tools/xlinfo.py \
                         tests/xload/xltest.c tests/xload/xltest.expected xload/xload.c \
                         $(patsubst %,tests/run/%.c,hello global_init pointers addr_const c_names)

# The compiler bootstrap test: the seed, this source's $(CC) build,
# compiles the compiler's source into a VM module (stage 1); stage 1,
# run under $(BOOTSTRAP_VM), compiles the same source into stage 2;
# stage 2 does it again into stage 3; and the test is that stage 2 and
# stage 3 are byte-for-byte the same module. It's its own target, not
# in the regular suite (full-test runs both), because it's slow. The
# sources come from here: each program's, and the library's.
$(OUT)/special/bootstrap.stamp: tests/special/bootstrap.sh $(PASSES) tests/lib.sh \
                             tools/cclink ccish.msg $(wildcard lib/include/*.h)
	@mkdir -p $(dir $@) && BOOTSTRAP_VM="$(BOOTSTRAP_VM)" \
	 BOOT_ccpp="$(CCPP_SRC)" BOOT_cccomp="$(CCCOMP_SRC)" \
	 BOOT_ccgen="$(CCGEN_SRC)" BOOT_ccrel="$(CCREL_SRC)" \
	 BOOT_LIB="$(CCLIB_SRC)" sh $< && touch $@

# Run bootstrap test. This takes ~30 seconds on Linux
bootstrap: $(OUT)/special/bootstrap.stamp

# Run all tests, including bootstrap. Thorough but takes longer
full-test: test $(OUT)/special/bootstrap.stamp

# Everything the tests need, built but not run: the passes, the small
# passes, the VM, and the cross references. The test stamps hang off this,
# so nothing runs against a stale pass. It also refreshes doc/measure.txt
# (and so doc/limits.md), which takes a few seconds, only after a change.
testbuild: all small $(PLVM) $(CCSTAT) $(LIBOBJS) xref doc/measure.txt

# The whole suite: a stamp for every test, then one that depends on all
# of them and reports the count (only when something actually ran).
ALL_STAMPS := $(patsubst tests/run/%.c,$(OUT)/run/%.stamp,$(RUN)) \
              $(patsubst tests/run/%.c,$(OUT)/img/%.stamp,$(RUN)) \
              $(patsubst tests/gen/%.c,$(OUT)/gen/%.stamp,$(GEN)) \
              $(patsubst tests/xrun/%.c,$(OUT)/xrun/%.stamp,$(XRUN)) \
              $(patsubst tests/errors/%.c,$(OUT)/err/%.stamp,$(ERR)) \
              $(patsubst tests/pp/%.c,$(OUT)/pp/%.stamp,$(PP_TESTS)) \
              $(LINK_STAMPS) \
              $(patsubst tests/special/%.sh,$(OUT)/special/%.stamp,$(SPECIAL))

test: testbuild $(ALL_STAMPS) $(OUT)/ALL.stamp

$(OUT)/ALL.stamp: $(ALL_STAMPS)
	@echo "passed: $(words $(ALL_STAMPS)) tests"
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

# doc/limits.md, the table of ccish's limits, from the "limit GROUP:"
# comments in the sources (tools/limits.sh), with each one's peak from
# doc/measure.txt. Both are checked in, so that people can read them
# without building. make all regenerates doc/limits.md from the
# measure.txt there is; it doesn't depend on measure.txt, so that make
# all doesn't run the measurement. Instead, the rule that writes
# measure.txt (which make test runs) regenerates limits.md too.
LIMIT_SRC := $(sort $(wildcard src/*.c src/*.h lib/*.c))
LIMITS_CMD = sh tools/limits.sh -m doc/measure.txt $(LIMIT_SRC) \
             > doc/limits.md.tmp && mv doc/limits.md.tmp doc/limits.md
doc/limits.md: tools/limits.sh $(LIMIT_SRC)
	@echo "tools/limits.sh > $@"
	@$(LIMITS_CMD)

limits: doc/limits.md

# doc/measure.txt: tools/measure.py's report, compiling ccish's own
# source with ccish (about 4 seconds; needs python3).
doc/measure.txt: ccpp cccomp ccgen ccrel ccish.msg tools/measure.py \
                 tools/cclink tools/plvmexit.c $(LIMIT_SRC) \
                 $(wildcard lib/include/*.h)
	@echo "tools/measure.py > $@"
	@python3 tools/measure.py > $@.tmp && mv $@.tmp $@ \
	 || { rm -f $@.tmp; exit 1; }
	@echo "tools/limits.sh > doc/limits.md"
	@$(LIMITS_CMD)

measure: doc/measure.txt
	@cat doc/measure.txt

# Profile ccpp, cccomp and ccgen compiling ccish's own sources, each a
# module under a plvm patched to count ops by bytecode offset
# (tools/profile.py, tools/plvm-profile.patch; about 20 seconds): each
# pass's busiest functions, in tools/out/profile.txt. PROFILE_ARGS
# passes options on, e.g. PROFILE_ARGS="-n 50 -f id_find cccomp".
PROFILE_ARGS ?=
profile: all doc/measure.txt $(CCSTAT)
	@python3 tools/profile.py $(PROFILE_ARGS) > tools/out/profile.txt.tmp \
	 && mv tools/out/profile.txt.tmp tools/out/profile.txt \
	 || { rm -f tools/out/profile.txt.tmp; exit 1; }
	@cat tools/out/profile.txt

# The demo disk and the emulator: samples/Makefile's disk and run, after
# building the compiler. Variables set on the command line (RUN_VM,
# BOOT_DISK, DISK_PUT, ...) pass on to it.
demo-disk: all
	$(MAKE) -C samples disk

# Run demo disk.
demo-run: all
	$(MAKE) -C samples run

# Put each source file's functions, C then machine ops, in tools/out/lst/
# (tools/listings.py); tools/opstats.py counts the ops in them.
listings: all
	python3 tools/listings.py

# Generate the sources, for tools/measure.py: a line for each Apple II program,
# its name and its sources in link order, and a line "lib" and the
# library's. measure.py asks for these, so they're kept only here.
print-sources:
	@echo ccpp $(CCPP_SRC)
	@echo cccomp $(CCCOMP_SRC)
	@echo ccgen $(CCGEN_SRC)
	@echo ccrel $(CCREL_SRC)
	@echo lib $(CCLIB_SRC)

# Print some common ops and op sequences (e.g., peephole targets)
# This counts cccomp's ops and its most common runs of 2 to 4 ops,
# reading the listings from step 1.
# Add -o to keep operands, so that LLW 0 and LLW 2 count separately.
common-ops: tools/opstats.py ccomp listings
	python3 tools/opstats.py -p cccomp -n 30 -l 4


clean:
	rm -f ccish ccpp cccomp ccgen ccrel
	rm -f ccpp-small cccomp-small ccgen-small ccrel-small
	rm -rf $(OUT) tools/out bootstrap
	rm -f doc/measure.txt doc/limits.md

.PHONY: all small test testbuild xref measure listings print-sources bootstrap xload \
        full-test limits demo-disk demo-run profile clean
