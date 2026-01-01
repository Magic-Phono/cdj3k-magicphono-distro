FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://mod_rcar-gen3-common.patch \
    file://mod_env_default.patch \
    file://mod_r8a7796_salvator-x.patch \
    file://mod_salvator-x.patch \
    file://tools/Realtek/* \
    file://ether_phy.patch \
    file://ether_test.patch \
    file://mod_Makefile.patch \
    file://mod_board_r.patch \
    file://mod_sh_pfc.patch \
    file://mod_autoboot.patch \
    file://modify-pfc.patch \
"
do_compile_prepend(){
    mkdir -p ${WORKDIR}/git/tools/Realtek
    cp -f ${WORKDIR}/tools/Realtek/* ${WORKDIR}/git/tools/Realtek
}
