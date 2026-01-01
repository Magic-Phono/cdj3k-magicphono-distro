FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://r8a7796_salvator-x_defconfig \
    file://fw_env_config.patch \
"

do_compile_prepend(){
    cp -f ${WORKDIR}/r8a7796_salvator-x_defconfig ${WORKDIR}/git/configs
}

