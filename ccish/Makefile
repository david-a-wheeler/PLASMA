# Build the ccish compiler and run its tests.
# Requires GNU Make
#
#   make        build everything the tests need, and run the stale tests
#   make all    build ccish and its passes (ccpp, cccomp, ccgen, cclink),
#               and doc/limits.md
#   make small  build the small passes (-DCCISH_SMALL), for the host
#   make xref   write the cross tests' references with a host C compiler
#   make limits regenerate doc/limits.md, the table of ccish's limits
#   make measure regenerate doc/measure.txt, ccish's sizes and peaks
#               measured on its own source, if stale, and print it
#   make cov-xrun statement and branch coverage of the passes, from
#               the cross tests (doc/coverage.md; cov-run and cov-all
#               for other sets, coverage KINDS=... for any set)
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
PARSER = src/tokin.c src/name.c src/type.c src/parse.c src/pexpr.c \
         src/pstmt.c src/ptop.c src/irout.c
CODEGEN = src/irin.c src/irloop.c src/lower.c src/peep.c src/emit.c \
          src/helper.c
LINK   = src/link.c
# The four Apple II programs (the passes), each built for the host
# twice: PROG, the full build, and PROG-small, the small build
# (-DCCISH_SMALL, approach.md decision 21), whose limits are the Apple
# II's; the tests check that both write the same objects and modules.
# Every list of the programs below comes from PROGS.
PROGS := ccpp cccomp ccgen cclink
SMALL_PROGS := $(PROGS:%=%-small)
# Each program's sources, in link order (the bootstrap's and
# measure.py's too, below), and ccish's, the driver that runs all four.
SRC_ccpp   = src/ccpp.c $(COMMON) $(PP)
SRC_cccomp = src/cccomp.c $(COMMON) $(NAM) $(PARSER)
SRC_ccgen  = src/ccgen.c $(COMMON) $(NAM) $(CODEGEN)
SRC_cclink  = src/cclink.c $(COMMON) $(LINK)
SRC_ccish  = src/main.c $(COMMON) $(PP) $(NAM) $(PARSER) $(CODEGEN) $(LINK)
HEADERS := $(wildcard src/*.h)
SOURCES := $(wildcard src/*.h src/*.c)
# The library's headers: ccpp's default include directory, compiled in
# for the host (on the VM it's <syspath>ccinc; src/pp.c). Every
# program gets it, so that one recipe builds them all; only the ones
# with pp.c (ccpp and ccish) use it.
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
# The stamp that says doc/measure.txt and tools/out are current (its
# rule is below, after doc/limits.md's).
MEASURED := tools/out/measure.stamp

all: ccish $(PROGS) doc/limits.md
small: $(SMALL_PROGS)

# Each program and its sources; one recipe builds them all, from the
# .c files among the prerequisites, and the small build adds its flag.
ccish:               $(SRC_ccish) $(HEADERS)
ccpp ccpp-small:     $(SRC_ccpp) $(HEADERS)
cccomp cccomp-small: $(SRC_cccomp) $(HEADERS)
ccgen ccgen-small:   $(SRC_ccgen) $(HEADERS)
cclink cclink-small:   $(SRC_cclink) $(HEADERS)
PROG_FLAGS =
$(SMALL_PROGS): PROG_FLAGS = -DCCISH_SMALL
ccish $(PROGS) $(SMALL_PROGS):
	$(CC) $(CFLAGS) $(PROG_FLAGS) $(CCINC) -o $@ $(filter %.c,$^)

$(PLVM):
	$(MAKE) -C ../src plvm

$(XPLASM):
	$(MAKE) -C ../src xplasm

$(CCSTAT): tools/ccstat.pla ../src/inc/cmdsys.plh $(XPLASM)
	@mkdir -p $(dir $@)
	$(XPLASM) -AMOWS tools/ccstat.pla > $@.a
	$(ACME) --setpc 4094 -o $@ $@.a

# The C library (lib/, doc/bootstrap-test.md R4): one object per
# source, $(OUT)/lib/NAME.obj and .syms, compiled by the host passes
# (its sources go to the bootstrap and measure.py too).
LIBSRC := $(wildcard lib/*.c)
LIBOBJS:= $(patsubst lib/%.c,$(OUT)/lib/%.obj,$(LIBSRC))
$(OUT)/lib/%.obj: lib/%.c $(wildcard lib/include/*.h) ccpp cccomp ccgen
	@mkdir -p $(dir $@)
	@./ccpp -o $(OUT)/lib/$* $< && ./cccomp -o $(OUT)/lib/$* $(OUT)/lib/$* \
	 && ./ccgen $(OUT)/lib/$*
# The library's map (doc/linking.md, 5.2): which object defines each
# name, generated from the summaries, for the linker's -L.
$(OUT)/lib/lib.map: $(LIBOBJS) tools/libmap.py
	@python3 tools/libmap.py $(OUT)/lib

# XLOAD (doc/xload.md), the loader for ccish's images (ccish -x): its
# first part, xload, a REL module, and its second part, xload2, an
# image beside it, both built with the host's ccish, for plvm and the
# Apple II alike.
XLOAD_DIR := $(OUT)/xload
XLOAD     := $(XLOAD_DIR)/xload
$(XLOAD_DIR)/xload2: xload/xload2.c xload/xlcore.c ccish
	@mkdir -p $(dir $@)
	@./ccish -x -o $@ xload/xload2.c
$(XLOAD): xload/xload.c xload/xlcore.c $(XLOAD_DIR)/xload2 ccish
	@./ccish -o $@ xload/xload.c
xload: $(XLOAD)

# apple: the disks that run ccish on a 64K Apple II through XLOAD, in
# tests/out/apple (tools/a2disk says what each holds).
# Its inputs: tools/a2disk makes them first (make apple-inputs), so
# that running it directly can't make disks from a stale tools/out.
APPLE_IN := $(XLOAD) $(MEASURED) $(LIBOBJS) $(OUT)/lib/lib.map cclink-small
apple: $(APPLE_IN)
	@tools/a2disk
apple-inputs: $(APPLE_IN)

# The passes a test may run; a stamp depends on these, so a rebuilt pass
# re-runs the tests.
PASSES := ccish $(PROGS) $(SMALL_PROGS) $(PLVM) \
          $(CCSTAT) tools/ccrun $(LIBOBJS) $(XLOAD)

# --- The test kinds. Each is a directory of inputs; the stamp for input
# tests/<kind>/<stem> is tests/out/<kind>/<stem>.stamp.

# Two steps the run, gen and xrun recipes share, for the module $*
# compiled beside the stamp $@. RUN_MODULE runs it under plvm (through
# tools/ccrun), its output, CRs removed, in $*.actual and its exit
# status in $*.status there. $(call CHECK_STATUS,KIND,WANT) fails
# unless that status is the number in the file WANT (0 if it's missing
# or empty, as /dev/null is, for "must be 0").
RUN_MODULE = (cd $(dir $@) && { $(CCRUN) -t 10 $* 2>&1; echo $$? > $*.status; } \
              | tr -d '\r' > $*.actual)
CHECK_STATUS = want=$$(cat $2 2>/dev/null); want=$${want:-0}; got=$$(cat $(dir $@)$*.status); \
               [ "$$got" = "$$want" ] || { echo "$1 $*: exit status $$got, expected $$want"; exit 1; }

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
	@./ccish -v -o $(dir $@)$* tests/run/$*.c >$(dir $@)$*.lst 2>$(dir $@)$*.stderr
	@$(RUN_MODULE)
	@diff -u tests/run/$*.expected $(dir $@)$*.actual
	@$(call CHECK_STATUS,run,tests/run/$*.status)
	@if [ -f tests/run/$*.stderr.expected ]; then diff -u tests/run/$*.stderr.expected $(dir $@)$*.stderr; elif [ -s $(dir $@)$*.stderr ]; then echo "run: unexpected stderr"; cat $(dir $@)$*.stderr; exit 1; fi
	@tests/check_listing.sh tests/run/$*.c $(dir $@)$*.lst
	@touch $@

# img: each run test again as an XLOAD image (ccish -x), run through
# XLOAD under plvm, from a directory that holds XLOAD: its stdout and
# exit status as for run (tests/imgrun.sh; its listing and compile-time
# stderr are run's to check).
$(OUT)/img/xload: $(XLOAD)
	@mkdir -p $(dir $@) && cp $< $@ && cp $<2 $@2
$(OUT)/img/%.stamp: tests/run/%.c tests/run/%.expected tests/imgrun.sh \
                    $(wildcard tests/run/*.status) $(PASSES) $(OUT)/img/xload
	@sh tests/imgrun.sh $* && touch $@

# gen: ccish -m (machine ops after the peephole stage) must equal the
# .exp, and the module must run under plvm (through tools/ccrun) and
# exit with the status in .status (or 0 if there is none).
GEN := $(wildcard tests/gen/*.c)
$(OUT)/gen/%.stamp: tests/gen/%.c tests/gen/%.exp \
                    $(wildcard tests/gen/*.status) $(PASSES)
	@mkdir -p $(dir $@)
	@./ccish -m -o $(dir $@)$* tests/gen/$*.c >$@.actual 2>$@.err
	@test ! -s $@.err || { echo "gen: unexpected messages"; cat $@.err; exit 1; }
	@diff -u tests/gen/$*.exp $@.actual
	@$(RUN_MODULE)
	@$(call CHECK_STATUS,gen,tests/gen/$*.status)
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
	@$(RUN_MODULE)
	@diff -u tests/xrun/$*.ref $(dir $@)$*.actual
	@$(call CHECK_STATUS,xrun,/dev/null)
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
                         tests/gen/*.c tests/link/*/*.c samples/*.c)
$(OUT)/special/front.stamp: tests/run/hello.c tests/errors/const_assign.c
$(OUT)/special/back.stamp: tests/run/hello.c tests/run/global_init.c src/ir.h
$(OUT)/special/cclink.stamp: tests/run/hello.c tests/run/global_init.c \
                         tests/link/helpers/a.c tests/link/helpers/b.c \
                         $(OUT)/lib/lib.map
$(OUT)/special/lib.stamp: $(OUT)/lib/lib.map
$(OUT)/special/msgs.stamp: ccish.msg
$(OUT)/special/smalldiag.stamp: tests/run/const.c
$(OUT)/special/tables.stamp: tests/special/tables.c src/irin.c src/lower.c src/ir.h \
                          src/mop.h
$(OUT)/special/enumvals.stamp: $(SOURCES)
$(OUT)/special/fit.stamp: $(MEASURED)
$(OUT)/special/xload_self.stamp: $(MEASURED) cclink-small $(XLOAD) \
                         tests/run/hello.c $(SOURCES)
$(OUT)/special/xload.stamp: cclink-small $(XLOAD) tools/xlinfo.py \
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
                             $(OUT)/lib/lib.map ccish.msg $(wildcard lib/include/*.h)
	@mkdir -p $(dir $@) && BOOTSTRAP_VM="$(BOOTSTRAP_VM)" \
	 $(foreach p,$(PROGS),BOOT_$p="$(SRC_$p)") \
	 BOOT_LIB="$(LIBSRC)" sh $< && touch $@

# Run bootstrap test. This takes ~30 seconds on Linux
bootstrap: $(OUT)/special/bootstrap.stamp

# Run all tests, including bootstrap. Thorough but takes longer
full-test: test $(OUT)/special/bootstrap.stamp

# Everything the tests need, built but not run: the passes, the small
# passes, the VM, and the cross references. The test stamps hang off this,
# so nothing runs against a stale pass. It also refreshes doc/measure.txt
# (and so doc/limits.md), which takes a few seconds, only after a change.
testbuild: all small $(PLVM) $(CCSTAT) $(LIBOBJS) xref $(MEASURED)

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

# --- Coverage (gcov): what the tests exercise ------------------------
# The statement and branch coverage of the host's passes, from
# selected sets of the tests, for finding what no test covers and
# adding tests for it (doc/coverage.md). The five passes are built
# instrumented (-fprofile-arcs -ftest-coverage) in tools/out/cov/build
# (tools/covbuild.sh); a set's tests run against them with the regular
# suite's checks (the COV_KIND_ rules are the test rules' twins); the
# counters, the .gcda, are wiped before a set's run, as they
# accumulate across runs; and gcov's data (the set's gcov/) is merged
# by tools/coverage.py into the set's report.txt: each source's and
# the total's statement and branch coverage, and the statements and
# branches no test covers, with the merged annotated sources in the
# set's annotated/.
#
#   make cov-xrun   the cross tests (the oracle is a host compiler)
#   make cov-run    the run and gen tests (the expected outputs are
#                   worked out by hand)
#   make cov-all    every kind, including the bootstrap
#   make coverage KINDS="xrun err"   another set; the kinds are
#                   run img gen xrun err pp link boot
#
# A set's report, and so its run, is stale when the passes' sources,
# the set's test files, or anything the tests depend on changes; the
# counters are wiped then too, so the report always covers exactly
# the set's tests against the passes as they are (a stamp can't see a
# file leave its prerequisites, so the set's membership is also
# re-found into tests.list every time). Run one set at a time, not -j
# over sets: the passes share the counters. CC must be gcc, as gcov
# reads its data.

COV       = tools/out/cov
COV_BUILD = $(COV)/build/build.stamp
COV_BIN   = $(COV)/build/bin
COV_OBJ   = $(COV)/build/obj
COV_FLAGS = -fprofile-arcs -ftest-coverage
COV_PROGS := ccish ccpp cccomp ccgen cclink

# The instrumented build: each program's sources in $(COV_OBJ)/<prog>,
# its .gcno beside each .o, the program in $(COV_BIN), ccish.msg
# beside it (diag looks for it beside the program).
$(COV_BUILD): $(SOURCES) $(wildcard lib/include/*.h) ccish.msg tools/covbuild.sh
	@mkdir -p $(COV_BIN)
	@CC="$(CC)" CFLAGS="$(CFLAGS) $(COV_FLAGS)" \
	 INC="$(abspath lib/include)" \
	 BIN="$(abspath $(COV_BIN))" OBJ="$(abspath $(COV_OBJ))" \
	 $(foreach p,$(COV_PROGS),SRC_$p="$(SRC_$p)") sh tools/covbuild.sh
	@touch $@

# The sets: the name and the kinds that make it up.
COV_KINDS_xrun := xrun
COV_KINDS_run  := run gen
COV_KINDS_all  := run img gen xrun err pp link boot
COV_SETS := xrun run all

# The kinds a set may name, and `make coverage KINDS=...'s set: its
# kinds sorted and joined with +.
COV_KIND_LIST := run img gen xrun err pp link boot
ifeq ($(KINDS),)
else
COV_SETNAME := $(shell printf '%s\n' $(sort $(KINDS)) | paste -sd + -)
COV_KINDS_$(COV_SETNAME) := $(sort $(KINDS))
COV_SETS += $(COV_SETNAME)
endif

# Per kind: the test inputs a wipe must wait for (COV_DEPS), the
# directory the membership list looks in (COV_TESTDIRS, kind:directory
# pairs, as a patsubst's replacement can't hold a $(VAR_%) ), and the
# tests of the kind (COV_TESTS, for cov_kind_stamps's stamps).
COV_DEPS_run  = $(wildcard tests/run/*)
COV_DEPS_img  = $(wildcard tests/run/*)
COV_DEPS_gen  = $(wildcard tests/gen/*)
COV_DEPS_xrun = $(wildcard tests/xrun/*)
COV_DEPS_err  = $(wildcard tests/errors/*)
COV_DEPS_pp   = $(wildcard tests/pp/*)
COV_DEPS_link = $(wildcard tests/link/*/*.c tests/link/*/expected \
                 tests/link/*/expect-error tests/link/*/status)
COV_DEPS_boot = tests/special/bootstrap.sh tests/lib.sh $(LIBSRC) ccish.msg
COV_TESTDIRS := run:tests/run img:tests/run gen:tests/gen xrun:tests/xrun \
                err:tests/errors pp:tests/pp link:tests/link boot:tests/special

# The directory of kind $(1)'s tests, from COV_TESTDIRS.
cov_testdir = $(word 2,$(subst :, ,$(filter $(1):%,$(COV_TESTDIRS))))
COV_TESTS_run  = $(RUN)
COV_TESTS_img  = $(RUN)
COV_TESTS_gen  = $(GEN)
COV_TESTS_xrun = $(XRUN)
COV_TESTS_err  = $(ERR)
COV_TESTS_pp   = $(PP_TESTS)

# The cov stamps of the set $(2)'s tests of kind $(1), one per test.
cov_kind_stamps = $(if $(filter boot,$(1)),$(COV)/$(2)/boot/bootstrap.stamp,\
                 $(if $(filter link,$(1)),$(patsubst tests/link/%/,$(COV)/$(2)/link/%.stamp,$(sort $(dir $(LINK_TESTS)))),\
                 $(if $(filter img,$(1)),$(patsubst tests/run/%.c,$(COV)/$(2)/img/%.stamp,$(RUN)),\
                 $(patsubst $(call cov_testdir,$(1))/%.c,$(COV)/$(2)/$(1)/%.stamp,$(call COV_TESTS_$(1))))))

# A kind that names no test is a typo (a pattern that matches nothing):
# fail when the Makefile is read, not with a silently smaller set.
$(foreach S,$(COV_SETS),$(foreach k,$(COV_KINDS_$(S)),$(if $(call cov_kind_stamps,$(k),$(S)),$(empty),$(error coverage: kind $(k) of set $(S) matches no tests))))

# The set $(1)'s targets, its kinds $(2), the test inputs a wipe
# must wait for $(3), and the set's test stamps $(4). The list is a
# real file whose rule always runs (a stamp can't see a file leave
# its prerequisites, but re-finding the set can): it rewrites the
# file only when the membership changes, so its time, and so the
# wipe, moves only then.
define COV_SET
.PHONY: cov-check-$(1)
cov-check-$(1):
$(COV)/$(1)/tests.list: cov-check-$(1)
	@mkdir -p $(COV)/$(1)
	@{ find $(sort $(foreach k,$(2),$(call cov_testdir,$(k)))) inc lib/include -type f; } \
	  | sort -u >$(COV)/$(1)/tests.list.tmp
	@if cmp -s $(COV)/$(1)/tests.list.tmp $(COV)/$(1)/tests.list; then \
	    rm -f $(COV)/$(1)/tests.list.tmp; \
	  else mv $(COV)/$(1)/tests.list.tmp $(COV)/$(1)/tests.list; fi

$(COV)/$(1)/wipe.stamp: $(COV_BUILD) $(COV)/$(1)/tests.list $(PASSES) \
                        $(wildcard inc/* lib/include/*.h) $(3)
	@rm -f $(COV_OBJ)/*/*.gcda
	@for k in $(2); do rm -rf $(COV)/$(1)/$$$$k; done
	@touch $$@

# The set's tests share the .gcda counters, and parallel writes can
# lose updates, so the stamps are chained, each after its
# predecessors, and run one at a time, even with -j.
$(eval __COV_CHAIN := )
$(foreach t,$(4),$(eval $(t): $(strip $(__COV_CHAIN)))$(eval __COV_CHAIN := $(strip $(__COV_CHAIN)) $(t)))

$(COV)/$(1)/run.stamp: $(4)
	@echo "cov $(1): $(words $(4)) tests ($(2))"
	@touch $$@

$(COV)/$(1)/report.txt: $(COV)/$(1)/run.stamp tools/coverage.py
	@rm -rf $(COV)/$(1)/gcov $(COV)/$(1)/annotated; mkdir -p $(COV)/$(1)/gcov
	@for p in $(COV_PROGS); do \
	    ls $(COV_OBJ)/$$$$p/*.gcda >/dev/null 2>&1 || continue; \
	    mkdir -p $(COV)/$(1)/gcov/$$$$p; \
	    (cd $(COV)/$(1)/gcov/$$$$p && \
	     gcov -b -j $(abspath $(COV_OBJ))/$$$$p/*.o >/dev/null) || exit 1; \
	done
	@python3 tools/coverage.py $(1) $(COV)/$(1) >$(COV)/$(1)/report.txt.tmp \
	  && mv $(COV)/$(1)/report.txt.tmp $(COV)/$(1)/report.txt
endef

# The set's test rules: the test rules above with the instrumented
# passes, in call argument 1's directory, and the set's directory,
# call argument 2, in place of tests/out; the wipe stamp takes the
# place of $(PASSES), as it covers it. The defines are expanded when
# the Makefile is read, not when a recipe runs, so the rules' $@ and
# $* are written doubled, with two dollars where a recipe line would
# have one, and a third for the $? and $got the shell wants, as
# make's own $ and $( ) references would otherwise eat them.
COV_RUN_MODULE = (cd $$(dir $$@) && { $(CCRUN) -t 10 $$* 2>&1; echo $$$$? > $$*.status; } \
                 | tr -d '\r' > $$*.actual)
COV_CHECK_STATUS = want=$$$$(cat $2 2>/dev/null); want=$$$${want:-0}; \
                   got=$$$$(cat $$(dir $$@)$$*.status); \
                   [ "$$$$got" = "$$$$want" ] || \
                   { echo "cov $1 $$*: exit status $$$$got, expected $$$$want"; exit 1; }

define COV_KIND_run
$(2)/run/%.stamp: tests/run/%.c tests/run/%.expected \
                  $(wildcard tests/run/*.stderr.expected) \
                  $(wildcard tests/run/*.status) \
                  $(2)/wipe.stamp tests/check_listing.sh
	@mkdir -p $$(dir $$@)
	@$(1)/ccish -v -o $$(dir $$@)$$* tests/run/$$*.c \
	  >$$(dir $$@)$$*.lst 2>$$(dir $$@)$$*.stderr
	@$(COV_RUN_MODULE)
	@diff -u tests/run/$$*.expected $$(dir $$@)$$*.actual
	@$(call COV_CHECK_STATUS,run,tests/run/$$*.status)
	@if [ -f tests/run/$$*.stderr.expected ]; then diff -u tests/run/$$*.stderr.expected $$(dir $$@)$$*.stderr; elif [ -s $$(dir $$@)$$*.stderr ]; then echo "cov run $$*: unexpected stderr"; cat $$(dir $$@)$$*.stderr; exit 1; fi
	@tests/check_listing.sh tests/run/$$*.c $$(dir $$@)$$*.lst
	@touch $$@
endef

define COV_KIND_img
$(2)/img/%.stamp: tests/run/%.c tests/run/%.expected \
                  $(wildcard tests/run/*.status) $(2)/wipe.stamp \
                  $(OUT)/img/xload tests/imgrun.sh
	@mkdir -p $$(dir $$@) && sh tests/imgrun.sh $$* $(1) && touch $$@
endef

define COV_KIND_gen
$(2)/gen/%.stamp: tests/gen/%.c tests/gen/%.exp $(wildcard tests/gen/*.status) $(2)/wipe.stamp
	@mkdir -p $$(dir $$@)
	@$(1)/ccish -m -o $$(dir $$@)$$* tests/gen/$$*.c >$$@.actual 2>$$@.err
	@test ! -s $$@.err || { echo "cov gen: unexpected messages"; cat $$@.err; exit 1; }
	@diff -u tests/gen/$$*.exp $$@.actual
	@$(COV_RUN_MODULE)
	@$(call COV_CHECK_STATUS,gen,tests/gen/$$*.status)
	@touch $$@
endef

define COV_KIND_xrun
$(2)/xrun/%.stamp: tests/xrun/%.c tests/xrun/%.ref $(wildcard tests/xrun/*.lib) $(2)/wipe.stamp
	@mkdir -p $$(dir $$@)
	@$(1)/ccish -I inc -o $$(dir $$@)$$* tests/xrun/$$*.c \
	 $$$$(for o in $$$$(cat tests/xrun/$$*.lib 2>/dev/null); do echo $(OUT)/lib/$$$$o; done) \
	 2>$$(dir $$@)$$*.err
	@test ! -s $$(dir $$@)$$*.err || { echo "cov xrun: unexpected messages"; cat $$(dir $$@)$$*.err; exit 1; }
	@$(COV_RUN_MODULE)
	@diff -u tests/xrun/$$*.ref $$(dir $$@)$$*.actual
	@$(call COV_CHECK_STATUS,xrun,/dev/null)
	@touch $$@
endef

define COV_KIND_err
$(2)/err/%.stamp: tests/errors/%.c tests/errors/%.stderr.expected $(2)/wipe.stamp
	@mkdir -p $$(dir $$@)
	@! $(1)/ccish -o $$(dir $$@)$$* tests/errors/$$*.c 2>$$@.actual
	@grep -qF -f tests/errors/$$*.stderr.expected $$@.actual
	@touch $$@
endef

define COV_KIND_pp
$(2)/pp/%.stamp: tests/pp/%.c tests/pp/%.nam tests/pp/%.tok $(2)/wipe.stamp tests/pp_test.sh tests/lib.sh
	@mkdir -p $$(dir $$@) && tests/pp_test.sh $$* $(1) && touch $$@
endef

define COV_KIND_link
$(2)/link/%.stamp: $(wildcard tests/link/*/*.c) $(LINK_TESTS) $(wildcard tests/link/*/status) $(2)/wipe.stamp tests/link_test.sh tests/lib.sh tests/check_listing.sh
	@mkdir -p $$(dir $$@) && sh tests/link_test.sh $$* $(1) && touch $$@
endef

define COV_KIND_boot
$(2)/boot/bootstrap.stamp: tests/special/bootstrap.sh tests/lib.sh \
                           $(2)/wipe.stamp $(OUT)/lib/lib.map ccish.msg \
                           $(wildcard lib/include/*.h)
	@mkdir -p $$(dir $$@) && BOOTSTRAP_VM="$(BOOTSTRAP_VM)" SEED="$(1)" \
	 $(foreach p,$(PROGS),BOOT_$p="$(SRC_$p)") BOOT_LIB="$(LIBSRC)" \
	 sh tests/special/bootstrap.sh && touch $$@
endef

# Expand the sets: each one's targets and its test rules, per kind.
$(foreach S,$(COV_SETS),$(eval $(call COV_SET,$(S),$(COV_KINDS_$(S)),\
  $(sort $(foreach k,$(COV_KINDS_$(S)),$(COV_DEPS_$(k)))),\
  $(sort $(foreach k,$(COV_KINDS_$(S)),$(call cov_kind_stamps,$(k),$(S)))))))
$(foreach S,$(COV_SETS),$(foreach k,$(COV_KINDS_$(S)),$(eval $(call COV_KIND_$(k),$(COV_BIN),$(COV)/$(S)))))

# The collections, each the set's report, printed.
.PHONY: cov-xrun cov-run cov-all coverage
cov-xrun: $(COV)/xrun/report.txt
	@cat $<
cov-run: $(COV)/run/report.txt
	@cat $<
cov-all: $(COV)/all/report.txt
	@cat $<

coverage:
	@test -n "$(KINDS)" || { echo 'coverage: KINDS="kinds" (the kinds: $(COV_KIND_LIST))'; exit 2; }
	@for k in $(KINDS); do \
	    case " $(COV_KIND_LIST) " in \
	    *" $$k "*) ;; \
	    *) echo "coverage: no kind $$k (the kinds: $(COV_KIND_LIST))"; exit 2;; \
	    esac; \
	done
	@$(MAKE) --no-print-directory $(COV)/$(COV_SETNAME)/report.txt
	@cat $(COV)/$(COV_SETNAME)/report.txt

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
# source with ccish (about 4 seconds; needs python3). It also writes
# tools/out (the modules, images, listings and PROGRAM.objs that
# tools/a2disk, xload_self.sh and the other tools read), and
# $(MEASURED) records that both are current. The rules that read
# either depend on $(MEASURED), never on doc/measure.txt: that's
# checked in, so a git checkout gives it a new time, and it stays when
# tools/out is deleted, and either way it would look current when
# tools/out isn't.
$(MEASURED): $(PROGS) ccish.msg tools/measure.py \
                 tools/libmap.py $(LIMIT_SRC) $(XLOAD) \
                 $(wildcard lib/include/*.h)
	@echo "tools/measure.py > doc/measure.txt"
	@python3 tools/measure.py > doc/measure.txt.tmp \
	 && mv doc/measure.txt.tmp doc/measure.txt \
	 || { rm -f doc/measure.txt.tmp; exit 1; }
	@echo "tools/limits.sh > doc/limits.md"
	@$(LIMITS_CMD)
	@touch $@

measure: $(MEASURED)
	@cat doc/measure.txt

# Profile ccpp, cccomp and ccgen compiling ccish's own sources, each a
# module under a plvm patched to count ops by bytecode offset
# (tools/profile.py, tools/plvm-profile.patch; about 20 seconds): each
# pass's busiest functions, in tools/out/profile.txt. PROFILE_ARGS
# passes options on, e.g. PROFILE_ARGS="-n 50 -f id_find cccomp".
PROFILE_ARGS ?=
profile: all $(MEASURED) $(CCSTAT)
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
	@$(foreach p,$(PROGS),echo $p $(SRC_$p);)
	@echo lib $(LIBSRC)

# Print cccomp's commonest ops and runs of 2 to 4 ops (peephole
# targets), from make listings' listings. Add -o to tools/opstats.py
# to keep operands, so that LLW 0 and LLW 2 count separately.
common-ops: tools/opstats.py listings
	python3 tools/opstats.py -p cccomp -n 30 -l 4


clean:
	rm -f ccish $(PROGS) $(SMALL_PROGS)
	rm -rf $(OUT) tools/out bootstrap
	rm -f doc/measure.txt doc/limits.md

.PHONY: all small test testbuild xref measure listings print-sources bootstrap xload apple apple-inputs \
        full-test limits demo-disk demo-run profile common-ops clean \
        cov-xrun cov-run cov-all coverage
