#!/bin/bash

set -e

docker build -f Dockerfile.linux -t number_sequence_game:linux .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence_game:linux /bin/bash -c 'cp /number_sequence_game/build/release/* /tmp'

docker build -f Dockerfile.windows -t number_sequence_game:windows .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence_game:windows /bin/bash -c 'cp /number_sequence_game/build/release/* /tmp'

docker build -f Dockerfile.wasm -t number_sequence_game:wasm .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence_game:wasm /bin/bash -c 'cp /number_sequence_game/build/release/* /tmp'

docker build -f Dockerfile.android -t number_sequence_game:android .
docker run --rm -it -v /tmp:/tmp --network=host number_sequence_game:android /bin/bash -c 'cp /SDL/build/org.libsdl.number_sequence_game/app/build/outputs/apk/release/*.apk /tmp'

