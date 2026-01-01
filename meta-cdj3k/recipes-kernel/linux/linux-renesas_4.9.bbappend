FILESEXTRAPATHS_prepend :="${THISDIR}/${PN}:"

SRC_URI_append += " \
    file://files/ \
    file://patches.scc \
    file://configs.scc \
"
addtask copy_files after do_kernel_checkout before do_patch
do_copy_files(){
    cp -fr ${WORKDIR}/files/rtl8367 ${S}/drivers/net/phy
    cp -f ${WORKDIR}/files/r8a779x_usb3_v3.dlmem ${S}/firmware
    cp -f ${WORKDIR}/files/ak4490.* ${S}/sound/soc/codecs
    cp -f ${WORKDIR}/files/fancont.c ${S}/drivers/thermal
    cp -f ${WORKDIR}/files/gpiodrv.c ${S}/drivers/gpio
    cp -f ${WORKDIR}/files/gpiodrv.h ${S}/include/linux/gpio
    cp -f ${WORKDIR}/files/subucom_spi.* ${S}/drivers/spi
    cp -f ${WORKDIR}/files/udev_*.c ${S}/fs/proc
}

do_compile_prepend(){
    cp -f ${WORKDIR}/files/logo_linux_clut224.ppm ${S}/drivers/video/logo
    cp -f ${WORKDIR}/files/u_audio.* ${S}/drivers/usb/gadget/function
    cp -f ${WORKDIR}/files/f_uac2.c ${S}/drivers/usb/gadget/function
    cp -f ${WORKDIR}/files/u_uac2.h ${S}/drivers/usb/gadget/function
}

addtask revision_check after do_deploy before do_build
do_revision_check(){
    cd ${TOPDIR}/tmp/deploy/images/salvator-x
    rm -fr boot
    rm -f images.tar.gz
    mkdir -p boot
    cp -fv Image-initramfs-salvator-x.bin boot/Image
    cp -fv Image-r8a7796-salvator-x.dtb boot
    tar czvf images.tar.gz boot/Image boot/Image-r8a7796-salvator-x.dtb
}

addtask delete_output after do_clean before do_cleansstate
do_delete_output(){
    cd ${TOPDIR}/tmp/deploy/images/salvator-x
    rm -fr boot
    rm -f system.rev
    rm -f images.tar.gz
    rm -f core-image-x11-salvator-x*
}
