mkdir /mnt/usb
mount /dev/sda1 /mnt/usb/

export DISPLAY=:0
export SDL_AUDIODRIVER=alsa

chocolate-doom -iwad Doom1.WAD 
