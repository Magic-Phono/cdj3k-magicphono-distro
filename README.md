# MagicPhono Linux

Distribution for CDJ-3000 based on OpenEmbedded/Yocto.

Includes GPL/LGPL code from https://www.pioneerdj.com/en-gb/support/open-source-code-distribution/gnu-open-source-license/

> [!WARNING]
> MagicPhono Linux is under early development and cannot guarantee correct operation.
>
> We are not responsible to any damage to your player. Install at your own risk!

## Device Driver Support

| Device  | Status | Kernel Driver | Notes |
| ------------- | ------------- | ------------- | ------------- |
| Serial  | ✅  | `sh-sci` | Onboard serial via `/dev/ttySC0` |
| Display  | ✅ | `rcar-du` | |
| Ethernet  | ✅ | `ravb` | |
| eMMC  | ✅ | `renesas_sdhi` | `/dev/mmcblk0` |
| SD card  | ✅ | `renesas_sdhi` | `/dev/mmcblk1` |
| USB  | ✅ | `phy-rcar-gen3-usb[2,3]` | Need to add hotplug support |
| Audio DIT  | ✅ | `ak4104` | |
| Audio DAC  | ❌ | `ak4490` | Need to figure out un-mute -- `PNL_xMUTE` is being held low by `subucom` |
| Controls  | 🛠️ | `subucom_spi` | Partial support via [`cdj3k-subucom-tools`](https://github.com/Magic-Phono/cdj3k-subucom-tools) to `/dev/uinput/event0` |
| Touchscreen  | ❌ | `subucom_spi` | Need to write driver to convert `subucom` data to touch events |
| LEDs  | 🛠️ | `subucom_spi` | Partial support via [`cdj3k-subucom-tools`](https://github.com/Magic-Phono/cdj3k-subucom-tools) |
| Jog LCD  | ❌ | | |


## Installing

### Compatibility
- Compatible with Renesas SoC based CDJ-3000s.

> [!NOTE]
> There are reports that a Rockchip variant of the CDJ-3000 exists. Trying to install Magic Phono on this variant has not been tested. If you have one of these variants, please reach out to us!

### Prerequisites
- Requires <a href="https://github.com/Magic-Phono/cdj3k-magicphono-loader">MagicPhono Loader</a> to be installed on a compatible CDJ-3000.
- An SD-card with of at least 1 GiB in size.

### Option 1 (easy): Copy a Pre-built Image to an SD-card

- Dowload a SD-card image from the Releases page.
- Write it to a SD-card using a tool like <a href="https://www.raspberrypi.com/software/" target="_blank">Raspberry Pi Imager</a> (despite the name it can write any image file to an SD card).

### Option 2 (developer): Build From Source and Copy to an SD-card

- Follow Build Requirements/Instructions below.

## Running

- On a CDJ-3000 with <a href="https://github.com/Magic-Phono/cdj3k-magicphono-loader">MagicPhono Loader</a> installed, put the SD-card in the SD slot and turn the CDJ-3000 on.
- MagicPhono Linux should boot from the SD-card.
- If booting from the SD-card fails, the CDJ-3000 will boot from its internal firmware as usual.

> [!WARNING]  
> Root access via SSH is enabled by default with no password. This will be disabled in future pre-built
> images as MagicPhono Linux stabilizes.

## Build Requirements

This distribution is based on Yocto 2.1.3 (Krogoth) so requires an older build host.

Sanity tested build host:
- Ubuntu 16.04 (64-bit) VM running on x86-64.

## Build Instructions

Install required packages:

```
 sudo apt-get install gawk wget git-core diffstat unzip texinfo gcc-multilib \
     build-essential chrpath socat libsdl1.2-dev xterm python-crypto cpio python python3 \
     python3-pip python3-pexpect xz-utils debianutils iputils-ping libssl-dev
```

Clone repo:

```
git clone https://github.com/Magic-Phono/cdj3k-magicphono-distro.git
cd cdj3k-magicphono-distro/
git submodule update --init --recursive
```

Set up environment:

```
cd cdj3k-magicphono-distro/
export WORK=`pwd`
./setup-local-build.sh
source magicphono/oe-init-build-env
```

Build cross-compiler and SDK:

```
cd $WORK/build
bitbake core-image-x11 -c populate_sdk
```

Install SDK:

```
sudo $WORK/build/tmp/deploy/sdk/magicphono-glibc-x86_64-core-image-x11-aarch64-toolchain-1.0.0.sh
```

Build images:

```
cd $WORK/build
bitbake core-image-x11 -c cleansstate
bitbake linux-renesas -c cleansstate
bitbake core-image-x11
```

Artifacts are in `$WORK/build/tmp/deploy/images/salvator-x/`.

Build SD card image (run as root):

```
./make-image.sh <image file> <build folder> [<sd card device>]
# example: ./make-image.sh magicphono1.0.img build/tmp/deploy/images/salvator-x/
```
