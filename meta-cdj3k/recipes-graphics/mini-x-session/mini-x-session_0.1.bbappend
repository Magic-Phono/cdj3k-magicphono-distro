FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"

SRC_URI_append += "file://mini-x-session"
S = "${WORKDIR}"

RDEPENDS_${PN} = "sudo"

inherit update-alternatives

ALTERNATIVE_${PN} = "x-session-manager"
ALTERNATIVE_TARGET[x-session-manager] = "${bindir}/mini-x-session"
ALTERNATIVE_PRIORITY = "50"

do_install() {
	install -d ${D}/${bindir}
	install -m 0755 ${S}/mini-x-session ${D}/${bindir}
}
