SUMMARY = "subucom_uinput systemd"
LICENSE = "CLOSED"


SRC_URI = " \
	file://subucom-uinput.service \
"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${systemd_unitdir}/system/
	install -m 0644 ${WORKDIR}/subucom-uinput.service ${D}${systemd_unitdir}/system/

	install -d ${D}${sysconfdir}/systemd/system/sysinit.target.wants/
	ln -s ${systemd_unitdir}/system/subucom-uinput.service ${D}${sysconfdir}/systemd/system/sysinit.target.wants/subucom-uinput.service
}

RDEPENDS_${PN} = "systemd"

# This is a machine specific file
FILES_${PN} = "${systemd_unitdir}/system/*.service ${sysconfdir}"

PACKAGE_ARCH = "${MACHINE_ARCH}"

SYSTEMD_SERVICE_${PN} = "subucom-uinput.service"
SYSTEMD_AUTO_ENABLE_${PN} = "enable"

# As this package is tied to systemd, only build it when we're also building systemd.
python () {
    if not bb.utils.contains ('DISTRO_FEATURES', 'systemd', True, False, d):
        raise bb.parse.SkipPackage("'systemd' not in DISTRO_FEATURES")
}
