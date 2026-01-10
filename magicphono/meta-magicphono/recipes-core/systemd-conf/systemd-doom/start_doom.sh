#!/bin/bash

export DISPLAY=:0
export SDL_AUDIODRIVER=alsa

# Hack to wait for USB
echo "Waiting for USB..."

USB=

until [ -n "$USB" ]
do
    USB=$(cat /proc/mounts | grep usb)
    sleep 1
done

mkdir -p /tmp
cp /media/usb/sda1/Doom1.WAD /tmp/

chocolate-doom -iwad /tmp/Doom1.WAD 
