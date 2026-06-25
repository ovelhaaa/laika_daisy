import Module from './synth.js';

class SynthProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.wasmApp = null;
        this.outLPtr = null;
        this.outRPtr = null;
        this.bufferSize = 128; // Tamanho padrão do bloco do Web Audio API

        // Recebe mensagens da thread principal (ex: eventos MIDI)
        this.port.onmessage = this.handleMessage.bind(this);

        // Inicializa o módulo WebAssembly
        Module().then((wasmModule) => {
            this.wasmApp = wasmModule;

            // Inicializa a engine C++ com a taxa de amostragem do AudioContext
            this.wasmApp._wasm_synth_init(sampleRate);

            // Aloca memória no heap do Wasm (128 floats * 4 bytes)
            this.outLPtr = this.wasmApp._malloc(this.bufferSize * 4);
            this.outRPtr = this.wasmApp._malloc(this.bufferSize * 4);

            // Avisa a thread principal que o synth está pronto
            this.port.postMessage({ type: 'ready' });
        });
    }

    handleMessage(event) {
        if (!this.wasmApp) return;

        if (event.data.type === 'midi') {
            this.wasmApp._wasm_synth_send_midi(
                event.data.status,
                event.data.data1,
                event.data.data2
            );
        }
    }

    process(inputs, outputs, parameters) {
        // Aguarda silenciosamente até que o Wasm carregue e aloque a memória
        if (!this.wasmApp || !this.outLPtr) return true;

        const output = outputs[0];
        const channelL = output[0];
        const channelR = output[1];

        // Garante que o bloco requisitado não seja maior que a memória que alocamos
        const frames = Math.min(channelL.length, this.bufferSize);

        // Roda o processamento DSP em C++
        this.wasmApp._wasm_synth_process(this.outLPtr, this.outRPtr, frames);

        // Cria "views" (referências rápidas) da memória Heap do Wasm
        const wasmHeapL = new Float32Array(this.wasmApp.HEAPF32.buffer, this.outLPtr, frames);
        const wasmHeapR = new Float32Array(this.wasmApp.HEAPF32.buffer, this.outRPtr, frames);

        // Copia os dados do Wasm direto para a saída de áudio do navegador
        channelL.set(wasmHeapL);
        if (channelR) {
            channelR.set(wasmHeapR);
        }

        // Retorna true para manter o processor vivo
        return true;
    }
}

// Registra o processor no contexto de áudio
registerProcessor('synth-processor', SynthProcessor);
