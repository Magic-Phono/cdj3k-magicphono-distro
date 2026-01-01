FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://modify-path-util.patch \
"
