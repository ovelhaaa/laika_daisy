#pragma once
#include "daisysp-lgpl.h"
#include "daisysp.h"
#include <math.h>
#include <stdint.h>

using namespace daisysp;

// Synth Modes matching main.cpp
enum SynthMode {
  SYNTH_OSC = 0,
  SYNTH_PLUCK,
  SYNTH_STRING,
  SYNTH_STRINGVOICE,
  SYNTH_FM,
  SYNTH_WAVETABLE,
  SYNTH_MODE_COUNT
};

struct VoiceParams {
  SynthMode mode;

  // Oscillator
  uint8_t osc_waveform;
  float osc_pulsewidth;
  float osc2_detune;
  float osc2_mix;

  // ADSR
  float env_attack;
  float env_decay;
  float env_sustain;
  float env_release;

  // Pluck
  float pluck_decay;
  float pluck_damp;

  // String
  float string_brightness;
  float string_damping;
  float string_nonlinearity;

  // StringVoice
  float sv_brightness;
  float sv_damping;
  float sv_structure;
  float sv_accent;

  // FM
  float fm_ratio;
  float fm_index;

  // Wavetable
  float varsaw_waveshape;
  float varsaw_pw;

  // Portamento
  bool portamento_enabled;
  float portamento_time;
  float velocity_curve;

  // Filter & Logic
  float filter_cutoff;
  float filter_res;
  float filter_env_amt;
  float filter_lfo_amt;
  float lfo_rate;
  float lfo_amp;
};

class SynthVoice {
public:
  void Init(float sample_rate);
  float Process();
  void UpdateParams(const VoiceParams &p, float sample_rate);

  void NoteOn(uint8_t note, float velocity);
  void NoteOff();

  // Use IsRunning() instead of deprecated/missing GetValue()
  bool IsActive() const { return gate || env.IsRunning(); }
  uint8_t GetNote() const { return note; }

  // DSP Objects
  Oscillator osc;
  Oscillator osc2;
  Pluck pluck;
  daisysp::String ks;
  StringVoice stringVoice;
  Fm2 fm;
  VariableSawOscillator varsaw;
  Adsr env;

  // New DSP
  Svf svf;
  Oscillator lfo;

  // State
  bool gate = false;
  bool trig = false;
  uint8_t note = 0;
  float velocity = 0.0f;
  uint32_t age = 0;

  // Portamento
  float current_freq = 440.0f;
  float target_freq = 440.0f;
  float porta_coeff = 1.0f;

  // Internal
  float detune_ratio_prescaler = 1.0f;
  SynthMode active_mode = SYNTH_OSC;
  float pluck_buf[2048];
  float osc2_mix_ = 0.5f;

  // Mod Params (Local caching)
  float lfo_val = 0.0f;
  float filter_env_amt_ = 0.0f;
  float filter_lfo_amt_ = 0.0f;
  float filter_base_freq_ = 1000.0f;

private:
  float sample_rate_;
  void UpdateFrequencies(float freq);
};
