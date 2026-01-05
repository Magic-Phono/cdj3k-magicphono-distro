HOMEPAGE = "https://github.com/Magic-Phono/cdj3k-subucom-tools"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=39bba7d2cf0ba1036f2a6e2be52fe3f0"
DEPENDS = "ncurses"
SUMMARY = "CDJ3K Subucom Utilities"

SRC_URI = " https://github.com/Magic-Phono/cdj3k-subucom-tools/archive/refs/tags/v${PV}.tar.gz "

S = "${WORKDIR}/cdj3k-subucom-tools-${PV}"

inherit autotools

SRC_URI[md5sum] = "cfb2a5cdb74186e9d71ffe220b543581"
SRC_URI[sha256sum] = "c214fd3a4ef451b48a21f081eaebbaf8144916dd3e821c2c412241098f186771"

BBCLASSEXTEND = "native"
