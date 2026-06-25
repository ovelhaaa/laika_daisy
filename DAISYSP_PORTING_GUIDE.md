# Guia de Portabilidade do SynthEngine: STM32 para WebAssembly

Este documento descreve as etapas e as considerações para portar e compilar a engine DSP `SynthEngine` de um microcontrolador STM32 (como o Daisy Seed) para a web rodando localmente via WebAssembly, e como publicá-lo no GitHub Pages.

## Arquitetura

O core do sintetizador (`SynthEngine`) foi refatorado para não depender de APIs específicas de hardware ou de interfaces globais exclusivas do STM32. Ele expõe uma classe `SynthEngine` com os seguintes métodos primários:
*   `Init(float sample_rate)`
*   `Process(float* out_l, float* out_r, size_t size)`
*   `ProcessMidiMessage(uint8_t status, uint8_t data1, uint8_t data2)`

A mesma classe é usada tanto pelo hardware (`main.cpp`) quanto pelos bindings WebAssembly (`wasm_bindings.cpp`).

## Compilação WebAssembly

O ambiente WebAssembly usa a toolchain do Emscripten (emsdk). Para garantir que compila perfeitamente para web:

1. **Memória Wasm e Abstração D2_RAM_ATTR:**
   O STM32 possui diferentes seções de RAM. O código de hardware utiliza a macro especial para alocar buffers de áudio (`delay_buf_left`, `delay_buf_right`) na seção D2 de RAM para não estourar a RAM de alta velocidade.
   Ao compilar para Wasm (onde essa arquitetura não existe e o heap é virtualizado), essa macro é definida como nada (`#define D2_RAM_ATTR`), ignorando a diretiva.

2. **Como rodar o build Wasm localmente:**
   No diretório raiz do projeto:
   ```bash
   ./setup_env.sh
   ./build_wasm.sh
   ```
   *Isso criará os artefatos `synth.js` e `synth.wasm` em `web/wasm/`.*

## Estrutura de UI e Testes Locais

A versão hardware possui o arquivo original `synth-controller.html` para a interface de Web MIDI.
A versão Wasm expõe um controlador similar, nomeado `synth_web.html`.

Para testar a interface do Web Synthesizer (rodando via AudioWorklet), deve-se usar um servidor web local devido a políticas de CORS em chamadas de módulos javascript (ex: import e web workers/worklets):
```bash
cd web
python3 -m http.server 8000
```
Depois acesse `http://localhost:8000/synth_web.html`.

## Como publicá-lo no GitHub Pages

O GitHub Pages permite a hospedagem gratuita de arquivos estáticos. Com o projeto atual, você precisará apontar o seu repositório para publicar o conteúdo estático da pasta `/web` (ou subir os artefatos compilados em um branch dedicado).

A maneira mais prática é:
1. Acesse as "Configurações (Settings)" do repositório no GitHub.
2. Na aba "Pages", escolha a "Source" como branch base (ex: `main` ou `gh-pages`).
3. (Se estiver usando a branch principal, você deve apontar a pasta para `/web` ou criar um index redirecionador). Como a estrutura atual tem a UI na pasta `/web`, certifique-se de configurar a raíz do Pages se ele suportar, ou simplesmente acesse via: `https://[seu-usuario].github.io/[repo]/web/synth_web.html`

### Recomendações do `.gitignore`
Para evitar inflar o repositório, o diretório `emsdk/` deve estar listado no seu `.gitignore`. No entanto, você **deve versionar os binários finais** localizados em `web/wasm/synth.js` e `web/wasm/synth.wasm` se deseja que o GitHub pages os sirva sem a necessidade de uma Action com docker do Emscripten para compilá-los a cada push.

Certifique-se de adicionar ao final do seu `.gitignore`:
```
emsdk/
```
E garantir que os artefatos web estão committados (não ignorados):
```
!web/wasm/synth.wasm
!web/wasm/synth.js
```
