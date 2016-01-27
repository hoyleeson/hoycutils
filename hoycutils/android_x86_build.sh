#!/bin/bash

NDK_DIR=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e
#SYSROOT=$NDK_DIR/platforms/android-19/arch-arm/
SYSROOT=$NDK_DIR/platforms/android-19/arch-x86/
TOOLCHAIN=$NDK_DIR/toolchains/x86-4.8/prebuilt/linux-x86_64

export CC="$TOOLCHAIN/bin/i686-linux-android-gcc --sysroot=$SYSROOT"

INSTAL_DIR=$PWD/_android_x86_install

if [ -e $INSTAL_DIR ]; then
	mkdir -p $INSTAL_DIR
fi

./configure  --enable-verbose --prefix=$INSTAL_DIR --host=i686-linux-androideabi --with-platform=android

make

/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/toolchains/x86-4.8/prebuilt/linux-x86_64/bin/i686-linux-android-gcc --sysroot=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/platforms/android-19/arch-x86/ -shared  -fPIC -DPIC  client/.libs/client.o   common/libcommon.a -llog  --sysroot=/home/lixinhai/develop_tools/android_ndk/android-ndk-r10e/platforms/android-19/arch-x86/ -O2   -Wl,-soname -Wl,libclient.so -o client/.libs/libclient.so.0.0.0

make install

cd samples/android_samples/jni/
ndk-build
