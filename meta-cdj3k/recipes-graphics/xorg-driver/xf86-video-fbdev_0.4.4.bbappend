FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"
SRC_URI_append += " \
    file://rcar_du_drm.h \
    file://flip_control.c \
    file://flip_control.h \
    file://enable-vsync.patch \
"

do_compile_prepend(){
    cp -f ${WORKDIR}/rcar_du_drm.h ${TOPDIR}/tmp/sysroots/salvator-x/usr/include/drm
    cp -f ${WORKDIR}/flip_control.c ${WORKDIR}/xf86-video-fbdev-0.4.4/src
    cp -f ${WORKDIR}/flip_control.h ${WORKDIR}/xf86-video-fbdev-0.4.4/src
}