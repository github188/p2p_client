#! /bin/sh
cd miniupnpc-1.9.20150206
make clean
make
cd ..
echo "make miniupnpc complete"

cp udt4.11/src/*.cpp pjproject-2.3/pjnath/src/pjnath
echo "cp all udt cpp to pjnath"

cd pjproject-2.3
chmod +x configure
chmod +x aconfigure
make clean
echo "make clean"
DIR="$( cd "$( dirname "$0"  )" && pwd  )"
echo $DIR
export LITEOS_BUILD=FALSE
./configure --prefix=$DIR --enable-epoll --disable-ssl

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
cp -f pjlib/lib/libpj-x86_64-unknown-linux-gnu.a lib
cp -f pjlib-util/lib/libpjlib-util-x86_64-unknown-linux-gnu.a lib
cp -f pjnath/lib/libpjnath-x86_64-unknown-linux-gnu.a lib

echo "gss make ok"
