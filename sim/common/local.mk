## See sim/Makefile.am.
##
## Copyright (C) 1997-2023 Free Software Foundation, Inc.
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.

## Parts of the common/ sim code that have been unified.

AM_CPPFLAGS += \
	-I$(srcdir)/%D% \
	-DSIM_TOPDIR_BUILD
AM_CPPFLAGS_%C% = -DSIM_COMMON_BUILD
AM_CPPFLAGS_FOR_BUILD += -I$(srcdir)/%D%

## NB: libcommon.a isn't used directly by ports.  We need a target for common
## objects to be a part of, and ports use the individual objects directly.
## We can delete this once ppc/Makefile.in is merged into ppc/local.mk.
noinst_LIBRARIES += %D%/libcommon.a
%C%_libcommon_a_SOURCES = \
	%D%/callback.c \
	%D%/portability.c \
	%D%/sim-load.c \
	%D%/sim-signal.c \
	%D%/syscall.c \
	%D%/target-newlib-errno.c \
	%D%/target-newlib-open.c \
	%D%/target-newlib-signal.c \
	%D%/target-newlib-syscall.c \
	%D%/version.c

%D%/version.c: %D%/version.c-stamp ; @true
%D%/version.c-stamp: $(srcroot)/gdb/version.in $(srcroot)/bfd/version.h $(srcdir)/%D%/create-version.sh
	$(AM_V_GEN)$(SHELL) $(srcdir)/%D%/create-version.sh $(srcroot)/gdb $@.tmp
	$(AM_V_at)$(SHELL) $(srcroot)/move-if-change $@.tmp $(@:-stamp=)
	$(AM_V_at)touch $@

CLEANFILES += \
	%D%/version.c %D%/version.c-stamp

## NB: This is a bit of a hack.  If we can generalize the common/ files, we can
## turn this from an arch-specific %/test-hw-events into a common/test-hw-events
## program.
.PRECIOUS: %/test-hw-events.o
%/test-hw-events.o: common/hw-events.c
	$(AM_V_CC)$(COMPILE) -DMAIN -c -o $@ $<
%/test-hw-events: %/test-hw-events.o %/libsim.a
	$(AM_V_CCLD)$(LINK) -o $@ $^ $(SIM_COMMON_LIBS) $(LIBS)

##
## For subdirs.
##

SIM_COMMON_HW_OBJS = \
	hw-alloc.o \
	hw-base.o \
	hw-device.o \
	hw-events.o \
	hw-handles.o \
	hw-instances.o \
	hw-ports.o \
	hw-properties.o \
	hw-tree.o \
	sim-hw.o

SIM_NEW_COMMON_OBJS = \
	sim-arange.o \
	sim-bits.o \
	sim-close.o \
	sim-command.o \
	sim-config.o \
	sim-core.o \
	sim-cpu.o \
	sim-endian.o \
	sim-engine.o \
	sim-events.o \
	sim-fpu.o \
	sim-hload.o \
	sim-hrw.o \
	sim-io.o \
	sim-info.o \
	sim-memopt.o \
	sim-model.o \
	sim-module.o \
	sim-options.o \
	sim-profile.o \
	sim-reason.o \
	sim-reg.o \
	sim-stop.o \
	sim-syscall.o \
	sim-trace.o \
	sim-utils.o \
	sim-watch.o

SIM_HW_DEVICES = cfi core pal glue

if SIM_ENABLE_HW
SIM_NEW_COMMON_OBJS += \
	$(SIM_COMMON_HW_OBJS) \
	$(SIM_HW_SOCKSER)
endif

# FIXME This is one very simple-minded way of generating the file hw-config.h.
%/hw-config.h: %/stamp-hw ; @true
%/stamp-hw: Makefile
	$(AM_V_GEN)set -e; \
	( \
	sim_hw="$(SIM_HW_DEVICES) $($(@D)_SIM_EXTRA_HW_DEVICES)" ; \
	echo "/* generated by Makefile */" ; \
	printf "extern const struct hw_descriptor dv_%s_descriptor[];\n" $$sim_hw ; \
	echo "const struct hw_descriptor * const hw_descriptors[] = {" ; \
	printf "  dv_%s_descriptor,\n" $$sim_hw ; \
	echo "  NULL," ; \
	echo "};" \
	) > $@.tmp; \
	$(SHELL) $(srcroot)/move-if-change $@.tmp $(@D)/hw-config.h; \
	touch $@
.PRECIOUS: %/stamp-hw

MOSTLYCLEANFILES += $(SIM_ENABLED_ARCHES:%=%/hw-config.h) $(SIM_ENABLED_ARCHES:%=%/stamp-hw)

## See sim_pre_argv_init and sim_module_install in sim-module.c for more details.
## TODO: Switch this to xxx_SOURCES once projects build objects in local.mk.
am_arch_d = $(subst -,_,$(@D))
GEN_MODULES_C_SRCS = \
	$(wildcard \
		$(patsubst %,$(srcdir)/%,$($(am_arch_d)_libsim_a_SOURCES)) \
		$(patsubst %.o,$(srcdir)/%.c,$($(am_arch_d)_libsim_a_OBJECTS) $($(am_arch_d)_libsim_a_LIBADD)) \
		$(filter-out %.o,$(patsubst $(@D)/%.o,$(srcdir)/common/%.c,$($(am_arch_d)_libsim_a_LIBADD))))
