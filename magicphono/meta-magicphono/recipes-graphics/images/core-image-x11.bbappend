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

    packagegroup-core-x11-xserver \
    packagegroup-core-x11-utils \
    
    doom-autostart \

    matchbox-wm \
    matchbox-terminal \
    unclutter-xfixes \
    xdotool \

    mini-x-session \
    liberation-fonts \
"

#    kiosk-refresh 
#    kiosk-wallpaper
#    feh 
#     user-kiosk 

#    systemd-doom

#    twm 
#    xclock 
#    xeyes 
#    xterm 


SYSTEMD_DEFAULT_TARGET = "graphical.target"
