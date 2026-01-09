SUMMARY = "DOOM autostart"

LICENSE="CLOSED"

RDEPENDS:${PN} = "mini-x-session"

SRC_URI:append = " file://session \
                   file://start_doom.sh \
    "
FILES:${PN} += " ${sysconfdir}/mini_x/session"
FILES_${PN} += " /home/root/scripts/start_doom.sh"

do_install() {
    install -d ${D}${sysconfdir}/mini_x
    install -m 755 ${WORKDIR}/session ${D}${sysconfdir}/mini_x/session

	install -d ${D}/home/root/scripts/
	install -m 0755 ${WORKDIR}/start_doom.sh ${D}/home/root/scripts/
}