export CC = mwccsym2 -c
export AR = mwldsym2 -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -library -o 
export LD = mwldsym2.exe -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -stdlib -noimplib -L"C:\Symbian\7.0s\S80_DP2_0_SDK_CW\epoc32\release\winscw\udeb" -lestlib.lib -leuser.lib -ledll.lib -search -m "?_E32Dll@@YGHPAXI0@Z"
export LDOUT = -o 
export RANLIB = echo ranlib

export OBJEXT := .o
export LIBEXT := .dll
export LIBEXT2 :=

export CC_OUT := -o 
export CC_INC := -i 
export CC_DEF := -D
export CC_OPTIMIZE := -O2
export CC_LIB := -l

export CC_SOURCES :=
export CC_CFLAGS := -sym full -wchar_t off -align 4 -enum int -str pool -nostdinc -exc ms -inline off -msgstyle parseable -nowraplines -O0 -D_UNICODE -D__SYMBIAN32__ -D__CW32__ -D__WINS__ -D__DLL__ -D__WINSCW__ -D_DEBUG -D__SUPPORT_CPP_EXCEPTIONS__ -D__SERIES60_30__ -D__SERIES60_3X__ -w cmdline -w pragmas -w empty -w possible -w unusedarg -w unusedvar -w extracomma -w pedantic -w largeargs -w ptrintconv -w tokenpasting -w missingreturn
#-include "C:\Symbian\9.1\S60_3rd\epoc32\include\variant\Symbian_OS_v9.1.hrh" -i "C:\dev\symbian\TLS1dll" -i- -i "C:\Symbian\9.1\S60_3rd\epoc32\include" -i "C:\Symbian\9.1\S60_3rd\epoc32\include\variant" 
#export CC_CFLAGS += -Wdeclaration-after-statement
export CC_CXXFLAGS := 
export CC_LDFLAGS :=

