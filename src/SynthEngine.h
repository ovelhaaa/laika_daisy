#pragma once
#include <stdint.h>
#include <stddef.h>
#include "daisysp.h"
#include "daisysp-lgpl.h"
#include "Voice.h"
#include "FastMath.h"

// Abstração de memória para compatibilidade cruzada (STM32 / Wasm)
#ifdef __EMSCRIPTEN__
    #define D2_RAM_ATTR
#else
    #define D2_RAM_ATTR __attribute__((section(".ram_d2")))
#endif

// Constantes
#define DELAY_MAX_SAMPLES 32000
#define NUM_VOICES 4

class SynthEngine {
public:
    void Init(float sample_rate);
    void Process(float* out_l, float* out_r, size_t size);
    void ProcessMidiMessage(uint8_t status, uint8_t data1, uint8_t data2);
    void Panic();

private:
    // Métodos auxiliares internos
    int AllocateVoice(uint8_t note);
    float applyVelocityCurve(float velocity, uint8_t curve);
    void ArpTriggerNote(uint8_t note);
    void ArpReleaseNote();
    uint8_t ArpGetNextNote();
    void ArpUpdateTiming();
    inline float softClip(float x) {
        return FastMath::CubicSoftClip(x);
    }

    // --- Estado do Sintetizador ---
    SynthMode synth_mode;
    float master_volume;
    float sample_rate_;

    // Vozes
    SynthVoice voices[NUM_VOICES];
    int voice_counter;

    // Efeitos Master
    daisysp::MoogLadder moog;
    daisysp::Chorus chorus;
    daisysp::Phaser phaser;
    daisysp::ReverbSc reverb;
    daisysp::Tremolo tremolo;
    daisysp::Flanger flanger;
    daisysp::Overdrive overdrive;
    daisysp::Decimator bitcrusher;
    daisysp::Compressor compressor;

    // Buffers de Delay (Protegidos pela macro D2_RAM_ATTR)
    D2_RAM_ATTR float delay_buf_left[DELAY_MAX_SAMPLES];
    D2_RAM_ATTR float delay_buf_right[DELAY_MAX_SAMPLES];
    size_t delay_write_ptr;
    float delay_frac;

    // --- Parameters ---
    bool svf_enabled;
    bool moog_enabled;
    bool chorus_enabled;
    bool phaser_enabled;
    bool delay_enabled;
    bool reverb_enabled;

    float chorus_mix;
    float phaser_mix;
    float reverb_mix;

    float delay_time;
    float delay_feedback;
    float delay_mix;

    float chorus_lfo_freq;
    float chorus_lfo_depth;
    float chorus_delay;
    float chorus_feedback;

    float phaser_lfo_freq;
    float phaser_lfo_depth;
    uint8_t phaser_poles;
    float phaser_feedback;

    float reverb_feedback;
    float reverb_lpfreq;

    bool tremolo_enabled;
    float tremolo_freq;
    float tremolo_depth;

    bool flanger_enabled;
    float flanger_freq;
    float flanger_depth;
    float flanger_feedback;

    bool overdrive_enabled;
    float overdrive_drive;
    float overdrive_level;

    bool bitcrusher_enabled;
    float bitcrush_amount;
    float downsample_amount;

    bool compressor_enabled;
    float comp_ratio;
    float comp_thresh;
    float comp_attack;
    float comp_release;

    float filter_cutoff;
    float filter_res;
    float filter_drive;
    float filter_env_amt;
    float filter_lfo_amt;

    float lfo_rate;
    float lfo_amp;

    float moog_cutoff;
    float moog_res;

    uint8_t osc_waveform;
    float osc_pulsewidth;
    float osc2_detune;
    float osc2_mix;

    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;

    bool portamento_enabled;
    float portamento_time;

    uint8_t velocity_curve;

    float fm_ratio;
    float fm_index;

    float varsaw_waveshape;
    float varsaw_pw;

    float pluck_decay;
    float pluck_damp;

    float string_brightness;
    float string_damping;
    float string_nonlinearity;

    float sv_brightness;
    float sv_damping;
    float sv_structure;
    float sv_accent;

    // Arpeggiator state
    bool arp_enabled;
    float arp_bpm;
    float arp_gate;
    uint8_t arp_direction;
    uint8_t arp_octaves;

    uint8_t arp_notes[8];
    uint8_t arp_note_count;
    int8_t arp_index;
    int8_t arp_current_octave;
    bool arp_going_up;
    uint8_t arp_current_note;

    uint32_t arp_sample_counter;
    uint32_t arp_gate_samples;
    bool arp_note_playing;

    uint32_t arp_phase_accumulator;
    uint32_t arp_phase_increment;
};
