#!/bin/sh

mkdir -p build/conf

cat <<EOT > build/conf/local.conf
DISTRO ?= "magicphono"

# Supported values are i686 and x86_64
#SDKMACHINE ?= "i686"
SDKMACHINE ?= "x86_64"

MACHINE ??= "salvator-x"
SOC_FAMILY = "r8a7796"
EOT
