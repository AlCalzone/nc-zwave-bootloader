#!/bin/bash
# Required env variables:
# SDK: Path to SDK root
#
# Optional env variables:
# SLC: Path to SLC-CLI binary (default: slc)
# COMMANDER: Path to Simplicity Commander binary (default: commander)
# TOOLCHAIN: Path to the ARM toolchain root (default: found under /opt)

if [ -z "${SDK}" ]; then
	echo "ERROR: env variable SDK must be set to SDK root"
	exit 1
fi

SLC=${SLC:-slc}
COMMANDER=${COMMANDER:-commander}

POST_BUILD_EXE=tools/mkgbl.sh
PROJ_NAME=nc_controller_bootloader_otw

rm -rf build/
rm -f *.Makefile
rm -f *.mak

$SLC signature trust --sdk $SDK

# Find the toolchain unless one was provided
if [ -z "${TOOLCHAIN}" ]; then
	TOOLCHAIN=$(find /opt -type d -name "*arm-none-eabi*" | head -n 1)
fi
echo "Using toolchain: $TOOLCHAIN"

$SLC generate \
	--project-file $PROJ_NAME.slcp \
	--export-destination build/ \
	--sdk "$SDK" \
	--copy-sources \
	--toolchain toolchain_gcc \
	--output-type makefile

cp build/*.Makefile ./
cp build/*.mak ./

make release -B -f $PROJ_NAME.Makefile POST_BUILD_EXE=$POST_BUILD_EXE ARM_GCC_DIR=$TOOLCHAIN