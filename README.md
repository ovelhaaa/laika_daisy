# 🎹 DaisySP STM32H743 Synthesizer

A feature-rich, polyphonic digital synthesizer running on the **WeAct Studio STM32H743VIT6** Mini Core board. This project combines the [DaisySP](https://github.com/electro-smith/DaisySP) DSP library with USB MIDI (via TinyUSB) and an optional web-based controller interface.

![Platform](https://img.shields.io/badge/Platform-STM32H743-blue)
![Framework](https://img.shields.io/badge/Framework-STM32Cube-green)
![DSP](https://img.shields.io/badge/DSP-DaisySP-orange)
![Audio](https://img.shields.io/badge/Audio-48kHz%20I2S-purple)

---

## ✨ Features

### 🎵 Synthesis Engines

Four selectable synthesis modes via MIDI Program Change:

| Mode                | Description                                                                                                                  |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| **0 - Oscillator**  | Dual-oscillator subtractive synth with ADSR envelope, detune, and 8 waveforms (Sin, Tri, Saw, Ramp, Square, pTri, pSaw, pSq) |
| **1 - Pluck**       | Karplus-Strong physical modeling with decay and damping control                                                              |
| **2 - String**      | Extended KS algorithm with brightness, damping, and nonlinearity parameters                                                  |
| **3 - StringVoice** | Rings-style resonator with accent and structure parameters                                                                   |

### 🔊 Effects Chain

Fully modulable effects with individual bypass and mix controls:

- **SVF Filter** – State Variable Filter with LFO modulation, cutoff, resonance, and drive
- **Moog Ladder Filter** – Classic ladder filter emulation
- **Chorus** – Stereo chorus with LFO, depth, delay, and feedback
- **Phaser** – Multi-stage allpass phaser (1-8 poles)
- **Stereo Delay** – Up to ~667ms with cross-feedback for stereo width
- **ReverbSc** – Lush stereo reverb from DaisySP-LGPL

### 🎼 Arpeggiator

Built-in arpeggiator with:

- **BPM**: 60-240
- **Gate Length**: 10-100%
- **Direction**: Up, Down, Up/Down, Random
- **Octave Range**: 1-4 octaves

### 🎹 Polyphony

- **4-voice polyphony** with voice stealing (oldest note)
- Velocity-sensitive
- Full ADSR envelopes on oscillator mode

---

## 🛠️ Hardware Requirements

| Component     | Specification                                                                   |
| ------------- | ------------------------------------------------------------------------------- |
| **MCU Board** | [WeAct Studio Mini STM32H743VIT6](https://github.com/WeActStudio/MiniSTM32H7xx) |
| **Audio DAC** | I2S-compatible (e.g., PCM5100, PCM5102)                                         |
| **Clock**     | 480 MHz Cortex-M7                                                               |
| **RAM**       | 512KB AXI SRAM (D1) + 288KB D2 SRAM for delay buffers                           |
| **Flash**     | 2MB                                                                             |
| **Audio**     | SAI1 (I2S mode) with DMA double-buffering                                       |

### Pin Configuration

| Function   | Pin              |
| ---------- | ---------------- |
| I2S MCLK   | PE2              |
| I2S SCK    | PE5              |
| I2S SD     | PE6              |
| I2S WS     | PE4              |
| USB FS     | PA11/PA12        |
| Status LED | PE3 (active low) |

---

## 📦 Project Structure

```
deisyleuza/
├── src/
│   ├── main.cpp           # Main synth engine, audio callback, MIDI handling
│   ├── audio_sai.cpp/.h   # SAI/I2S audio driver with DMA
│   ├── system.cpp/.h      # Clock configuration (480MHz), MPU setup
│   ├── tusb_config.h      # TinyUSB configuration for USB MIDI
│   ├── usb_descriptors.c  # USB device descriptors
│   └── tinyusb_glue.c     # TinyUSB HAL integration
├── lib/
│   └── DaisySP/           # DaisySP DSP library (LGPL portion)
├── linker/
│   └── STM32H743_WEACT.ld # Custom linker script with optimized memory layout
├── web/
│   └── synth-controller.html # Web MIDI controller interface
├── platformio.ini          # PlatformIO configuration
└── DAISYSP_PORTING_GUIDE.md # Notes on porting DaisySP to this platform
```

---

## 🚀 Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB cable for programming (DFU mode)

### Build & Upload

1. **Clone the repository**:

   ```bash
   git clone https://github.com/your-username/deisyleuza.git
   cd deisyleuza
   ```

2. **Build the firmware**:

   ```bash
   pio run
   ```

3. **Enter DFU mode** on the WeAct board:

   - Hold the BOOT0 button
   - Press and release RESET
   - Release BOOT0

4. **Upload via DFU**:
   ```bash
   pio run -t upload
   ```

---

## 🎛️ MIDI Control Reference

### Program Change (Synth Mode)

| Program | Mode        |
| ------- | ----------- |
| 0       | Oscillator  |
| 1       | Pluck       |
| 2       | String      |
| 3       | StringVoice |
| 4       | FM          |
| 5       | Wavetable   |
| 1       | Pluck       |
| 2       | String      |
| 3       | StringVoice |
| 4       | FM          |
| 5       | Wavetable   |

### Control Change (CC) Map

#### Global Controls

| CC  | Parameter     | Range       |
| --- | ------------- | ----------- |
| 7   | Master Volume | 0.3x - 3.0x |

#### Oscillator Mode

| CC  | Parameter         | Range               |
| --- | ----------------- | ------------------- |
| 70  | Waveform          | 0-7                 |
| 71  | Pulse Width       | 0-1                 |
| 79  | Osc2 Detune       | 0-0.5 semi          |
| 80  | Osc2 Mix          | 0-1                 |
| 24  | Envelope Attack   | 1ms - 2.0s          |
| 25  | Envelope Decay    | 1ms - 2.0s          |
| 26  | Envelope Sustain  | 0-100%              |
| 27  | Envelope Release  | 1ms - 3.0s          |
| 5   | Portamento Time   | 10ms - 1.0s         |
| 84  | Portamento Enable | 0=Off, 127=On       |
| 88  | Velocity Curve    | 0=Lin, 1=Exp, 2=Log |

#### Pluck Mode

| CC  | Parameter |
| --- | --------- |
| 72  | Decay     |
| 73  | Damp      |

#### String Mode

| CC  | Parameter    |
| --- | ------------ |
| 75  | Brightness   |
| 77  | Damping      |
| 78  | Nonlinearity |

#### StringVoice Mode

| CC  | Parameter  |
| --- | ---------- |
| 102 | Brightness |
| 103 | Damping    |
| 104 | Structure  |
| 105 | Accent     |

#### FM Mode

| CC  | Parameter | Range      |
| --- | --------- | ---------- |
| 89  | FM Ratio  | 0.5 - 8.0  |
| 90  | FM Index  | 0.0 - 10.0 |

#### Wavetable Mode

| CC  | Parameter         | Range       |
| --- | ----------------- | ----------- |
| 94  | Waveshape (Morph) | 0.0 - 1.0   |
| 96  | Pulse Width       | -1.0 - +1.0 |

#### SVF Filter

| CC  | Parameter         | Range      |
| --- | ----------------- | ---------- |
| 1   | Cutoff            | 0-1        |
| 21  | Drive             | 0-1        |
| 74  | Resonance         | 0-1        |
| 30  | Filter Env Amount | -1.0 - 1.0 |
| 31  | Filter LFO Amount | 0-1        |
| 76  | Voice LFO Rate    | 0.1-20Hz   |
| 29  | Voice LFO Amp     | 0-1        |
| 64  | Enable/Disable    | Tgl        |

#### Moog Ladder Filter

| CC  | Parameter      |
| --- | -------------- |
| 22  | Cutoff         |
| 23  | Resonance      |
| 65  | Enable/Disable |

#### Effects Enable/Disable (≥64 = ON)

| CC  | Effect      |
| --- | ----------- |
| 64  | SVF Filter  |
| 65  | Moog Filter |
| 66  | Chorus      |
| 67  | Phaser      |
| 68  | Reverb      |
| 69  | Delay       |

#### Effect Parameters

| CC      | Parameter                              |
| ------- | -------------------------------------- |
| 91      | Reverb Mix                             |
| 93      | Chorus Mix                             |
| 95      | Phaser Mix                             |
| 85-87   | Delay (Time/Feedback/Mix)              |
| 113-116 | Chorus (LFO Freq/Depth/Delay/Feedback) |
| 117-120 | Phaser (LFO Freq/Depth/Poles/Feedback) |
| 121-122 | Reverb (Decay/LP Freq)                 |
| 123-125 | Tremolo (Enable/Freq/Depth)            |
| 17-20   | Flanger (Enable/Freq/Depth/Feedback)   |

#### Distortion & Dynamics

| CC  | Parameter        | Range      |
| --- | ---------------- | ---------- |
| 14  | Overdrive Enable | Tgl        |
| 15  | Overdrive Drive  | 0-1        |
| 16  | Overdrive Level  | 0-1        |
| 35  | Bitcrush Enable  | Tgl        |
| 36  | Bitcrush Amount  | 0-1        |
| 37  | Downsample Amt   | 0-1        |
| 40  | Compressor On    | Tgl        |
| 41  | Comp Threshold   | -80 to 0dB |
| 42  | Comp Ratio       | 1-40       |
| 43  | Comp Attack      | 1-500ms    |
| 44  | Comp Release     | 1ms-2s     |

#### Arpeggiator

| CC  | Parameter                                    |
| --- | -------------------------------------------- |
| 108 | Enable/Disable                               |
| 109 | BPM (60-240)                                 |
| 110 | Gate Length (10-100%)                        |
| 111 | Direction (0=Up, 1=Down, 2=UpDown, 3=Random) |
| 112 | Octaves (1-4)                                |

---

## 🌐 Web Controller

Open `web/synth-controller.html` in a browser with Web MIDI support (Chrome, Edge, Opera).

**Features**:

- 🎵 Synthesis mode selection
- 🎼 Arpeggiator controls with real-time value display
- ✨ Full effects control with enable toggles
- 🎹 On-screen keyboard (click or use keys: A-K)
- 🎛️ All parameter sliders with CC routing
- 🔀 MIDI input routing (forward external controller to synth)

The controller will auto-detect devices named "TinyUSB MIDI" and connect automatically.

---

## 💾 Memory Optimization

This project runs the memory-intensive `ReverbSc` effect on limited embedded RAM. Key optimizations include:

1. **Custom Linker Script** (`linker/STM32H743_WEACT.ld`):

   - Heap: 128KB
   - Stack: 16KB
   - Delay buffers placed in RAM_D2 (288KB)

2. **Reduced Reverb Buffer** (`reverbsc.h`):

   - `DSY_REVERBSC_MAX_SIZE` reduced to 48000 samples (~1 second delay, stereo)

3. **Hard Float ABI**:
   - FPv5-D16 FPU utilized via `-mfloat-abi=hard`

See [DAISYSP_PORTING_GUIDE.md](DAISYSP_PORTING_GUIDE.md) for detailed porting notes.

---

## 📝 License

- **DaisySP**: [MIT License](https://github.com/electro-smith/DaisySP/blob/master/LICENSE) / LGPL for certain modules
- **TinyUSB**: [MIT License](https://github.com/hathach/tinyusb/blob/master/LICENSE)
- **Project Code**: MIT License

---

## 🤝 Contributing

Contributions welcome! Please open an issue or submit a pull request.

---

## 📚 References

- [DaisySP Documentation](https://electro-smith.github.io/DaisySP/)
- [WeAct STM32H743 Wiki](https://github.com/WeActStudio/MiniSTM32H7xx)
- [STM32H7 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [TinyUSB MIDI](https://github.com/hathach/tinyusb)
