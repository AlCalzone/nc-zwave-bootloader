#!/bin/bash
# Required env variables:
# SLC: Path to SLC-CLI
# SDK: Path to SDK root
# COMMANDER: Path to Simplicity Commander binary

if [ -z "${SLC}" ]; then
	echo "ERROR: env variable SLC must be set to SLC-CLI binary"
	exit 1
fi

if [ -z "${SDK}" ]; then
	echo "ERROR: env variable SDK must be set to SDK root"
	exit 1
fi

if [ -z "${COMMANDER}" ]; then
	echo "ERROR: env variable COMMANDER must be set to Simplicity Commander binary"
	exit 1
fi

POST_BUILD_EXE=tools/mkgbl.sh
PROJ_NAME=nc_controller_bootloader_otw

rm -rf build/
rm -f *.Makefile
rm -f *.mak

$SLC generate \
	--project-file $PROJ_NAME.slcp \
	--export-destination build/ \
	--sdk "$SDK" \
	--copy-sources \
	--output-type makefile

cp build/*.Makefile ./
cp build/*.mak ./

make release -B -f $PROJ_NAME.Makefile POST_BUILD_EXE=$POST_BUILD_EXE