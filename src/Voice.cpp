#include "Voice.h"
#include "daisysp-lgpl.h"
#include "daisysp.h"
#include <cmath>
#include <stdlib.h> // for rand()

void SynthVoice::Init(float sample_rate) {
  sample_rate_ = sample_rate;

  // Oscillator
  osc.Init(sample_rate);
  osc.SetWaveform(Oscillator::WAVE_SAW);
  osc.SetAmp(1.0f);

  osc2.Init(sample_rate);
  osc2.SetWaveform(Oscillator::WAVE_SAW);
  osc2.SetAmp(1.0f);

  // Pluck
  // pluck_buf is now a member array
  pluck.Init(sample_rate, pluck_buf, 2048, PLUCK_MODE_RECURSIVE);
  pluck.SetAmp(1.0f);
  pluck.SetDecay(0.95f);
  pluck.SetDamp(0.5f);

  ks.Init(sample_rate);
  ks.SetBrightness(0.5f);
  ks.SetDamping(0.7f);

  stringVoice.Init(sample_rate);
  stringVoice.SetBrightness(0.5f);
  stringVoice.SetDamping(0.5f);
  stringVoice.SetStructure(0.4f);
  stringVoice.SetAccent(0.8f);

  fm.Init(sample_rate);
  fm.SetRatio(2.0f);
  fm.SetIndex(1.0f);

  varsaw.Init(sample_rate);
  varsaw.SetWaveshape(0.5f);

  env.Init(sample_rate);
  env.SetAttackTime(0.01f);
  env.SetDecayTime(0.2f);
  env.SetSustainLevel(0.7f);
  env.SetReleaseTime(0.3f);

  // New DSP Init
  svf.Init(sample_rate);
  svf.SetRes(0.0f);
  svf.SetDrive(0.0f);
  svf.SetFreq(1000.0f);

  lfo.Init(sample_rate);
  lfo.SetWaveform(Oscillator::WAVE_TRI);
  lfo.SetFreq(0.2f);
  lfo.SetAmp(1.0f);

  gate = false;
  trig = false;
  note = 0;
  velocity = 0.0f;
  age = 0;

  target_freq = 440.0f;
  current_freq = 440.0f;
  active_mode = SYNTH_OSC;
}

void SynthVoice::UpdateParams(const VoiceParams &p, float sample_rate) {
  active_mode = p.mode;
  osc2_mix_ = p.osc2_mix;

  // Cache mod params
  filter_env_amt_ = p.filter_env_amt;
  filter_lfo_amt_ = p.filter_lfo_amt;
  filter_base_freq_ = p.filter_cutoff; // Expected in Hz

  // Update SVF Base
  svf.SetRes(p.filter_res);
  // Drive not yet in params, assume 0 or global? using 0 for now local

  // Update LFO
  lfo.SetFreq(p.lfo_rate);
  lfo.SetAmp(p.lfo_amp);

  // Portamento coeff
  if (p.portamento_enabled && p.portamento_time > 0.001f) {
    porta_coeff = 1.0f - expf(-1.0f / (p.portamento_time * sample_rate));
  } else {
    porta_coeff = 1.0f;
  }

  // Detune ratio
  detune_ratio_prescaler = powf(2.0f, p.osc2_detune / 12.0f);

  // Apply params to engines based on mode
  switch (p.mode) {
  case SYNTH_OSC:
    osc.SetWaveform(p.osc_waveform);
    osc.SetPw(p.osc_pulsewidth);
    osc2.SetWaveform(p.osc_waveform);
    osc2.SetPw(p.osc_pulsewidth);
    env.SetAttackTime(p.env_attack);
    env.SetDecayTime(p.env_decay);
    env.SetSustainLevel(p.env_sustain);
    env.SetReleaseTime(p.env_release);
    break;
  case SYNTH_PLUCK:
    pluck.SetDecay(p.pluck_decay);
    pluck.SetDamp(p.pluck_damp);
    break;
  case SYNTH_STRING:
    ks.SetBrightness(p.string_brightness);
    ks.SetDamping(p.string_damping);
    ks.SetNonLinearity(p.string_nonlinearity);
    break;
  case SYNTH_STRINGVOICE:
    stringVoice.SetBrightness(p.sv_brightness);
    stringVoice.SetDamping(p.sv_damping);
    stringVoice.SetStructure(p.sv_structure);
    stringVoice.SetAccent(p.sv_accent);
    break;
  case SYNTH_FM:
    fm.SetRatio(p.fm_ratio);
    fm.SetIndex(p.fm_index);
    env.SetAttackTime(p.env_attack);
    env.SetDecayTime(p.env_decay);
    env.SetSustainLevel(p.env_sustain);
    env.SetReleaseTime(p.env_release);
    break;
  case SYNTH_WAVETABLE:
    varsaw.SetWaveshape(p.varsaw_waveshape);
    varsaw.SetPW(p.varsaw_pw);
    env.SetAttackTime(p.env_attack);
    env.SetDecayTime(p.env_decay);
    env.SetSustainLevel(p.env_sustain);
    env.SetReleaseTime(p.env_release);
    break;
  default:
    break;
  }
}

