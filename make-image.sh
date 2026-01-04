#!/bin/sh

#
# This file is part of the Magic Phono project (https://magicphono.org/).
# Copyright (c) 2025 xorbxbx <xorbxbx@magicphono.org>
#

create_image()
{
    DEV=$1
    FILE=$2

    echo "➡️  create_image: ${DEV} on ${FILE}"

    echo "  🏃‍♂️‍➡️ Allocating space..."
    dd if=/dev/zero of=${FILE} bs=710M count=1

    echo "  🏃‍♂️‍➡️ Creating partition..."
    parted ${DEV} --script -- mklabel gpt
    parted ${DEV} --script -- mkpart primary ext4 0% 700M

    echo "  🏃‍♂️‍➡️ Formatting partition..."
    mkfs.ext4 -v ${DEV}p1 -O ^extent,^64bit

    parted ${DEV} print

    echo "✅ create_image: done"
}

copy()
{
    DEV=$1
    INPUT=$2

    echo "➡️  copy: ${INPUT} to ${DEV}"

    mkdir -p /mnt/loop
    echo "  🏃‍♂️‍➡️ Mounting loop device..."
    mount ${DEV}p1 /mnt/loop
    mkdir /mnt/loop/boot  
    echo "  🏃‍♂️‍➡️ Copying boot images..."
    cp ${INPUT}/Image ${INPUT}/Image-r8a7796-salvator-x.dtb  /mnt/loop/boot/
    echo "  🏃‍♂️‍➡️ Copying rootfs..."
    tar xjf ${INPUT}/core-image-x11-salvator-x.tar.bz2 -C /mnt/loop/
    #cp Doom1.WAD /mnt/sdcard/home/root/

    echo "  🏃‍♂️‍➡️ Syncing..."
    sync  
    umount /mnt/loop/

    echo "✅ copy: done"
}

write_card()
{
    DEV=$1
    FILE=$2

    echo "➡️  write_card: from ${FILE} to ${DEV}"

    echo "  🏃‍♂️‍➡️ Writing disk image..."
    dd if=${FILE} of=${DEV} bs=710M status=progress count=1

    echo "✅ write_card: done"
}

run()
{
    LOOP=`losetup -f`
    IMAGE=$1  # magicphono1.0.img
    INPUT=$2  # build/tmp/deploy/images/salvator-x/
    DEVICE=$3 # /dev/sdb

    losetup $LOOP $IMAGE

    create_image $LOOP $IMAGE
    copy $LOOP $INPUT

    losetup -d $LOOP
    ls -lha $IMAGE

    if [ -n "$DEVICE" ]; then
        write_card $DEVICE $IMAGE
    fi
}

if [ $(id -u) -ne 0 ]; then
   echo "‼️ This script must be run as root" 
   exit 1
fi

if [ "$#" -lt 2 ]; then
    echo "make-image.sh <image file> <build folder> [<sd card device>]"
    exit 1
fi

run $1 $2 $3
