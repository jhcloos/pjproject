export CC = mwccsym2 -c
export AR = ar rv
export LD = mwldsym2.exe -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -addcommand export:_E32Dll\=__E32Dll -shared -export dllexport -stdlib -L"C:\Symbian\9.1\S60_3rd\Epoc32\release\winscw\udeb" -L"C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\Runtime\Runtime_x86\Runtime_Win32\Libs" -ledll.lib -leuser.lib -lestlib.lib

#mwldsym2.exe -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -stdlib -L"C:\Symbian\9.1\S60_3rd\Epoc32\release\winscw\udeb" -L"C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\Runtime\Runtime_x86\Runtime_Win32\Libs"
#DLL -addcommand export:_E32Dll\=__E32Dll -shared -export dllexport 
#o
# -ledll.lib -leuser.lib -lestlib.lib
export LDEXE = mwldsym2.exe -sym full -subsystem windows -msgstyle parseable -nowraplines -nodefaultlibs -stdlib -L"C:\Symbian\9.1\S60_3rd\epoc32\release\winscw\udeb" -L"C:\dev\pj\symbian\pjlib\lib" -L"C:\project\symbian\pjlib\lib" -noimplib -m="?_E32Bootstrap@@YGXXZ"
export LDOUT = -o 
export RANLIB = ranlib

export OBJEXT := .o
export STATICLIBEXT := .lib
export LIBEXT := .dll
export LIBEXT2 :=

export CC_OUT := -o 
export CC_INC := -i 
export CC_DEF := -D
export CC_OPTIMIZE := -O2
export CC_LIB := -l

export CC_SOURCES :=
export CC_CFLAGS := -sym full -wchar_t off -align 4 -enum int -str pool -nostdinc -exc ms -inline off -msgstyle parseable -nowraplines -O0 -D_UNICODE -D__SYMBIAN32__ -D__CW32__ -D__WINS__ -D__DLL__ -D__WINSCW__ -D_DEBUG -D__SUPPORT_CPP_EXCEPTIONS__ -D__SERIES60_30__ -D__SERIES60_3X__ -w cmdline -w pragmas -w empty -w possible -w unusedvar -w extracomma -w pedantic -w largeargs -w ptrintconv -w tokenpasting -w missingreturn
#export CC_CFLAGS += -Wdeclaration-after-statement
export CC_CXXFLAGS := -sym full -wchar_t off -align 4 -enum int -str pool -nostdinc -exc ms -inline off -msgstyle parseable -nowraplines -O0 -D_UNICODE -D__SYMBIAN32__ -D__CW32__ -D__WINS__ -D__DLL__ -D__WINSCW__ -D_DEBUG -D__SUPPORT_CPP_EXCEPTIONS__ -D__SERIES60_30__ -D__SERIES60_3X__ -w cmdline -w pragmas -w empty -w possible -w unusedvar -w extracomma -w pedantic -w largeargs -w ptrintconv -w tokenpasting -w missingreturn
export CC_LDFLAGS :=
export MWLibraries=C:\dev\pj\symbian\pjlib\lib;C:\project\symbian\pjlib\lib;C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\Win32-x86 Support\Libraries\Win32 SDK;C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\MSL\MSL_C\MSL_Common\Include;C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\MSL\MSL_C\MSL_Win32\Include;C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\MSL\MSL_C++\MSL_Common\Include;C:\Program Files\Carbide\plugins\com.nokia.carbide.cpp.support_1.0.0\Symbian_Support\Runtime\Runtime_x86\Runtime_Win32\Libs
export MWSym2LibraryFiles=libpj-symbian.lib;gdi32.lib;user32.lib;kernel32.lib;MSL_All_MSE_Symbian.lib;MSL_All_x86.lib;MSL_All_x86_Symbian.lib

