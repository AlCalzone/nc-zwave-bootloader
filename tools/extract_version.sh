#!/bin/bash
# Prints the bootloader version as major.minor.customer, the same three fields
# the SDK packs into BOOTLOADER_VERSION_MAIN and reports to the host.
#
# Required env variables:
# SDK: Path to SDK root

set -euo pipefail

if [ -z "${SDK:-}" ]; then
	echo "ERROR: env variable SDK must be set to SDK root" >&2
	exit 1
fi

# Major and minor belong to the SDK, so read them from the SDK in use
SDK_CONFIG="$SDK/platform/bootloader/config/btl_config.h"
PROJECT_CONFIG=config/btl_core_cfg.h

read_define() {
	sed -nE "s/^#define[[:space:]]+$2[[:space:]]+([0-9]+).*/\1/p" "$1" | head -n 1
}

MAJOR=$(read_define "$SDK_CONFIG" BOOTLOADER_VERSION_MAIN_MAJOR || true)
MINOR=$(read_define "$SDK_CONFIG" BOOTLOADER_VERSION_MAIN_MINOR || true)
CUSTOMER=$(read_define "$PROJECT_CONFIG" BOOTLOADER_VERSION_MAIN_CUSTOMER || true)

if [ -z "$MAJOR" ] || [ -z "$MINOR" ] || [ -z "$CUSTOMER" ]; then
	echo "ERROR: failed to extract bootloader version" >&2
	exit 1
fi

echo "$MAJOR.$MINOR.$CUSTOMER"
