**Causa raiz provável atual:**
O problema central reside no fato de que o código gerado pelo Emscripten tenta utilizar o objeto global `URL` (e.g. `new URL(...)`), que não está definido ou suportado no contexto restrito do `AudioWorkletGlobalScope` (que carece de objetos como `document`, `window` e frequentemente `URL`). Ademais, instanciar a API de rede (como o `fetch`) também não é suportado dentro do `AudioWorkletGlobalScope`. Portanto, qualquer tentativa de buscar o WASM internamente pelo worklet ou pelo loader do Emscripten falha.

**Evidências encontradas no código:**
1. A invocação do build (no `build_wasm.sh`) utiliza:
   `FLAGS+=" -s MODULARIZE=1 -s EXPORT_ES6=1"`
   Mas não estabelece o ambiente alvo (ENVIRONMENT). Isso faz o Emscripten assumir premissas web em seu construtor de path, injetando `new URL(...)` no código gerado (`synth.js`).
2. O artefato gerado (`web/wasm/synth.js`) contém diversas linhas contendo `new URL(...)`, como:
   `function findWasmBinary(){if(Module["locateFile"]){return locateFile("synth.wasm")}return new URL("synth.wasm",import.meta.url).href}`
3. Em `web/wasm/synth-worklet.js`, a função `locateFile` apenas retornava a string `"wasm/" + path`. Embora ajudasse no Emscripten, o loader ainda batia em `URL`. E caso tentássemos fazer um `fetch()` no próprio worklet, ocorreria o erro `fetch is not defined`.

**Patch mínimo recomendado:**

Devemos abordar o problema carregando o WASM pela thread principal (`main`) e apenas repassar o buffer para o worklet.

1. **`build_wasm.sh`**:
Adicione `-s ENVIRONMENT=web,worker` na compilação para instruir o Emscripten a remover as dependências absolutas do DOM:
```diff
--- a/build_wasm.sh
+++ b/build_wasm.sh
@@ -28,7 +28,7 @@ FLAGS="-O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -include stddef.h"
 FLAGS+=" -s EXPORTED_FUNCTIONS=['_wasm_synth_init','_wasm_synth_process','_wasm_synth_send_midi','_malloc','_free']"
 FLAGS+=" -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
 FLAGS+=" -s ERROR_ON_UNDEFINED_SYMBOLS=0"
-FLAGS+=" -s MODULARIZE=1 -s EXPORT_ES6=1"
+FLAGS+=" -s MODULARIZE=1 -s EXPORT_ES6=1 -s ENVIRONMENT=web,worker"
 FLAGS+=" --no-entry"
```

2. **`web/index.html`**:
Faça o fetch na thread principal (onde `fetch` e `URL` estão garantidos) e passe via `processorOptions`:
```diff
--- a/web/index.html
+++ b/web/index.html
@@ -620,8 +620,15 @@
           try {
               audioContext = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 48000 });

+              // Buscamos o WASM na thread principal
+              const wasmResponse = await fetch('./wasm/synth.wasm');
+              if (!wasmResponse.ok) throw new Error("WASM file not found");
+              const wasmBuffer = await wasmResponse.arrayBuffer();
+
               await audioContext.audioWorklet.addModule('./wasm/synth-worklet.js');
-              synthNode = new AudioWorkletNode(audioContext, 'synth-processor', { outputChannelCount: [2] });
+              synthNode = new AudioWorkletNode(audioContext, 'synth-processor', {
+                  outputChannelCount: [2],
+                  processorOptions: { wasmBinary: wasmBuffer }
+              });
               synthNode.connect(audioContext.destination);
```

3. **`web/wasm/synth-worklet.js`**:
Mude o construtor para receber as opções e carregá-lo por `wasmBinary`:
```diff
--- a/web/wasm/synth-worklet.js
+++ b/web/wasm/synth-worklet.js
@@ -1,7 +1,7 @@
 import Module from './synth.js';

 class SynthProcessor extends AudioWorkletProcessor {
-    constructor() {
+    constructor(options) {
         super();

         this.wasmApp = null;
@@ -11,10 +11,8 @@ class SynthProcessor extends AudioWorkletProcessor {

         this.port.onmessage = this.handleMessage.bind(this);

         Module({
-            locateFile: (path) => {
-                return "wasm/" + path;
-            }
+            wasmBinary: options.processorOptions.wasmBinary
         })
         .then((wasmModule) => {
```

**Outros problemas subsequentes prováveis:**
- O mesmo arquivo compilado de WebAssembly, se exigir memória estrita ou threads, pode esbarrar em políticas de CORS/COOP e COEP no GitHub Pages se você habilitar `SharedArrayBuffer` no futuro.
