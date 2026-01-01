FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"

SRC_URI_append += " \
    file://enable-endless.patch \
"

DEBUG_BUILD="1"
