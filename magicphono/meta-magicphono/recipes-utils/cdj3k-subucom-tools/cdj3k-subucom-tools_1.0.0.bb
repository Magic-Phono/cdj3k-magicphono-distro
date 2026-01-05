HOMEPAGE = "https://github.com/Magic-Phono/cdj3k-subucom-tools"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://${S}/LICENSE;md5=39bba7d2cf0ba1036f2a6e2be52fe3f0"
DEPENDS = "ncurses"
SUMMARY = "CDJ3K Subucom Utilities"

SRC_URI = " \
    https://github.com/Magic-Phono/cdj3k-subucom-tools/archive/refs/tags/v${PV}.tar.gz 
"

S = "${WORKDIR}/cdj3k-subucom-tools-${PV}"

inherit autotools

SRC_URI[md5sum] = "01faa745888fb0ddcd2e89b2773a1fb9"
SRC_URI[sha256sum] = "23af49b462e0f73bd76aa093e78e178771b60f65d1929a727c4f972baa94964e"

BBCLASSEXTEND = "nativesdk"
