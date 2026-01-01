SUMMARY = "Meta package for building a installable toolchain"
LICENSE = "MIT"

PR = "r7"

LIC_FILES_CHKSUM = "file://${COREBASE}/LICENSE;md5=1cba34dd67f41321df36921342603dc7 \
                    file://${COREBASE}/meta/COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

inherit populate_sdk
