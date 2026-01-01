# MagicPhono Linux

Distribution for CDJ-3000 based on OpenEmbedded/Yocto.

Includes GPL/LGPL code from https://www.pioneerdj.com/en-gb/support/open-source-code-distribution/gnu-open-source-license/

This is very much work in progress.

## Requirements

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