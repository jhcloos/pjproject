export CC = mwccsym2 -c
export AR = mwldsym2 -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -library -o 
export LD = mwldsym2.exe -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -stdlib -noimplib -m="?_E32Startup@@YGXXZ" -L"C:\Symbian\7.0s\S80_DP2_0_SDK_CW\epoc32\release\winscw\udeb" -leexe.lib -l"euser.lib" -l"estlib.lib" -l"ecrt0.lib"
export LDOUT = -o 
export RANLIB = echo ranlib

export OBJEXT := .o
export LIBEXT := .lib
export LIBEXT2 :=

export CC_OUT := -o 
export CC_INC := -i 
export CC_DEF := -D
export CC_OPTIMIZE := -O2
export CC_LIB := -l

export CC_SOURCES :=
export CC_CFLAGS := -sym full -wchar_t off -align 4 -enum int -str pool -nostdinc -msgstyle parseable -nowraplines -O0 -D_UNICODE -D__SYMBIAN32__ -D__CW32__ -D__WINS__ -D__WINSCW__ -D_DEBUG -i-
#export CC_CFLAGS += -Wdeclaration-after-statement
export CC_CXXFLAGS := $(CC_CFLAGS)
export CC_LDFLAGS :=

