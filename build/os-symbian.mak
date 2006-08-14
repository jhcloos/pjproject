export OS_CFLAGS   := $(CC_DEF)PJ_SYMBIAN=1 -include "$(SYMBIAN_SDK_PATH)/include/variant/Symbian_OS_v9.1.hrh" -i- $(CC_INC)$(SYMBIAN_SDK_PATH)/include $(CC_INC)$(SYMBIAN_SDK_PATH)/include/variant $(CC_INC)$(SYMBIAN_SDK_PATH)/include/libc $(CC_INC)../src/pjlib-test -O

export OS_CXXFLAGS   := $(CC_DEF)PJ_SYMBIAN=1 -include "$(SYMBIAN_SDK_PATH)/include/variant/Symbian_OS_v9.1.hrh" -i- $(CC_INC)$(SYMBIAN_SDK_PATH)/include $(CC_INC)$(SYMBIAN_SDK_PATH)/include/variant $(CC_INC)$(SYMBIAN_SDK_PATH)/include/libc $(CC_INC)../src/pjlib-test -O

export OS_LDFLAGS  := 

export OS_SOURCES  :=

export TARGET_EXE := .exe

