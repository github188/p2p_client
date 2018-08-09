#!/bin/sh

Version="v1.5.120"
OutPath="out"
if [ ! -x "$OutPath" ]; then  
  mkdir "$OutPath" 
fi
rm -rf $OutPath/*
rm sdk/p2pdemo/*.a

sh client_make.sh
cd sdk/p2pdemo
sh gcclib.sh

cd ../..
sh client_make_arm.sh
cd sdk/p2pdemo
sh gcclib_arm.sh

cd ../..
sh client_make_liteos.sh
cd sdk/p2pdemo
sh gcclib_liteos.sh

cd ../..
sh client_make_mips.sh
cd sdk/p2pdemo
sh gcclib_mips.sh

cd ../..
sh client_make_openwrt.sh
cd sdk/p2pdemo
sh gcclib_openwrt.sh

cd ../..
cd android/jni
ndk-build

cd ../..
cp -rf sdk/include $OutPath
cp -rf sdk/p2pdemo/*.a $OutPath
cp -rf android/libs $OutPath
cp -rf ../common $OutPath

cd $OutPath
tar zcvf p2p-hisiv300-$Version.tar.gz include/ p2p-hisiv300.a
tar zcvf p2p-hisiv500-$Version.tar.gz include/ p2p-hisiv500.a
tar zcvf p2p-ingenic-$Version.tar.gz include/ p2p-ingenic-linux-mips.a
tar zcvf p2p-openwrt-$Version.tar.gz include/ p2p-openwrt-linux-mips.a
tar zcvf p2p-android-$Version.tar.gz include/ libs/
cd ..

