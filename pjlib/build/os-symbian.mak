#
# PJLIB OS specific configuration for Symbian
#

#
# PJLIB_OBJS specified here are object files to be included in PJLIB
# (the library) for this specific operating system. Object files common
# to all operating systems should go in Makefile instead.
#
export PJLIB_OBJS += addr_resolv_sock.o file_access_unistd.o \
			file_io_ansi.o guid_simple.o \
			log_writer_stdout.o os_core_symbian.o \
			os_error_unix.o os_time_unix.o \
			os_timestamp_common.o os_timestamp_posix.o \
			pool_policy_malloc.o compat/string.o sock_bsd.o sock_select.o 

#
# TEST_OBJS are operating system specific object files to be included in
# the test application.
#
export TEST_OBJS += main_symbian.o

#
# RTEMS_LIBRARY_PATH points to the installed RTEMS libraries for the
# desired target.  pjlib-test can't link without this.
#
#export RTEMS_LIBRARY_PATH := $(RTEMS_LIBRARY_PATH)

#
# Additional LDFLAGS for pjlib-test
#
#-L"C:\project\symbian\pjlib\lib" -
export TEST_LDFLAGS +=  -leexe.lib -leuser.lib -L. -lpj-symbian.lib

#
# TARGETS are make targets in the Makefile, to be executed for this given
# operating system.
#
export TARGETS	    =	pjlib pjlib-test


