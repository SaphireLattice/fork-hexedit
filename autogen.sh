#!/bin/sh

test -f configure.ac || {
  echo "Please, run this script in the top level project directory."
  exit
}

mkdir -p build
pushd ./build
autoconf ../configure.ac > configure
autoheader ../configure.ac
popd

echo "Run \"make\" in the \"./build\" directory to build"