#!/bin/bash

INSTAL_DIR=$PWD/_install

aclocal
autoconf
autoheader
libtoolize --automake
automake --add-missing

if[ -e $INSTAL_DIR ]
	mkdir -p $INSTAL_DIR

./configure --prefix=$INSTAL_DIR

make
make install

