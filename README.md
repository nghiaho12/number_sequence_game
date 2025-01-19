Simple number sequence game for kids written using SDL3 and OpenGL ES.

![screenshot](screenshot.png)

# Binary release
See the Github releases page for Linux, Windows, Android and web binaries.

# Building from source
## Prerequisite
This repo uses git LFS for the assets. Install it before cloning, e.g. ```sudo apt install git-lfs```.
You'll also need to have Docker installed, e.g. ```sudo apt install docker-ce```.

## Linux
The default Linux target is Debian 12.8. Edit Dockerfile.linux to match your distro if you run into problems with the binary.

```
docker build -f Dockerfile.linux -t number_sequence:linux .
docker run --rm -it --network=host number_sequence:linux
```
Go to http://localhost:8000 to download the release package.

The tarball comes with SDL3 shared library bundled. Run the binary by calling
```
LD_LIBRARY_PATH=. ./number_sequence
```

## Windows
```
docker build -f Dockerfile.windows -t number_sequence:windows .
docker run --rm -it --network=host number_sequence:windows
```

Point your browser to http://localhost:8000 to download the release package.

## Android
The APK is targeted at Android 9 (API Level 28) and above.

```
docker build -f Dockerfile.android -t number_sequence:android .
docker run --rm -it --network=host number_sequence:android
```

Point your Android web browser to http://localhost:8000 to download the APK.

## Web
```
docker build -f Dockerfile.wasm -t number_sequence:wasm .
docker run --rm -it --network=host number_sequence:wasm
```

Point your browser to http://localhost:8000 to download the release package.
If you have Python installed you can run the app by calling

```
python3 -m http.server
```

inside the extracted folder and point your browser to http://localhost:8000.

# Credits
Sound assets 
- https://opengameart.org/content/win-sound-effect
- https://kenney.nl/assets/ui-audio
- https://soundimage.org/funny-2

Ogg Vorbis decoder
- https://github.com/nothings/stb

Signed Distance Field (SDF) font
- https://github.com/Chlumsky/msdfgen

# Contact
nghiaho12@yahoo.com
