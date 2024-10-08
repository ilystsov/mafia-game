#! /usr/bin/bash

if [ ! -d "thirdparty" ]; then
  mkdir thirdparty
fi

cd thirdparty

git clone --depth 1 --recursive https://github.com/andreasbuhr/cppcoro.git
