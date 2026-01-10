IMAGE_INSTALL_append = " \
    openssh-sftp-server \
    bash \
    systemd-usb-mount \
    systemd-subucom-uinput \
    systemd-doom \
    daemonize \
    cdj3k-subucom-tools \
    chocolatedoom \
    libgpiod \
    libpng \
    libsdl \
    libsamplerate0 \
    mpg123 \
    packagegroup-fonts-truetype \
    packagegroup-core-x11-xserver \
    packagegroup-core-x11-utils \
    doom-autostart \
    matchbox-wm \
    matchbox-terminal \
    xdotool \
    mini-x-session \
    liberation-fonts \
    fontconfig \
    twm \ 
    xclock \ 
    xeyes \
    xterm \
"

SYSTEMD_DEFAULT_TARGET = "graphical.target"
