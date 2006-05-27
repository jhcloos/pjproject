#
# PJLIB OS specific configuration for Symbian
#

#
# PJLIB_OBJS specified here are object files to be included in PJLIB
# (the library) for this specific operating system. Object files common
# to all operating systems should go in Makefile instead.
#
export PJLIB_OBJS +=

#
# TEST_OBJS are operating system specific object files to be included in
# the test application.
#
export TEST_OBJS +=	main.o

#
# RTEMS_LIBRARY_PATH points to the installed RTEMS libraries for the
# desired target.  pjlib-test can't link without this.
#
#export RTEMS_LIBRARY_PATH := $(RTEMS_LIBRARY_PATH)

#
# Additional LDFLAGS for pjlib-test
#
export TEST_LDFLAGS +=

#
# TARGETS are make targets in the Makefile, to be executed for this given
# operating system.
#
export TARGETS	    =	pjlib pjlib-test.bas

pjlib-test.bas: pjlib-test
	@echo [creating bas]





