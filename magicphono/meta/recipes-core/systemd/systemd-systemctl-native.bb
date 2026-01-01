SUMMARY = "Wrapper for enabling systemd services"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COREBASE}/LICENSE;md5=1cba34dd67f41321df36921342603dc7"

PR = "r6"

inherit native

SRC_URI = "file://systemctl"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${bindir}
	install -m 0755 ${WORKDIR}/systemctl ${D}${bindir}
}
