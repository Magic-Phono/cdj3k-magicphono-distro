SUMMARY = "DOOM support for systemd"
LICENSE = "CLOSED"


SRC_URI = " 
	file://doom.service \
	file://start_doom.sh \
"

S = "${WORKDIR}"

do_install() {
	install -d ${D}${systemd_unitdir}/system/
	install -m 0644 ${WORKDIR}/doom@.service ${D}${systemd_unitdir}/system/

	install -d ${D}/home/root/scripts/
	install -m 0755 ${WORKDIR}/start_doom.sh ${D}$/home/root/scripts/

}


RDEPENDS_${PN} = "systemd"

# This is a machine specific file
FILES_${PN} = "${systemd_unitdir}/system/*.service ${sysconfdir}"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# As this package is tied to systemd, only build it when we're also building systemd.
python () {
    if not bb.utils.contains ('DISTRO_FEATURES', 'systemd', True, False, d):
        raise bb.parse.SkipPackage("'systemd' not in DISTRO_FEATURES")
}