void SynthVoice::NoteOn(uint8_t n, float vel) {
  note = n;
  velocity = vel;
  gate = true;
  trig = true;

  float freq = mtof(n);
  target_freq = freq;

  // Use a small epsilon to check if envelope is effectively silent
  // This allows "legato" style portamento if notes overlap and env hasn't fully
  // released
  if (!env.IsRunning()) {
    current_freq = freq;
  } else {
    // Legato play - do not snap, let it glide if porta enabled
    // If porta disabled, coeff is 1.0, so it will snap in Process()
    // immediately.
  }

  // If portamento is OFF, snap immediately (improves feel for chords)
  if (porta_coeff >= 0.999f) {
    current_freq = target_freq;
  }

  // Randomize LFO phase slightly for organic feel?
  // lfo.Reset(rand() / (float)RAND_MAX); // Daisysp oscillator doesn't strictly
  // have Reset() with phase arg in all versions, skipping

  env.Retrigger(false);
}

void SynthVoice::NoteOff() { gate = false; }

float SynthVoice::Process() {
  float sig = 0.0f;

  // 1. Process Modulators
  float env_val = env.Process(gate);
  float lfo_out = lfo.Process();

  // Calculate Filter Cutoff Modulation
  // Base + (Env * Amt) + (LFO * Amt)
  float cut = filter_base_freq_;

  // Env mod (Unipolar 0..1) -> Map to frequency range (e.g. up to 10kHz)
  if (filter_env_amt_ != 0.0f) {
    cut += env_val * filter_env_amt_ * 8000.0f;
  }

  // LFO mod (Bipolar -1..1) -> Map to frequency range
  if (filter_lfo_amt_ != 0.0f) {
    cut += lfo_out * filter_lfo_amt_ * 2000.0f;
  }

  // Clamp safely
  if (cut < 20.0f)
    cut = 20.0f;
  if (cut > 20000.0f)
    cut = 20000.0f;

  svf.SetFreq(cut);

  // Portamento
  if (current_freq != target_freq) {
    current_freq += (target_freq - current_freq) * porta_coeff;
    if (fabsf(target_freq - current_freq) < 0.1f) {
      current_freq = target_freq;
    }
  }

  // Update Frequencies
  UpdateFrequencies(current_freq);

  // Generate Signal match switch(l_synth_mode) from main.cpp
  switch (active_mode) {
  case SYNTH_OSC: {
    // osc.SetAmp(1.0f); // optimization: assumed 1.0
    float sig1 = osc.Process();
    float sig2 = osc2.Process();
    sig = sig1 * (1.0f - osc2_mix_) + sig2 * osc2_mix_;

    // Apply Filter BEFORE VCA (Subtractive style)
    svf.Process(sig);
    sig = svf.Low(); // Default to Lowpass for now. Could be param?

    // Apply VCA
    sig *= env_val * velocity;
    break;
  }
  case SYNTH_PLUCK: {
    float t = trig ? 1.0f : 0.0f;
    sig = pluck.Process(t);
    trig = false;
    sig *= velocity;
    // Pluck usually doesn't need extra filtering, but we can enable it if
    // desired? Leaving raw for now as Pluck has its own damping.
    break;
  }
  case SYNTH_STRING: {
    float excitation =
        gate ? ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.3f : 0.0f;
    sig = ks.Process(excitation);
    sig *= velocity;
    break;
  }
  case SYNTH_STRINGVOICE: {
    if (trig) {
      stringVoice.Trig();
      trig = false;
    }
    sig = stringVoice.Process();
    sig *= velocity;
    break;
  }
  case SYNTH_FM: {
    sig = fm.Process();
    // Filter FM? Sure, why not
    svf.Process(sig);
    sig = svf.Low();

    sig *= env_val * velocity;
    break;
  }
  case SYNTH_WAVETABLE: {
    sig = varsaw.Process();
    // Filter Wavetable
    svf.Process(sig);
    sig = svf.Low();

    sig *= env_val * velocity;
    break;
  }
  default:
    break;
  }

  return sig;
}

void SynthVoice::UpdateFrequencies(float freq) {
  osc.SetFreq(freq);
  osc2.SetFreq(freq * detune_ratio_prescaler);
  pluck.SetFreq(freq);
  ks.SetFreq(freq);
  stringVoice.SetFreq(freq);
  fm.SetFrequency(freq);
  varsaw.SetFreq(freq);
}
