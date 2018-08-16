#! /bin/sh
cd miniupnpc-1.9.20150206
make clean
make -f MakefileMips
cd ..

cp udt4.11/src/*.cpp pjproject-2.3/pjnath/src/pjnath

cd pjproject-2.3
chmod +x configure
chmod +x aconfigure
make clean
DIR="$( cd "$( dirname "$0"  )" && pwd  )"
export LITEOS_BUILD=FALSE
./configure --prefix=$DIR --host=mips-linux-gnu CC=mips-linux-uclibc-gnu-gcc --enable-epoll CXXFLAGS=-DPJ_ARM_MIPS

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
cp -f pjlib/lib/libpj-mips-unknown-linux-gnu.a lib
cp -f pjlib-util/lib/libpjlib-util-mips-unknown-linux-gnu.a lib
cp -f pjnath/lib/libpjnath-mips-unknown-linux-gnu.a lib

echo "gss make ok"
