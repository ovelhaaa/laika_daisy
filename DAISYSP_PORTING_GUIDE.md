# Porting DaisySP-LGPL Effects to WeAct Studio STM32H743

This document details the steps taken to successfully integrate the `DaisySP` DSP library (specifically the LGPL portion containing `ReverbSc`) into a PlatformIO project for the WeAct Studio Mini STM32H743VIT6 board.

## 1. Library Integration

### The Challenge

The standard `DaisySP` library is designed for the Electro-Smith Daisy hardware ecosystem. To use it on a generic STM32H7 board, we needed to manually integrate the source files and ensure the build system could find them.

### Steps Taken

1.  **Source Code**: The `DaisySP-LGPL` repository (containing `ReverbSc`, `Compressor`, `Limiter`, etc.) was cloned/copied into the project.
    - **Location**: `lib/DaisySP/src/DaisySP-LGPL`
    - Moving the files to this exact structure ensures PlatformIO's library dependency finder (LDF) recursively builds them.
2.  **PlatformIO Configuration**:
    - Added include paths to `platformio.ini` to expose the library headers to `main.cpp`.
    ```ini
    build_flags =
        -I$PROJECT_DIR/lib/DaisySP/src
        -I$PROJECT_DIR/lib/DaisySP/src/DaisySP-LGPL
        -I$PROJECT_DIR/lib/DaisySP/src/Utility
    ```
3.  **Missing Dependencies**:
    - The library depends on `dsp.h` and standard C libraries (`stdlib.h`, `math.h`). We ensured these were included or available in the include path.

## 2. Memory Management (The Critical Part)

### The Challenge

The `ReverbSc` module is memory-intensive. By default, it allocates approximately **400KB** of RAM for its delay lines (stereo buffers).

- **Error Encountered**: `region 'RAM_D1' overflowed by 20008 bytes`.
- **Cause**: The STM32H743 has segmented RAM. The default linker script for this board allocates the bulk of global variables to `RAM_D1` (AXI SRAM, 512KB). Between the `ReverbSc` instance, the system Heap, and Stack, we ran out of contiguous memory.

### Steps Taken

1.  **Reduced Heap Size**:
    - Modified `linker/STM32H743_WEACT.ld`.
    - Changed `_Min_Heap_Size` from `0x20000` (128KB) to `0x4000` (16KB).
    - **Result**: Freed up **112KB** of RAM in `RAM_D1` for global variables.
2.  **Optimized Reverb Buffer Size**:
    - Modified `lib/DaisySP/src/DaisySP-LGPL/Effects/reverbsc.h`.
    - Reduced `DSY_REVERBSC_MAX_SIZE` from `98936` to `48000`.
    - **Impact**: Reduced Reverb RAM usage from ~400KB to ~192KB (roughly 1 second of delay time, still very lush).
3.  **Fixed Allocation Logic**:
    - Modified `lib/DaisySP/src/DaisySP-LGPL/Effects/reverbsc.cpp`.
    - The original code calculated allocation in _bytes_, but the buffer pointer arithmetic works on `float*` types.
    - **Fix**: Updated `DelayLineBytesAlloc` logic to return **samples** (counts) instead of bytes, ensuring the internal pointers didn't overshoot the allocated buffer boundaries.

## 3. Audio System Configuration

### The Challenge

DaisySP is just a math library; it doesn't handle hardware audio I/O. We needed a robust audio engine to feed it.

### Steps Taken

1.  **SAI & DMA**:
    - Configured SAI1 (I2S) with DMA (Direct Memory Access) for low-overhead audio transfer.
    - Implemented Double Buffering: The CPU processes the "first half" of the buffer while DMA transmits the "second half", and vice-versa.
2.  **System Clock**:
    - Configured the H743 to run at max speed (**480MHz**) to ensure ample CPU headroom for DSP math.
3.  **Cache Management**:
    - Enabled I-Cache and D-Cache (`SCB_EnableICache`, `SCB_EnableDCache`) for performance.
    - _Note_: For strict correctness with DMA, buffers usually go in a non-cacheable region (D2 SRAM or configured via MPU), but for this specific setup, the simple configuration worked well enough for audio streaming.

## 4. Software Implementation

### Main Loop (`main.cpp`)

1.  **Integration**: Included `daisysp-lgpl.h`.
2.  **Instantiation**: Created global objects for `Oscillator`, `AdEnv`, `Svf` (Filter), and `ReverbSc`.
3.  **Audio Callback**:
    - **Logic**:
      1.  Check Process Clock (Sequencer).
      2.  Generate Note (Oscillator).
      3.  Apply Envelope & Filter.
      4.  **Apply Reverb**: Pass the dry signal into `reverb.Process()`.
    - **Safety**: Added initialization `float wet_left = 0.0f` to prevent noise if the reverb process fails.
    - **Crossfade**: Implemented a simple 60% Dry / 40% Wet mix when Reverb is active.

## Summary of Files Modified

- `platformio.ini`: Built flags and library paths.
- `linker/STM32H743_WEACT.ld`: Heap size reduction.
- `lib/DaisySP/src/DaisySP-LGPL/Effects/reverbsc.h`: Buffer size reduction.
- `lib/DaisySP/src/DaisySP-LGPL/Effects/reverbsc.cpp`: Allocation logic fix.
- `src/main.cpp`: Main audio engine and effect logic.

You now have a portable, optimized DSP engine running on "humble" but powerful hardware!
