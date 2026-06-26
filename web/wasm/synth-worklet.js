import Module from './synth.js';

class SynthProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmApp = null;
        this.outLPtr = null;
        this.outRPtr = null;
        this.bufferSize = 128;

        this.port.onmessage = this.handleMessage.bind(this);

        Module({
            wasmBinary: options.processorOptions.wasmBinary
        })
        .then((wasmModule) => {
            console.log("WASM carregado");

            this.wasmApp = wasmModule;

            this.wasmApp._wasm_synth_init(sampleRate);

            this.outLPtr = this.wasmApp._malloc(this.bufferSize * 4);
            this.outRPtr = this.wasmApp._malloc(this.bufferSize * 4);

            this.port.postMessage({ type: 'ready' });
        })
        .catch((err) => {
            console.error("Falha ao carregar WASM:", err);
        });
    } // <-- FALTAVA FECHAR O CONSTRUCTOR AQUI

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
        if (!this.wasmApp || !this.outLPtr) return true;

        const output = outputs[0];
        const channelL = output[0];
        const channelR = output[1];

        const frames = Math.min(channelL.length, this.bufferSize);

        this.wasmApp._wasm_synth_process(
            this.outLPtr,
            this.outRPtr,
            frames
        );

        const wasmHeapL = new Float32Array(
            this.wasmApp.HEAPF32.buffer,
            this.outLPtr,
            frames
        );

        const wasmHeapR = new Float32Array(
            this.wasmApp.HEAPF32.buffer,
            this.outRPtr,
            frames
        );

        channelL.set(wasmHeapL);

        if (channelR) {
            channelR.set(wasmHeapR);
        }

        return true;
    }
}

registerProcessor('synth-processor', SynthProcessor);
