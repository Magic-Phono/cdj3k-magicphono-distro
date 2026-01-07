#!/bin/bash

export DISPLAY=:0
export SDL_AUDIODRIVER=alsa

subucom_uinput&
sleep 1
startx&
sleep 2

chocolate-doom -iwad /tmp/Doom1.WAD 
