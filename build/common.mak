#
# Include host/target/compiler selection.
# This will export CC_NAME, MACHINE_NAME, OS_NAME, and HOST_NAME variables.
#
include ../../build.mak

#
# Include global compiler specific definitions
#
include ../../build/cc-$(CC_NAME).mak

#
# (Optionally) Include compiler specific configuration that is
# specific to this project. This configuration file is
# located in this directory.
#
-include cc-$(CC_NAME).mak

#
# Include global machine specific definitions
#
include ../../build/m-$(MACHINE_NAME).mak
-include m-$(MACHINE_NAME).mak

#
# Include target OS specific definitions
#
include ../../build/os-$(OS_NAME).mak

#
# (Optionally) Include target OS specific configuration that is
# specific to this project. This configuration file is
# located in this directory.
#
-include os-$(OS_NAME).mak

#
# Include host specific definitions
#
include ../../build/host-$(HOST_NAME).mak

#
# (Optionally) Include host specific configuration that is
# specific to this project. This configuration file is
# located in this directory.
#
-include host-$(HOST_NAME).mak

#
# Include global user configuration, if any
#
-include ../../user.mak


