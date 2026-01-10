IMAGE_INSTALL_append = " \
    openssh-sftp-server \
    bash \
    systemd-usb-mount \
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
    dejavu-sans-ttf \
    fontconfig \
    twm \ 
    xclock \ 
    xeyes \
    xterm \
"

#    kiosk-refresh 
#    kiosk-wallpaper
#    feh 
#     user-kiosk 

#    systemd-doom




SYSTEMD_DEFAULT_TARGET = "graphical.target"
