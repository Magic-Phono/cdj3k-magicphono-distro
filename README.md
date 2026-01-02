# MagicPhono Linux

Distribution for CDJ-3000 based on OpenEmbedded/Yocto.

Includes GPL/LGPL code from https://www.pioneerdj.com/en-gb/support/open-source-code-distribution/gnu-open-source-license/

> [!WARNING]
> Magic Phono is under early development and cannot guarantee correct operation.
>
> We are not responsible to any damage to your player. Install at your own risk!

## Installing

### Compatibility
- MagicPhono Linux is currently compatible with Renesas SoC based CDJ-3000s.

### Prerequisites
- Magic Phono Loader to be installed on a compatible CDJ-3000.
- An SD-card with of at least 1 GiB in size.

### Option 1 (easy): Copy a Pre-built Image to an SD-card

- Dowload a SD-card image from the Releases page.
- Write it to a SD-card using a tool like Disk Utility or <a href="https://etcher.balena.io/">etcher</a>.

### Option 2 (developer): Build From Source and Copy to an SD-card

- Follow Build Requirements/Instructions below.
- Write it to a SD-card (TODO: add instructions)

## Running

- On a CDJ-3000 with Magic Phono Loader installed, put the SD-card in the SD slot and turn the CDJ-3000 on.
- Magic Phono Linux should boot from the SD-card.
- If booting from the SD-card fails, the CDJ-3000 will boot from its internal firmware as usual.

> [!WARNING]  
> Root access via SSH is enabled by default with password `&mpcdj3k`. This will be disabled in future pre-built
> images as Magic Phono Linux stabilizes.å

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
sudo $WORK/build/tmp/deploy/sdk/magicphono-glibc-x86_64-core-image-x11-aarch64-toolchain-2.1.3.sh
```

Build image:

```
cd $WORK/build
bitbake core-image-x11 -c cleansstate
bitbake linux-renesas -c cleansstate
bitbake core-image-x11
```

Artifacts are in `$WORK/build/tmp/deploy/images/salvator-x/`.