%/modules.c: %/stamp-modules ; @true
%/stamp-modules: Makefile
	$(AM_V_GEN)set -e; \
	LANG=C ; export LANG; \
	LC_ALL=C ; export LC_ALL; \
	sed -n -e '/^sim_install_/{s/^\(sim_install_[a-z_0-9A-Z]*\).*/\1/;p}' $(GEN_MODULES_C_SRCS) | sort >$@.l-tmp; \
	( \
	echo '/* Do not modify this file.  */'; \
	echo '/* It is created automatically by the Makefile.  */'; \
	echo '#include "libiberty.h"'; \
	echo '#include "sim-module.h"'; \
	sed -e 's:\(.*\):extern MODULE_INIT_FN \1;:' $@.l-tmp; \
	echo 'MODULE_INSTALL_FN * const sim_modules_detected[] = {'; \
	sed -e 's:\(.*\):  \1,:' $@.l-tmp; \
	echo '};'; \
	echo 'const int sim_modules_detected_len = ARRAY_SIZE (sim_modules_detected);'; \
	) >$@.tmp; \
	$(SHELL) $(srcroot)/move-if-change $@.tmp $(@D)/modules.c; \
	rm -f $@.l-tmp; \
	touch $@
.PRECIOUS: %/stamp-modules

## NB: The ppc port doesn't currently utilize the modules API, so skip it.
MOSTLYCLEANFILES += $(SIM_ENABLED_ARCHES:%=%/modules.c) $(SIM_ENABLED_ARCHES:%=%/stamp-modules)

LIBIBERTY_LIB = ../libiberty/libiberty.a
BFD_LIB = ../bfd/libbfd.la
OPCODES_LIB = ../opcodes/libopcodes.la

SIM_COMMON_LIBS = \
	$(BFD_LIB) \
	$(OPCODES_LIB) \
	$(LIBIBERTY_LIB) \
	$(LIBGNU) \
	$(LIBGNU_EXTRA_LIBS)

##
## CGEN support.
##

## If the local tree has a bundled copy of guile, use that.
GUILE = $(or $(wildcard ../guile/libguile/guile),guile)
CGEN = "$(GUILE) -l $(cgendir)/guile.scm -s"
CGENFLAGS = -v
CGEN_CPU_DIR = $(cgendir)/cpu
## Most ports use the files here instead of cgen/cpu.
CPU_DIR = $(srcroot)/cpu
CGEN_ARCHFILE = $(CPU_DIR)/$(@D).cpu

CGEN_READ_SCM = $(cgendir)/sim.scm
CGEN_ARCH_SCM = $(cgendir)/sim-arch.scm
CGEN_CPU_SCM = $(cgendir)/sim-cpu.scm $(cgendir)/sim-model.scm
CGEN_DECODE_SCM = $(cgendir)/sim-decode.scm
CGEN_DESC_SCM = $(cgendir)/desc.scm $(cgendir)/desc-cpu.scm

## Various choices for which cpu specific files to generate.
## These are passed to cgen.sh in the "extrafiles" argument.
CGEN_CPU_EXTR = /extr/
CGEN_CPU_READ = /read/
CGEN_CPU_WRITE = /write/
CGEN_CPU_SEM = /sem/
CGEN_CPU_SEMSW = /semsw/

CGEN_WRAPPER = $(srccom)/cgen.sh

CGEN_GEN_ARCH = \
	$(SHELL) $(CGEN_WRAPPER) arch $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" ignored "$$isa" $$mach ignored \
		$(CGEN_ARCHFILE) ignored
CGEN_GEN_CPU = \
	$(SHELL) $(CGEN_WRAPPER) cpu $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" $$cpu "$$isa" $$mach "$$SUFFIX" \
		$(CGEN_ARCHFILE) "$$EXTRAFILES"
CGEN_GEN_DEFS = \
	$(SHELL) $(CGEN_WRAPPER) defs $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" $$cpu "$$isa" $$mach "$$SUFFIX" \
		$(CGEN_ARCHFILE) ignored
CGEN_GEN_DECODE = \
	$(SHELL) $(CGEN_WRAPPER) decode $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" $$cpu "$$isa" $$mach "$$SUFFIX" \
		$(CGEN_ARCHFILE) "$$EXTRAFILES"
CGEN_GEN_CPU_DECODE = \
	$(SHELL) $(CGEN_WRAPPER) cpu-decode $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" $$cpu "$$isa" $$mach "$$SUFFIX" \
		$(CGEN_ARCHFILE) "$$EXTRAFILES"
CGEN_GEN_CPU_DESC = \
	$(SHELL) $(CGEN_WRAPPER) desc $(srcdir)/$(@D) \
		$(CGEN) $(cgendir) "$(CGENFLAGS)" \
		$(@D) "$$FLAGS" $$cpu "$$isa" $$mach "$$SUFFIX" \
		$(CGEN_ARCHFILE) ignored $$opcfile
