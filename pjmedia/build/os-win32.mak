#
# OS specific configuration for Win32 OS target. 
#

#
# PJMEDIA_OBJS specified here are object files to be included in PJMEDIA
# (the library) for this specific operating system. Object files common 
# to all operating systems should go in Makefile instead.
#
export PJMEDIA_OBJS += $(PA_DIR)/pa_win_hostapis.o $(PA_DIR)/pa_win_util.o \
		       $(PA_DIR)/pa_win_wmme.o

export OS_CFLAGS += -DPA_NO_ASIO -DPA_NO_DS \
		    -DPJMEDIA_SOUND_IMPLEMENTATION=PJMEDIA_SOUND_PORTAUDIO_SOUND

# Example:
#  to activate Null sound, uncomment these two lines below.
#export SOUND_OBJS = $(NULLSOUND_OBJS)
#export OS_CFLAGS += $(CC_DEF)PJMEDIA_HAS_NULL_SOUND

# Example:
#  to include only GSM and exclude Speex codec, uncomment the "export" line below.
#  Note that you'll need to put "#define PJMEDIA_HAS_SPEEX_CODEC 0" in
#  <pj/config_site.h>
#export CODEC_OBJS = $(GSM_OBJS)

