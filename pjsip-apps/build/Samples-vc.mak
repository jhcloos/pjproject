
TARGET = i386-win32-vc$(VC_VER)-$(BUILD_MODE)
LIBEXT = .lib

!if "$(BUILD_MODE)" == "debug"
BUILD_FLAGS = /MTd /Od /Zi /W4
!else
BUILD_FLAGS = /Ox /MD /DNDEBUG /W4
!endif

PJLIB_LIB = ..\..\pjlib\lib\pjlib-$(TARGET)$(LIBEXT)
PJLIB_UTIL_LIB = ..\..\pjlib-util\lib\pjlib-util-$(TARGET)$(LIBEXT)
PJNATH_LIB = ..\..\pjnath\lib\pjnath-$(TARGET)$(LIBEXT)
PJMEDIA_LIB = ..\..\pjmedia\lib\pjmedia-$(TARGET)$(LIBEXT)
PJMEDIA_CODEC_LIB = ..\..\pjmedia\lib\pjmedia-codec-$(TARGET)$(LIBEXT)
PJSIP_LIB = ..\..\pjsip\lib\pjsip-core-$(TARGET)$(LIBEXT)
PJSIP_UA_LIB = ..\..\pjsip\lib\pjsip-ua-$(TARGET)$(LIBEXT)
PJSIP_SIMPLE_LIB = ..\..\pjsip\lib\pjsip-simple-$(TARGET)$(LIBEXT)
PJSUA_LIB_LIB = ..\..\pjsip\lib\pjsua-lib-$(TARGET)$(LIBEXT)

LIBS = $(PJSUA_LIB_LIB) $(PJSIP_UA_LIB) $(PJSIP_SIMPLE_LIB) \
	  $(PJSIP_LIB) $(PJNATH_LIB) $(PJMEDIA_CODEC_LIB) $(PJMEDIA_LIB) \
	  $(PJLIB_UTIL_LIB) $(PJLIB_LIB)

CFLAGS 	= /DPJ_WIN32=1 /DPJ_M_I386=1 \
	  $(BUILD_FLAGS) \
	  -I..\..\pjsip\include \
	  -I..\..\pjlib\include -I..\..\pjlib-util\include \
	  -I..\..\pjmedia\include \
	  -I..\..\pjnath/include
LDFLAGS = $(BUILD_FLAGS) $(LIBS) \
	  ole32.lib user32.lib dsound.lib dxguid.lib netapi32.lib \
	  mswsock.lib ws2_32.lib 

SRCDIR = ..\src\samples
OBJDIR = .\output\samples-$(TARGET)
BINDIR = ..\bin\samples


SAMPLES = $(BINDIR)\confsample.exe \
	  $(BINDIR)\confbench.exe \
	  $(BINDIR)\level.exe \
	  $(BINDIR)\pjsip-perf.exe \
	  $(BINDIR)\playfile.exe \
	  $(BINDIR)\playsine.exe\
	  $(BINDIR)\recfile.exe  \
	  $(BINDIR)\resampleplay.exe \
	  $(BINDIR)\simpleua.exe \
	  $(BINDIR)\simple_pjsua.exe \
	  $(BINDIR)\siprtp.exe \
	  $(BINDIR)\sipstateless.exe \
	  $(BINDIR)\sndinfo.exe \
	  $(BINDIR)\sndtest.exe \
	  $(BINDIR)\streamutil.exe \
	  $(BINDIR)\tonegen.exe


all: $(OBJDIR) $(SAMPLES)

$(SAMPLES): $(SRCDIR)\$(@B).c $(LIBS) $(SRCDIR)\util.h Samples-vc.mak
	cl -nologo -c $(SRCDIR)\$(@B).c /Fo$(OBJDIR)\$(@B).obj $(CFLAGS) 
	cl /nologo $(OBJDIR)\$(@B).obj /Fe$@ /Fm$(OBJDIR)\$(@B).map $(LDFLAGS)

$(OBJDIR):
	if not exist $(OBJDIR) mkdir $(OBJDIR)

clean:
	echo Cleaning up samples...
	if exist $(BINDIR) del /Q $(BINDIR)\*.*
	if exist $(OBJDIR) del /Q $(OBJDIR)\*.*

