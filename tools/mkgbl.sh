#!/bin/bash
# Required env variables:
# COMMANDER: Path to Simplicity Commander binary

BUILD_OUTPUT=build/release/nc_controller_bootloader_otw.hex
OUTFILE=artifact/zwa2_bootloader.gbl
SIGN_KEY=keys/vendor_sign.key
ENC_KEY=keys/vendor_encrypt.key

mkdir -p $(dirname $OUTFILE)

$COMMANDER gbl create $OUTFILE --bootloader $BUILD_OUTPUT --sign $SIGN_KEY --encrypt $ENC_KEY --compress lzma