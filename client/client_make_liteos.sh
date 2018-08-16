#! /bin/sh
cd miniupnpc-1.9.20150206
make clean
make -f MakefileLiteos
cd ..

cp udt4.11/src/*.cpp pjproject-2.3/pjnath/src/pjnath

cd pjproject-2.3
chmod +x configure
chmod +x aconfigure
make clean
DIR="$( cd "$( dirname "$0"  )" && pwd  )"
export LITEOS_BUILD=TRUE
./configure --prefix=$DIR --host=arm-hisiv500-linux CXXFLAGS=-DPJ_ARM_MIPS CFLAGS=-DLITEOS

cd pjlib/build/
make 
cd ../../

cd pjlib-util/build
make
cd ../../

cd pjnath/build
make
cd ../../

rm -f lib/*.a
cp -f pjlib/lib/libpj-arm-hisiv500-linux-gnu.a lib
cp -f pjlib-util/lib/libpjlib-util-arm-hisiv500-linux-gnu.a lib
cp -f pjnath/lib/libpjnath-arm-hisiv500-linux-gnu.a lib

echo "gss make ok"
