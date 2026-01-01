require ../../../meta-cdj3k/recipes-graphics/images/core-image-renesas-base.inc

IMAGE_FEATURES += " \
    ssh-server-openssh \
"

IMAGE_INSTALL_append += " \
    procps \
    curl \
    alsa-lib \
    alsa-utils \
    libbsd \
    neon \
    libpthread-stubs \
    mmc-utils \
    dosfstools \
    bzip2 \
    freetype \
    stress \
    memtester \
    iperf \
    util-linux \
    util-linux-blkid \
    pulseaudio \
    lsof \
    spitools \
    usbutils \
    jack-utils \
    portaudio-v19 \
    libatomic \
    avahi-autoipd \
    boost \
    e2fsprogs \
    u-boot-fw-utils \
    systemd-analyze \
    fuse \
    jpeg-tools \
    libturbojpeg \
    exfat-utils \
    fuse-exfat \
    gptfdisk \
    pigz \
    tar \
    iproute2 \
    sdparm \
    libunwind \
    directfb \
    less \
"

IMAGE_FSTYPES = "${INITRAMFS_FSTYPES}"

ROOTFS_POSTPROCESS_COMMAND += " my_postprocess_function ; "

my_postprocess_function(){
    rm ${IMAGE_ROOTFS}/etc/hostname
    echo -n MP_CDJ3K > ${IMAGE_ROOTFS}/etc/hostname
    cat  ${TOPDIR}/../meta-cdj3k/recipes-graphics/images/core-image-x11/etc/sysctl.conf_append >> ${IMAGE_ROOTFS}/etc/sysctl.conf
    cp -f ${TOPDIR}/../meta-cdj3k/recipes-graphics/images/core-image-x11/etc/99avahi-autoipd ${IMAGE_ROOTFS}/etc/udhcpc.d
    cp -f ${TOPDIR}/../meta-cdj3k/recipes-graphics/images/core-image-x11/bin/fsck.hfsplus ${IMAGE_ROOTFS}/sbin
    cp -f ${TOPDIR}/../meta-cdj3k/recipes-graphics/images/core-image-x11/bin/libmpg123.so.0.44.10 ${IMAGE_ROOTFS}/usr/lib
    ln -sf libmpg123.so.0.44.10 ${IMAGE_ROOTFS}/usr/lib/libmpg123.so.0
    ln -sf libmpg123.so.0.44.10 ${IMAGE_ROOTFS}/usr/lib/libmpg123.so
    rm -rf ${IMAGE_ROOTFS}/lib/modules/4.9.0-yocto-standard/kernel/net/ipv6
    rm -f ${IMAGE_ROOTFS}/boot/*
    rm -f ${IMAGE_ROOTFS}/usr/share/fonts/ttf/Liberation*
    rm -f ${IMAGE_ROOTFS}/usr/share/sounds/alsa/*
    rm -f ${IMAGE_ROOTFS}/usr/lib/libicu*
    rm -f ${IMAGE_ROOTFS}/usr/lib/libcairo*
    rm -f ${IMAGE_ROOTFS}/usr/bin/dfb*
    rm -f ${IMAGE_ROOTFS}/usr/bin/directfb-csource
    rm -f ${IMAGE_ROOTFS}/usr/bin/mkdfiff
    rm -f ${IMAGE_ROOTFS}/usr/bin/mkdgiff
    rm -f ${IMAGE_ROOTFS}/usr/bin/mkdgifft
    rm -rf ${IMAGE_ROOTFS}/usr/share/directfb-1.7-7
    rm -rf ${IMAGE_ROOTFS}/usr/lib/directfb-1.7-7
    rm -f ${IMAGE_ROOTFS}/usr/lib/lib++dfb-1.7.so*
    rm -f ${IMAGE_ROOTFS}/usr/lib/libdirect-1.7.so*
    rm -f ${IMAGE_ROOTFS}/usr/lib/libdirectfb*
    rm -f ${IMAGE_ROOTFS}/usr/lib/libfusion-1.7.so*
    rm -f ${IMAGE_ROOTFS}/etc/init.d/sshd
    rm -f ${IMAGE_ROOTFS}/etc/systemd/system/sockets.target.wants/sshd.socket
    rm -f ${IMAGE_ROOTFS}/usr/lib/libgtk-x11*
    rm -f ${IMAGE_ROOTFS}/usr/bin/gtk-*
}
