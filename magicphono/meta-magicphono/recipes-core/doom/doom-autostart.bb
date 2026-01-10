SUMMARY = "DOOM autostart"

LICENSE="CLOSED"

RDEPENDS_${PN} = "mini-x-session"

RDEPENDS_${PN} = "bash"

SRC_URI_append = " file://.xinitrc \
    "
FILES_${PN} += " /home/root/.xinitrc"

do_install() {
    install -d ${D}/home/root/
    install -m 644 ${WORKDIR}/.xinitrc ${D}/home/root/
}
