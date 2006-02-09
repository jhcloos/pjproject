#
# OS specific configuration for Win32 OS target. 
#

#
# PJMEDIA_OBJS specified here are object files to be included in PJMEDIA
# (the library) for this specific operating system. Object files common 
# to all operating systems should go in Makefile instead.
#
export PJMEDIA_OBJS += $(PA_DIR)/pa_win_hostapis.o $(PA_DIR)/pa_win_util.o \
		       $(PA_DIR)/pa_win_ds.o

export OS_CFLAGS += -DPA_NO_ASIO
