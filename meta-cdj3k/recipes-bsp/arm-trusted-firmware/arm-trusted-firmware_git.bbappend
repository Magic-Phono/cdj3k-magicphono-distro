FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://asm_macros.patch \
    file://lpddr4.patch \
    file://mod-board-type.patch \
    file://disable-secure-boot.patch \
"

COMPATIBLE_MACHINE = "salvator-x"
export PSCI_DISABLE_BIGLITTLE_IN_CA57BOOT="0"
export RCAR_SA6_TYPE="1"
export RCAR_SECURE_BOOT="0"
