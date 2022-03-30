#!/usr/bin/env bash

[ ! -d "build" ] mkdir "build"

pushd build
cmake -DCMAKE_BUILD_TYPE=Debug  -DHUNTER_ENABLED=ON -DUSE_BUNDLED_COEURL=ON ../ && make
popd