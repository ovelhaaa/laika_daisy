#!/bin/bash

# Carrega o Emscripten de forma flexível (CI/CD ou local)
if command -v emcc &> /dev/null; then
    echo "Emscripten detectado no PATH."
elif [ -d "emsdk" ]; then
    source ./emsdk/emsdk_env.sh
    if ! command -v emcc &> /dev/null; then
        echo "Erro: emsdk carregado, mas emcc não foi encontrado no PATH."
        exit 1
    fi
else
    echo "Erro: emcc não encontrado."
    exit 1
fi

mkdir -p web/wasm

# Fontes (Ignorando arquivos específicos de hardware STM32 como main.cpp e audio_sai.cpp)
SYNTH_SOURCES="src/wasm_bindings.cpp src/SynthEngine.cpp src/Voice.cpp"
DAISYSP_SOURCES=$(find lib/DaisySP/Source -name "*.cpp" | grep -v "tests")
DAISYSP_LGPL_SOURCES=$(find lib/DaisySP/DaisySP-LGPL/Source -name "*.cpp" | grep -v "tests")
INCLUDES="-Isrc -Ilib/DaisySP/Source -Ilib/DaisySP/Source/Utility -Iinclude -Ilib/DaisySP/DaisySP-LGPL/Source"

# Flags do Emscripten
# Fix missing size_t in allpass.h by adding it globally via preprocessor
FLAGS="-O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -include stddef.h"
FLAGS+=" -s EXPORTED_FUNCTIONS=['_wasm_synth_init','_wasm_synth_process','_wasm_synth_send_midi','_malloc','_free']"
FLAGS+=" -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
FLAGS+=" -s ERROR_ON_UNDEFINED_SYMBOLS=0"
FLAGS+=" -s MODULARIZE=1 -s EXPORT_ES6=1 -s ENVIRONMENT=web,worker"
FLAGS+=" --no-entry"

echo "Compilando para Wasm..."
emcc $SYNTH_SOURCES $DAISYSP_SOURCES $DAISYSP_LGPL_SOURCES $INCLUDES $FLAGS -o web/wasm/synth.js

if [ $? -eq 0 ]; then
    echo "Build Wasm concluído com sucesso!"
else
    echo "Erro no build Wasm."
    exit 1
fi
