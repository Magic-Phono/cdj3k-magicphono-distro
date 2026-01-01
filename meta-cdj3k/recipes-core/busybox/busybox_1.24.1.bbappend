FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://enable-infinite-lease-time.patch \
    file://cmd-option.cfg \
"

