#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdint.h>
#include "SynthEngine.h"

// Instância global do motor DSP para o Wasm
SynthEngine wasm_engine;

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void wasm_synth_init(float sample_rate) {
        wasm_engine.Init(sample_rate);
    }

    EMSCRIPTEN_KEEPALIVE
    void wasm_synth_process(uintptr_t out_l_ptr, uintptr_t out_r_ptr, int size) {
        float* out_l = reinterpret_cast<float*>(out_l_ptr);
        float* out_r = reinterpret_cast<float*>(out_r_ptr);
        wasm_engine.Process(out_l, out_r, size);
    }

    EMSCRIPTEN_KEEPALIVE
    void wasm_synth_send_midi(uint8_t status, uint8_t data1, uint8_t data2) {
        wasm_engine.ProcessMidiMessage(status, data1, data2);
    }
}
#endif
