#!/bin/sh

Version="v1.5.312"
OutPath="out"
if [ ! -x "$OutPath" ]; then  
  mkdir "$OutPath" 
fi
rm -rf $OutPath/*
rm sdk/p2pdemo/*.a
cp -rf sdk/include $OutPath

function linux(){
	sh client_make.sh
	cd sdk/p2pdemo
	sh gcclib.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p.a $OutPath
	cd $OutPath
	tar zcvf p2p-$Version.tar.gz include/ p2p.a
	cd ..
}

function hisiv300(){
	sh client_make_arm.sh
	cd sdk/p2pdemo
	sh gcclib_arm.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p-hisiv300.a $OutPath
	cd $OutPath
	tar zcvf p2p-hisiv300-$Version.tar.gz include/ p2p-hisiv300.a
	cd ..
}

function liteos(){
	sh client_make_liteos.sh
	cd sdk/p2pdemo
	sh gcclib_liteos.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p-hisiv500.a $OutPath
	cd $OutPath
	tar zcvf p2p-hisiv500-$Version.tar.gz include/ p2p-hisiv500.a
	cd ..
}

function ingenic(){
	sh client_make_mips.sh
	cd sdk/p2pdemo
	sh gcclib_mips.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p-ingenic-linux-mips.a $OutPath
	cd $OutPath
	tar zcvf p2p-ingenic-$Version.tar.gz include/ p2p-ingenic-linux-mips.a
	cd ..
}

function openwrt(){
	sh client_make_openwrt.sh
	cd sdk/p2pdemo
	sh gcclib_openwrt.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p-openwrt-linux-mips.a $OutPath
	cd $OutPath
	tar zcvf p2p-openwrt-$Version.tar.gz include/ p2p-openwrt-linux-mips.a
	cd ..
}

function goke(){
	sh client_make_goke.sh
	cd sdk/p2pdemo
	sh gcclib_goke.sh
	cd ../..
	cp -rf sdk/p2pdemo/p2p-goke.a $OutPath
	cd $OutPath
	tar zcvf p2p-goke-$Version.tar.gz include/ p2p-goke.a
	cd ..
}

function android(){
	cd android/jni
	ndk-build
	cd ../..
	cp -rf android/libs $OutPath
	rm -rf android/obj/local/armeabi/objs/ android/obj/local/armeabi-v7a/objs/ android/obj/local/x86/objs/
	cp -rf android/obj/local/armeabi android/obj/local/armeabi-v7a android/obj/local/x86 $OutPath
	cd $OutPath
	tar zcvf p2p-android-$Version.tar.gz include/ libs/
	tar zcvf p2p-android-debug-$Version.tar.gz include/ armeabi/ armeabi-v7a/ x86/
	cd ..
}

module_type=$1
usage="Usage: `basename $0` (android|openwrt|ingenic|liteos|hisiv300|goke|linux|all) to build module"

case $module_type in
  (android)
     android
     ;;
  (openwrt)
     openwrt
     ;;
  (ingenic)
     ingenic
     ;;
  (liteos)
     liteos
     ;;
  (hisiv300)
     hisiv300
     ;;
  (linux)
     linux
     ;;
  (goke)
     goke
     ;;
  (all)
     android
     openwrt
     ingenic
     liteos
	 hisiv300
	 goke
	 linux
     ;;
  (*)
     echo "Error command"
	 echo "$usage"
     ;;
esac
