#!/bin/bash

export DISPLAY=:0
export SDL_AUDIODRIVER=alsa

mkdir -p /tmp
cp /media/usb/sda1/Doom1.WAD /tmp/

chocolate-doom -iwad /tmp/Doom1.WAD 
