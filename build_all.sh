#!/bin/bash

set -e

docker build -f Dockerfile.linux -t number_sequence:linux .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence:linux /bin/bash -c 'cp /number_sequence/build/release/* /tmp'

docker build -f Dockerfile.windows -t number_sequence:windows .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence:windows /bin/bash -c 'cp /number_sequence/build/release/* /tmp'

docker build -f Dockerfile.wasm -t number_sequence:wasm .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence:wasm /bin/bash -c 'cp /number_sequence/build/release/* /tmp'

docker build -f Dockerfile.android -t number_sequence:android .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence:android /bin/bash -c 'cp /SDL/build/org.libsdl.number_sequence/app/build/outputs/apk/debug/*.apk /tmp'

