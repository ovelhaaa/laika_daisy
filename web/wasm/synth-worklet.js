import Module from './synth.js';

class SynthProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmApp = null;
        this.outLPtr = null;
        this.outRPtr = null;
        this.bufferSize = 128;

        this.port.onmessage = this.handleMessage.bind(this);

        console.log("[Worklet] SynthProcessor inicializado. Options:", options);

        const wasmModule = options?.processorOptions?.wasmModule;
        if (!wasmModule) {
            throw new Error("SynthProcessor: wasmModule is required in processorOptions");
        }

        console.log("[Worklet] Iniciando carregamento do WASM...");
        Module({
            instantiateWasm: function(info, receiveInstance) {
                try {
                    const instance = new WebAssembly.Instance(wasmModule, info);
                    receiveInstance(instance, wasmModule);
                    return instance.exports;
                } catch (err) {
                    console.error("[Worklet] WebAssembly instantiation failed", err);
                    throw err;
                }
            }
        })
        .then((wasmApp) => {
            console.log("[Worklet] WASM carregado com sucesso.");
            this.wasmApp = wasmApp;

            console.log("[Worklet] _wasm_synth_init(sampleRate) com SR =", sampleRate);
            this.wasmApp._wasm_synth_init(sampleRate);

            this.outLPtr = this.wasmApp._malloc(this.bufferSize * 4);
            this.outRPtr = this.wasmApp._malloc(this.bufferSize * 4);

            this.port.postMessage({ type: 'ready' });
        })
        .catch((err) => {
            console.error("[Worklet] Falha ao carregar WASM:", err);
            this.port.postMessage({ type: 'error', message: err.message || err.toString() });
        });

        this.processCount = 0;
        this.logCount = 0;
    }

    handleMessage(event) {
        if (event.data.type === 'midi') {
            console.log(`[Worklet] handleMessage recebeu MIDI - Status: ${event.data.status}, Data1: ${event.data.data1}, Data2: ${event.data.data2}`);
            if (!this.wasmApp) {
                console.warn("[Worklet] wasmApp não inicializado ao receber MIDI!");
                return;
            }
            if (typeof this.wasmApp._wasm_synth_send_midi === 'function') {
                this.wasmApp._wasm_synth_send_midi(
                    event.data.status,
                    event.data.data1,
                    event.data.data2
                );

                // Reiniciar o contador de logs após uma tecla ser pressionada para capturarmos os primeiros samples
                if (event.data.status === 0x90) { // NoteOn
                    this.logCount = 0;
                }
            }
        }
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        const channelL = output[0];
        const channelR = output[1];

        // -- INÍCIO DO BYPASS DE DSP --
        // Teste de sinal DC puro 0.05 executado ANTES do guard do WASM.
        // Assim testamos a cadeia gráfica mesmo se o WASM falhar ao carregar.
        channelL.fill(0.05);
        if (channelR) {
            channelR.fill(0.05);
        }
        // -- FIM DO BYPASS --

        if (!this.wasmApp || !this.outLPtr) return true;

        this.processCount++;
        if (this.processCount % 500 === 0) {
            console.log(`[Worklet] process() executando... Iteração: ${this.processCount}`);
        }

        const frames = Math.min(channelL.length, this.bufferSize);

        // As linhas de chamada real para process() no DSP
        // encontram-se omitidas temporariamente para testar a cadeia:
        /*
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

        // Log dos primeiros buffers após o MIDI
        if (this.logCount < 3) {
            const peakL = Math.max(...wasmHeapL.map(v => Math.abs(v)));
            const peakR = Math.max(...wasmHeapR.map(v => Math.abs(v)));
            console.log(`[Worklet] Inspect WASM Output (Bloco pós-MIDI ${this.logCount}) -> Peak L: ${peakL.toFixed(6)}, Peak R: ${peakR.toFixed(6)}`);
            this.logCount++;
        }

        channelL.set(wasmHeapL);

        if (channelR) {
            channelR.set(wasmHeapR);
        }
        */
        // -- FIM DO BYPASS --

        return true;
    }
}

registerProcessor('synth-processor', SynthProcessor);
