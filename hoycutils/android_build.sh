#!/bin/bash

NDK_DIR=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e
SYSROOT=$NDK_DIR/platforms/android-21/arch-arm/
TOOLCHAIN=$NDK_DIR/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64

INSTAL_DIR=$PWD/_android_install

if [ ! -e $INSTAL_DIR ]; then
	mkdir -p $INSTAL_DIR
fi

export CC="$TOOLCHAIN/bin/arm-linux-androideabi-gcc --sysroot=$SYSROOT"

./configure  --enable-debug --enable-verbose --prefix=$INSTAL_DIR --host=arm-linux-androideabi --with-platform=android

make

/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc --sysroot=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/platforms/android-21/arch-arm/ -shared  -fPIC -DPIC  client/.libs/client.o   common/libcommon.a -llog  --sysroot=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/platforms/android-21/arch-arm/ -O2   -Wl,-soname -Wl,libclient.so -o client/.libs/libclient.so.0.0.0

make install

