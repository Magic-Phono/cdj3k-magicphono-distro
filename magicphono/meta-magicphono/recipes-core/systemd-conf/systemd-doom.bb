SUMMARY = "DOOM support for systemd"
LICENSE = "CLOSED"


SRC_URI = " \
	file://doom.service \
	file://start_doom.sh \
"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${systemd_unitdir}/system/
	install -m 0644 ${WORKDIR}/doom.service ${D}${systemd_unitdir}/system/

	install -d ${D}/home/root/scripts/
	install -m 0755 ${WORKDIR}/start_doom.sh ${D}/home/root/scripts/

	install -d ${D}${sysconfdir}/systemd/system/graphical.target.wants/
	ln -s ${systemd_unitdir}/system/doom.service ${D}${sysconfdir}/systemd/system/graphical.target.wants/doom.service
}

RDEPENDS_${PN} = "systemd"

# This is a machine specific file
FILES_${PN} = "${systemd_unitdir}/system/*.service ${sysconfdir}"
FILES_${PN} += "/home/root/scripts/start_doom.sh"

PACKAGE_ARCH = "${MACHINE_ARCH}"

SYSTEMD_PACKAGES = "${PN}"
SYSTEMD_SERVICE_${PN} = "doom.service"
SYSTEMD_AUTO_ENABLE_${PN} = "enable"

# As this package is tied to systemd, only build it when we're also building systemd.
python () {
    if not bb.utils.contains ('DISTRO_FEATURES', 'systemd', True, False, d):
        raise bb.parse.SkipPackage("'systemd' not in DISTRO_FEATURES")
}
