SUMMARY = "USB automount support for systemd"
LICENSE = "CLOSED"


SRC_URI = " \
	file://usb-mount@.service \
	file://usb-mount.sh \
"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${systemd_unitdir}/system/
	install -m 0644 ${WORKDIR}/usb-mount@.service ${D}${systemd_unitdir}/system/

	install -d ${D}/usr/local/bin/
	install -m 0755 ${WORKDIR}/usb-mount.sh ${D}/usr/local/bin/
}

RDEPENDS_${PN} = "systemd"

# This is a machine specific file
FILES_${PN} = "${systemd_unitdir}/system/*.service ${sysconfdir}"
FILES_${PN} += "/usr/local/bin/usb-mount.sh"

PACKAGE_ARCH = "${MACHINE_ARCH}"

SYSTEMD_PACKAGES = "${PN}
SYSTEMD_SERVICE_${PN} = "usb-mount@"
SYSTEMD_AUTO_ENABLE_${PN} = "enable"

# As this package is tied to systemd, only build it when we're also building systemd.
python () {
    if not bb.utils.contains ('DISTRO_FEATURES', 'systemd', True, False, d):
        raise bb.parse.SkipPackage("'systemd' not in DISTRO_FEATURES")
}
