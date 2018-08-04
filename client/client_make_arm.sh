#! /bin/sh
cd miniupnpc-1.9.20150206
make clean
make -f MakefileArm
cd ..

cp udt4.11/src/*.cpp pjproject-2.3/pjnath/src/pjnath

cd pjproject-2.3
chmod +x configure
chmod +x aconfigure
make clean
DIR="$( cd "$( dirname "$0"  )" && pwd  )"
./configure --prefix=$DIR --host=arm-hisiv300-linux --enable-epoll

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
cp -f pjlib/lib/libpj-arm-hisiv300-linux-gnu.a lib
cp -f pjlib-util/lib/libpjlib-util-arm-hisiv300-linux-gnu.a lib
cp -f pjnath/lib/libpjnath-arm-hisiv300-linux-gnu.a lib

echo "gss make ok"
