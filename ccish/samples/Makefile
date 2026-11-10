# Sample programs for ccish. Run make in ccish/ first, which builds the
# compiler.
#
#   make         the Apple II modules: rod (the plain one) and rodfast
#   make disk    rods.po, a ProDOS disk of them and PLASMA's own rod
#   make run     boot PLASMA on an emulated 64K Apple IIe (tools/a2run,
#                a window), with rods.po in the second drive
#
# A module runs on an Apple II under PLASMA (copy it to a disk as a
# REL file, as "make disk" does). These draw on the real screen, so
# they don't run under plvm, whose heap covers the screen's memory;
# use an Apple II or tools/a2run.

CCISH = ../ccish
CCPP  = ../ccpp
CCCOMP = ../cccomp
CCGEN = ../ccgen

all: rod rodfast

lores.obj: lores.c lores.h
	$(CCPP) -o lores lores.c && $(CCCOMP) lores && $(CCGEN) lores

rodfast: rodfast.c lores.h conio.h lores.obj
	$(CCISH) -o rodfast rodfast.c lores

rod: rod.c conio.h
	$(CCISH) -o rod rod.c

# A 140K ProDOS disk, rods.po (volume RODS), of Rod's Colors three
# ways, to run on an Apple II (or tools/a2run) beside a PLASMA boot
# disk, whose SYS/CONIO they all use: RODPLA, PLASMA's own (rod.pla, as
# built for PLASMA 2.20's demo disk), and ccish's ROD and RODFAST.
# Run one at PLASMA's prompt with +/RODS/ROD (and so on). PLASMA_DEMOS
# is the disk RODPLA is taken from (as DEMOS/ROD).
#
# The disk commands are AppleCommander's command-line tool's (AC) by
# default; set these to use another tool:
#   DISK_NEW IMAGE VOLUME         make an empty 140K ProDOS disk
#   DISK_GET IMAGE PATH           write a file's bytes to stdout
#   DISK_PUT IMAGE NAME TYPE      store stdin as a file of ProDOS type
#   DISK_DEL IMAGE NAME           delete a file
#   DISK_LIST IMAGE               list the disk's files
AC = ../../bin/ac-linux-aarch64-15.2
DISK_NEW  = $(AC) -pro140
DISK_GET  = $(AC) -g
DISK_PUT  = $(AC) -p
DISK_DEL  = $(AC) -d
DISK_LIST = $(AC) -l
PLASMA_DEMOS = ../../images/apple/PLEIADES-2.20-DEMOS.po
DISK = rods.po

disk: $(DISK)

$(DISK): rod rodfast $(PLASMA_DEMOS)
	rm -f $@
	$(DISK_NEW) $@ RODS
	$(DISK_GET) $(PLASMA_DEMOS) DEMOS/ROD | $(DISK_PUT) $@ RODPLA REL
	$(DISK_PUT) $@ ROD REL < rod
	$(DISK_PUT) $@ RODFAST REL < rodfast
	$(DISK_LIST) $@

# The PLASMA boot disk for make run: by default, boot.po, a copy of
# PLASMA 2.20's install disk (PLASMA_INST) without its AUTORUN, so that
# it stops at PLASMA's prompt, not the installer. Set BOOT_DISK to boot
# another disk instead (BOOT_DISK=../../,inst.po, say).
PLASMA_INST = ../../images/apple/PLEIADES-2.20-INST.po
BOOT_DISK = boot.po

boot.po: $(PLASMA_INST)
	cp $(PLASMA_INST) $@.tmp
	$(DISK_DEL) $@.tmp AUTORUN
	mv $@.tmp $@

# Run the emulator: RUN_VM DISK1 DISK2 boots DISK1 with DISK2 in the
# second drive. By default, tools/a2run: MAME's 64K enhanced IIe, in a
# window, with MAME's ROM sets from ROMDIR. Set RUN_VM to use another
# emulator.
ROMDIR = ../../mame
RUN_VM = ../tools/a2run $(ROMDIR)

# By default this boots in to the CMD command line, not ProDos' usual.
# CMD's commands are a single character each (docmds(), cmd.pla:1355):
# + module [args] : load and run a PLASMA module, e.g. +/RODS/ROD
# - file          : run a ProDOS SYS program
# C [dir]         : catalog a directory
# P path          : set the prefix, e.g., P /RODS
# /               : move the prefix up one directory
# V               : list the online volumes
# . file          : run the first line of a file as a command (as AUTORUN does)
# For example, you could:
# P /RODS
# +RODS
run: $(BOOT_DISK) $(DISK)
	$(RUN_VM) $(BOOT_DISK) $(DISK)

clean:
	rm -rf rod rodfast lores.obj lores.syms $(DISK) boot.po boot.po.tmp

.PHONY: all clean disk run
