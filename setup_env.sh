#!/bin/bash
if [ ! -d "emsdk" ]; then
    echo "Instalando Emscripten SDK..."
    git clone https://github.com/emscripten-core/emsdk.git
    cd emsdk
    ./emsdk install latest
    ./emsdk activate latest
    cd ..
fi
echo "Emscripten SDK pronto."
