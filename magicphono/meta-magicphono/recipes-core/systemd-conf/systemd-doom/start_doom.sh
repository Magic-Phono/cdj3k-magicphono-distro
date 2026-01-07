#!/bin/bash

export DISPLAY=:0
export SDL_AUDIODRIVER=alsa

cp /media/usb/sda1/Doom1.WAD .

subucom_uinput&
sleep 1
startx&
sleep 2

chocolate-doom -iwad /tmp/Doom1.WAD 
