#! /bin/sh
cd miniupnpc-1.9.20150206
make clean
cd ..
cd pjproject-2.3
make clean

cd pjlib/build/
make clean
cd ../../

cd pjlib-util/build
make clean
cd ../../

cd pjnath/build
make clean
cd ../../

rm -f lib/*.a
echo "gss make clean ok"
