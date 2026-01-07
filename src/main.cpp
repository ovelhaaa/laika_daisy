#include "main.h"
#include "FastMath.h"
#include "Voice.h" // Includes SynthMode and SynthVoice
#include "audio_sai.h"
#include "daisysp-lgpl.h" // Includes ReverbSc, Pluck
#include "daisysp.h"
#include "stm32h7xx_hal.h"
#include "system.h"
#include <math.h>
#include <stdlib.h>
#include <tusb.h>

using namespace daisysp;

// =============================================================================
// GLOBAL VARIABLES & OBJECTS
// =============================================================================

// System
const int SAMPLE_RATE = 48000;
const int NUM_VOICES = 4;

// Audio Buffer (for USB)
// 48 samples * 2 channels * 2 bytes/sample = 192 bytes per packet
// We need enough buffer for jitter. 1ms at 48k = 48 samples.
// TinyUSB usually handles one SOF (1ms) per transfer.
// Let's use a reasonable block size.
#define AUDIO_BLOCK_SIZE 48
extern int32_t audio_tx_buf[2 * AUDIO_BLOCK_SIZE]; // Stereo buffer
extern volatile uint8_t req_fill_first_half;
extern volatile uint8_t req_fill_second_half;

// Synth Mode
volatile SynthMode synth_mode = SYNTH_OSC;

// Voices
// Voices
SynthVoice voices[NUM_VOICES]; // Using new class
int voice_counter = 0;         // For LRU allocation
Oscillator lfw;                // Shared LFO for filter sweep
// Svf filter;                 // REMOVED: Now per-voice (SynthVoice::svf)
MoogLadder moog; // Moog Ladder Filter (Master effect)

// Effects chain
Chorus chorus;
Phaser phaser;
ReverbSc reverb;
Tremolo tremolo;       // NEW: Amplitude modulation
Flanger flanger;       // NEW: Flanging effect
Overdrive overdrive;   // NEW: Distortion/saturation
Decimator bitcrusher;  // NEW: Downsample/Bitcrush
Compressor compressor; // NEW: Dynamics compression

// Stereo Delay - buffers in RAM_D2 (~667ms max at 48kHz)
#define DELAY_MAX_SAMPLES 32000
__attribute__((section(".ram_d2"))) float delay_buf_left[DELAY_MAX_SAMPLES];
__attribute__((section(".ram_d2"))) float delay_buf_right[DELAY_MAX_SAMPLES];
size_t delay_write_ptr = 0;
float delay_frac = 0.0f;

// Enable/disable toggles (controlled via MIDI CC 64-69)
volatile bool svf_enabled =
    true; // Still used to enable/disable per-voice filters?
volatile bool moog_enabled = false;
volatile bool chorus_enabled = true;
volatile bool phaser_enabled = false;
volatile bool delay_enabled = false; // Off by default
volatile bool reverb_enabled = true;

// Effect amounts (0.0 = dry, 1.0 = full wet)
volatile float chorus_mix = 0.3f;
volatile float phaser_mix = 0.5f;
volatile float reverb_mix = 0.4f;

// Delay parameters
volatile float delay_time = 0.25f;
volatile float delay_feedback = 0.4f;
volatile float delay_mix = 0.3f;

// Chorus parameters
volatile float chorus_lfo_freq = 0.5f;  // LFO freq in Hz
volatile float chorus_lfo_depth = 0.3f; // LFO depth 0-1
volatile float chorus_delay = 10.0f;    // Delay time in ms (0-50)
volatile float chorus_feedback = 0.2f;  // Feedback 0-1

// Phaser parameters
volatile float phaser_lfo_freq = 0.3f;  // LFO freq in Hz
volatile float phaser_lfo_depth = 0.8f; // LFO depth 0-1
volatile uint8_t phaser_poles = 4;      // Allpass stages (1-8)
volatile float phaser_feedback = 0.5f;  // Feedback 0-1

// Reverb parameters
volatile float reverb_feedback = 0.9f;  // Reverb time (0-1)
volatile float reverb_lpfreq = 8000.0f; // LP filter freq (200-16000 Hz)

// === NEW: Tremolo parameters ===
volatile bool tremolo_enabled = false;
volatile float tremolo_freq = 5.0f;  // LFO freq in Hz (0.5-20)
volatile float tremolo_depth = 0.5f; // Depth 0-1

// === NEW: Flanger parameters ===
volatile bool flanger_enabled = false;
volatile float flanger_freq = 0.5f;     // LFO freq in Hz (0.1-5)
volatile float flanger_depth = 0.5f;    // Depth 0-1
volatile float flanger_feedback = 0.5f; // Feedback 0-1

// === NEW: Overdrive parameters ===
volatile bool overdrive_enabled = false;
volatile float overdrive_drive = 0.5f; // Drive amount 0-1
volatile float overdrive_level = 1.0f; // Output level compensation 0-1

// === NEW: Bitcrusher parameters ===
volatile bool bitcrusher_enabled = false;
volatile float bitcrush_amount = 0.0f;   // 0-1 (Bit reduction)
volatile float downsample_amount = 0.0f; // 0-1 (Sample rate reduction)

// === NEW: Compressor parameters ===
volatile bool compressor_enabled = false;
volatile float comp_ratio = 4.0f;    // 1-40
volatile float comp_thresh = -20.0f; // 0 to -80 dB
volatile float comp_attack = 0.05f;  // 0.001 - 10s
volatile float comp_release = 0.2f;  // 0.001 - 10s

// SVF Filter parameters (Per-Voice)
volatile float filter_cutoff = 1000.0f; // Hz
volatile float filter_res = 0.0f;
volatile float filter_drive = 0.0f;
volatile float filter_env_amt = 0.0f; // -1.0 to 1.0 (Amt to add to cutoff)
volatile float filter_lfo_amt = 0.0f; // -1.0 to 1.0 (Amt to add to cutoff)

// Voice LFO Parameters
volatile float lfo_rate = 1.0f; // Hz
volatile float lfo_amp = 0.0f;  // 0-1 (Global LFO Amplitude?)

// Moog Filter parameters
volatile float moog_cutoff = 0.5f;
volatile float moog_res = 0.3f;

// Oscillator voice parameters
volatile uint8_t osc_waveform = 2; // Default: SAW
volatile float osc_pulsewidth = 0.5f;
volatile float osc2_detune =
    0.1f;                       // Detune in semitones (0-1 maps to 0-24 cents)
volatile float osc2_mix = 0.5f; // 0 = osc1 only, 1 = osc2 only, 0.5 = equal mix

// ADSR Envelope parameters
volatile float env_attack = 0.01f; // Attack time in seconds (0.001 - 2.0)
volatile float env_decay = 0.2f;   // Decay time in seconds (0.001 - 2.0)
volatile float env_sustain = 0.7f; // Sustain level (0.0 - 1.0)
volatile float env_release = 0.3f; // Release time in seconds (0.001 - 3.0)

// === NEW: Portamento/Glide ===
volatile bool portamento_enabled = false; // CC84 toggle
volatile float portamento_time = 0.1f;    // CC5: Glide time 0.01-1.0 seconds

// === NEW: Velocity Curves ===
// 0 = Linear, 1 = Exponential (more dynamic), 2 = Logarithmic (compressed)
volatile uint8_t velocity_curve = 0; // CC88

// === NEW: FM Synthesis parameters ===
volatile float fm_ratio = 2.0f; // CC89: Mod/Carrier ratio (0.5-8.0)
volatile float fm_index = 1.0f; // CC90: Modulation index (0-10)

// === NEW: Wavetable/VariableSaw parameters ===
volatile float varsaw_waveshape = 0.5f; // CC94: Waveform morph (0-1)
volatile float varsaw_pw = 0.0f;        // CC96: Pulse Width (-1 to +1)

// Pluck voice parameters
volatile float pluck_decay = 0.95f;
volatile float pluck_damp = 0.5f;

// String voice parameters (KarplusString)
volatile float string_brightness = 0.5f;
volatile float string_damping = 0.7f;
volatile float string_nonlinearity =
    0.0f; // -1 to 1 (curved bridge to dispersion)

// StringVoice parameters (Rings-style)
volatile float sv_brightness = 0.5f;
volatile float sv_damping = 0.5f;
volatile float sv_structure = 0.4f;
volatile float sv_accent = 0.8f;

// Master output volume (1.0 = current safe level, max 3.0)
volatile float master_volume = 1.0f;

// Arpeggiator state
volatile bool arp_enabled = false;
volatile float arp_bpm = 120.0f;    // 60-240 BPM
volatile float arp_gate = 0.5f;     // Gate length (0-1)
volatile uint8_t arp_direction = 0; // 0=Up, 1=Down, 2=UpDown, 3=Random
volatile uint8_t arp_octaves = 1;   // 1-4 octaves

// Arpeggiator note buffer
uint8_t arp_notes[8];
uint8_t arp_note_count = 0;
int8_t arp_index = 0;
int8_t arp_current_octave = 0;
bool arp_going_up = true;
uint8_t arp_current_note = 0;

// Arpeggiator timing (in samples)
uint32_t arp_sample_counter = 0;
uint32_t arp_gate_samples = 0;
bool arp_note_playing = false;

// Find voice to use: prefer free voice, otherwise steal oldest
int AllocateVoice(uint8_t note) {
  int free_voice = -1;
  int oldest_voice = 0;
  uint32_t oldest_age = 0;

  for (int i = 0; i < NUM_VOICES; i++) {
    // Check if this note is already playing (retrigger same voice)
    if (voices[i].note == note && voices[i].gate) {
      return i;
    }
    // Find a free voice
    if (!voices[i].gate && !voices[i].env.IsRunning()) {
      free_voice = i;
    }
    // Track oldest voice for stealing
    if (voices[i].age > oldest_age) {
      oldest_age = voices[i].age;
      oldest_voice = i;
    }
  }

  return (free_voice >= 0) ? free_voice : oldest_voice;
}

// === NEW: Velocity Curve Processing ===
// Applies selected curve to velocity for more expressive playing
float applyVelocityCurve(float velocity, uint8_t curve) {
  switch (curve) {
  case 0: // Linear (default)
    return velocity;
  case 1: // Exponential (more dynamic range, quiet notes quieter)
    return velocity * velocity;
  case 2: // Logarithmic (compressed, quiet notes louder)
    return sqrtf(velocity);
  default:
    return velocity;
  }
}

// Arpeggiator: trigger a note with current settings
void ArpTriggerNote(uint8_t note) {
  int v = AllocateVoice(note);
  float freq = mtof(note);

  // Set all oscillator frequencies
  voices[v].osc.SetFreq(freq);
  voices[v].osc2.SetFreq(freq * powf(2.0f, osc2_detune / 12.0f));
  voices[v].pluck.SetFreq(freq);
  voices[v].ks.SetFreq(freq);
  voices[v].stringVoice.SetFreq(freq);
  voices[v].fm.SetFrequency(freq);
  voices[v].varsaw.SetFreq(freq);

  // Portamento target
  voices[v].target_freq = freq;
  if (!portamento_enabled) {
    voices[v].current_freq = freq; // No glide, instant
  }

  voices[v].velocity = 0.8f; // Fixed velocity for arp
  voices[v].note = note;
  voices[v].gate = true;
  voices[v].trig = true;
  voices[v].age = ++voice_counter;
  voices[v].env.Retrigger(false);

  arp_current_note = note;
}

// Arpeggiator: release current note
void ArpReleaseNote() {
  for (int v = 0; v < NUM_VOICES; v++) {
    if (voices[v].note == arp_current_note && voices[v].gate) {
      voices[v].gate = false;
    }
  }
}

// Arpeggiator: get next note in sequence
uint8_t ArpGetNextNote() {
  if (arp_note_count == 0)
    return 0;

  uint8_t base_note = arp_notes[arp_index];
  uint8_t note = base_note + (arp_current_octave * 12);

  // Advance to next note based on direction
  switch (arp_direction) {
  case 0: // Up
    arp_index++;
    if (arp_index >= arp_note_count) {
      arp_index = 0;
      arp_current_octave++;
      if (arp_current_octave >= arp_octaves)
        arp_current_octave = 0;
    }
    break;
  case 1: // Down
    arp_index--;
    if (arp_index < 0) {
      arp_index = arp_note_count - 1;
      arp_current_octave--;
      if (arp_current_octave < 0)
        arp_current_octave = arp_octaves - 1;
    }
    break;
  case 2: // Up/Down
    if (arp_going_up) {
      arp_index++;
      if (arp_index >= arp_note_count) {
        if (arp_current_octave < arp_octaves - 1) {
          arp_current_octave++;
          arp_index = 0;
        } else {
          arp_going_up = false;
          arp_index = arp_note_count - 2;
          if (arp_index < 0)
            arp_index = 0;
        }
      }
    } else {
      arp_index--;
      if (arp_index < 0) {
        if (arp_current_octave > 0) {
          arp_current_octave--;
          arp_index = arp_note_count - 1;
        } else {
          arp_going_up = true;
          arp_index = 1;
          if (arp_index >= arp_note_count)
            arp_index = 0;
        }
      }
    }
    break;
  case 3: // Random
    arp_index = rand() % arp_note_count;
    arp_current_octave = rand() % arp_octaves;
    break;
  }

  return note;
}

// === Arpeggiator Timing (integer-based for jitter-free operation) ===
// Uses fixed-point phase accumulator instead of floating-point
uint32_t arp_phase_accumulator = 0;
uint32_t arp_phase_increment = 0; // Calculated from BPM

// Update arpeggiator timing parameters (call when BPM changes)
void ArpUpdateTiming() {
  // Phase increment = (BPM / 60) * (2^32 / SAMPLE_RATE)
  // This gives us sample-accurate timing without float accumulation errors
  float beats_per_sample = arp_bpm / (60.0f * SAMPLE_RATE);
  arp_phase_increment = (uint32_t)(beats_per_sample * 4294967296.0f);
}

// === Musical Soft Clipper ===
// Fast tanh approximation using rational polynomial (Pade approximant)
// Provides smooth, tube-like saturation without harsh digital clipping
inline float softClip(float x) {
  // Use optimized cubic soft clip from FastMath
  // Avoids division operations for better performance
  return FastMath::CubicSoftClip(x);
}

// Global Panic Function
void Panic() {
  for (int i = 0; i < NUM_VOICES; i++) {
    voices[i].gate = false;
    voices[i].trig = false;
    voices[i].velocity = 0.0f;
    voices[i].env.SetSustainLevel(0.0f); // Force envelope down
    voices[i].age = 0;
  }
  // Reset Arpeggiator
  arp_note_count = 0;
  arp_note_playing = false;

  // Turn off LED
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
}

void AudioCallback(int32_t *buffer, size_t size) {
  // ===================================================================
  // BLOCK START: Copy all volatile parameters to local variables ONCE
  // This avoids memory reads every sample and enables register optimization
  // ===================================================================

  // Synth mode and master
  SynthMode l_synth_mode = synth_mode;
  float l_master_volume = master_volume;

  // Oscillator parameters
  uint8_t l_osc_waveform = osc_waveform;
  float l_osc_pulsewidth = osc_pulsewidth;
  float l_osc2_detune = osc2_detune;
  float l_osc2_mix = osc2_mix;
  // Detune ratio optimization moved to SynthVoice::UpdateParams

  // ADSR parameters
  float l_env_attack = env_attack;
  float l_env_decay = env_decay;
  float l_env_sustain = env_sustain;
  float l_env_release = env_release;

  // Pluck parameters
  float l_pluck_decay = pluck_decay;
  float l_pluck_damp = pluck_damp;

  // String parameters
  float l_string_brightness = string_brightness;
  float l_string_damping = string_damping;
  float l_string_nonlinearity = string_nonlinearity;

  // StringVoice parameters
  float l_sv_brightness = sv_brightness;
  float l_sv_damping = sv_damping;
  float l_sv_structure = sv_structure;
  float l_sv_accent = sv_accent;

  // Filter parameters
  bool l_svf_enabled = svf_enabled;
  bool l_moog_enabled = moog_enabled;
  float l_filter_cutoff = filter_cutoff;
  float l_filter_res = filter_res;
  float l_filter_env_amt = filter_env_amt;
  float l_filter_lfo_amt = filter_lfo_amt;
  float l_lfo_rate = lfo_rate;
  float l_lfo_amp = lfo_amp;

  float l_moog_cutoff = moog_cutoff;
  float l_moog_res = moog_res;

  // Effect enables
  bool l_chorus_enabled = chorus_enabled;
  bool l_phaser_enabled = phaser_enabled;
  bool l_delay_enabled = delay_enabled;
  bool l_reverb_enabled = reverb_enabled;

  // Effect mix levels
  float l_chorus_mix = chorus_mix;
  float l_phaser_mix = phaser_mix;
  float l_delay_mix = delay_mix;
  float l_reverb_mix = reverb_mix;

  // Delay parameters
  float l_delay_time = delay_time;
  float l_delay_feedback = delay_feedback;

  // Reverb parameters
  float l_reverb_feedback = reverb_feedback;
  float l_reverb_lpfreq = reverb_lpfreq;

  // === NEW: Effect parameters ===
  bool l_tremolo_enabled = tremolo_enabled;
  float l_tremolo_freq = tremolo_freq;
  float l_tremolo_depth = tremolo_depth;

  bool l_flanger_enabled = flanger_enabled;
  float l_flanger_freq = flanger_freq;
  float l_flanger_depth = flanger_depth;
  float l_flanger_feedback = flanger_feedback;

  bool l_overdrive_enabled = overdrive_enabled;
  float l_overdrive_drive = overdrive_drive;
  float l_overdrive_level = overdrive_level;

  bool l_bitcr_enabled = bitcrusher_enabled;
  float l_bitcrush = bitcrush_amount;
  float l_downsample = downsample_amount;

  bool l_comp_enabled = compressor_enabled;
  float l_comp_thresh = comp_thresh;
  float l_comp_ratio = comp_ratio;
  float l_comp_attack = comp_attack;
  float l_comp_release = comp_release;

  // Arpeggiator parameters

  // Arpeggiator parameters
  bool l_arp_enabled = arp_enabled;
  float l_arp_bpm = arp_bpm;
  float l_arp_gate = arp_gate;

  // Portamento parameters
  bool l_portamento_enabled = portamento_enabled;
  float l_portamento_time = portamento_time;
  // Calculate glide coefficient: how much to move toward target per sample
  // Using exponential smoothing: coeff = 1 - e^(-1/(time * samplerate))
  float porta_coeff = 1.0f - expf(-1.0f / (l_portamento_time * SAMPLE_RATE));

  // ===================================================================
  // BLOCK START: Update effect parameters ONCE per block (not per sample)
  // ===================================================================

  // LFO rate (only changes on CC)
  // LFO rate (only changes on CC)
  // lfw removed - LFO is now per-voice

  // Moog filter parameters
  if (l_moog_enabled) {
    float moog_freq = 100.0f + (l_moog_cutoff * 7900.0f);
    moog.SetFreq(moog_freq);
    moog.SetRes(l_moog_res);
  }

  // Chorus parameters
  if (l_chorus_enabled) {
    chorus.SetLfoFreq(chorus_lfo_freq);
    chorus.SetLfoDepth(chorus_lfo_depth);
    chorus.SetDelayMs(chorus_delay);
    chorus.SetFeedback(chorus_feedback);
  }

  // Phaser parameters
  if (l_phaser_enabled) {
    phaser.SetLfoFreq(phaser_lfo_freq);
    phaser.SetLfoDepth(phaser_lfo_depth);
    phaser.SetPoles(phaser_poles);
    phaser.SetFeedback(phaser_feedback);
  }

  // Reverb parameters
  if (l_reverb_enabled) {
    reverb.SetFeedback(l_reverb_feedback);
    reverb.SetLpFreq(l_reverb_lpfreq);
  }

  // === NEW: Update new effect parameters ===
  if (l_tremolo_enabled) {
    tremolo.SetFreq(l_tremolo_freq);
    tremolo.SetDepth(l_tremolo_depth);
  }
  if (l_flanger_enabled) {
    flanger.SetLfoFreq(l_flanger_freq);
    flanger.SetLfoDepth(l_flanger_depth);
    flanger.SetFeedback(l_flanger_feedback);
  }
  if (l_overdrive_enabled) {
    overdrive.SetDrive(l_overdrive_drive);
  }
  if (l_bitcr_enabled) {
    bitcrusher.SetBitcrushFactor(l_bitcrush);
    bitcrusher.SetDownsampleFactor(l_downsample);
  }
  if (l_comp_enabled) {
    compressor.SetThreshold(l_comp_thresh);
    compressor.SetRatio(l_comp_ratio);
    compressor.SetAttack(l_comp_attack);
    compressor.SetRelease(l_comp_release);
  }

  // ADSR parameters for all voices (apply once per block)
  // === Update Voice Parameters ===
  VoiceParams p;
  p.mode = l_synth_mode;
  p.osc_waveform = l_osc_waveform;
  p.osc_pulsewidth = l_osc_pulsewidth;
  p.osc2_detune = l_osc2_detune;
  p.osc2_mix = l_osc2_mix;
  p.env_attack = l_env_attack;
  p.env_decay = l_env_decay;
  p.env_sustain = l_env_sustain;
  p.env_release = l_env_release;
  p.pluck_decay = l_pluck_decay;
  p.pluck_damp = l_pluck_damp;
  p.string_brightness = l_string_brightness;
  p.string_damping = l_string_damping;
  p.string_nonlinearity = l_string_nonlinearity;
  p.sv_brightness = l_sv_brightness;
  p.sv_damping = l_sv_damping;
  p.sv_structure = l_sv_structure;
  p.sv_accent = l_sv_accent;
  p.fm_ratio = fm_ratio;                 // Global
  p.fm_index = fm_index;                 // Global
  p.varsaw_waveshape = varsaw_waveshape; // Global
  p.varsaw_pw = varsaw_pw;               // Global
  p.portamento_enabled = l_portamento_enabled;
  p.portamento_time = l_portamento_time;
  p.velocity_curve = (float)velocity_curve; // Global

  // Filter & Mod Params
  // Map 0-1 cutoff to Hz (Logarithmic-ish mapping)
  // 20Hz to 12000Hz
  p.filter_cutoff = 20.0f + (l_filter_cutoff * l_filter_cutoff * 12000.0f);
  p.filter_res = l_filter_res;
  p.filter_env_amt = l_filter_env_amt;
  p.filter_lfo_amt = l_filter_lfo_amt;
  p.lfo_rate = l_lfo_rate * 20.0f; // 0-1 -> 0-20Hz
  p.lfo_amp = l_lfo_amp;

  for (int v = 0; v < NUM_VOICES; v++) {
    voices[v].UpdateParams(p, SAMPLE_RATE);
  }

  // Arpeggiator timing update (integer-based)
  uint32_t l_arp_phase_inc =
      (uint32_t)((l_arp_bpm / (60.0f * SAMPLE_RATE)) * 4294967296.0f);
  uint32_t l_arp_gate_threshold = (uint32_t)(l_arp_gate * 4294967296.0f);

  // Pre-calculate delay read parameters
  float delay_samples = l_delay_time * (DELAY_MAX_SAMPLES - 1);
  size_t delay_int = (size_t)delay_samples;
  float delay_frac_local = delay_samples - (float)delay_int;

  // ===================================================================
  // SAMPLE LOOP: Per-sample processing using local variables
  // ===================================================================
  for (size_t i = 0; i < size; i++) {

    // --- Arpeggiator Processing (integer-based timing) ---
    if (l_arp_enabled && arp_note_count > 0) {
      uint32_t prev_phase = arp_phase_accumulator;
      arp_phase_accumulator += l_arp_phase_inc;

      // Detect phase wrap (new beat)
      if (arp_phase_accumulator < prev_phase) {
        // New step - trigger next note
        uint8_t note = ArpGetNextNote();
        if (note > 0) {
          ArpTriggerNote(note);
          arp_note_playing = true;
        }
      }

      // Gate off detection
      if (arp_note_playing && arp_phase_accumulator >= l_arp_gate_threshold) {
        ArpReleaseNote();
        arp_note_playing = false;
      }
    }

    // --- Mix all voices ---
    float mix = 0.0f;
    for (int v = 0; v < NUM_VOICES; v++) {
      mix += voices[v].Process();
    }

    // Scale down to prevent clipping
    mix *= (1.0f / NUM_VOICES);

    // Overdrive (Distortion) - Applied before filters
    if (l_overdrive_enabled) {
      mix = overdrive.Process(mix);
      mix *= l_overdrive_level; // Output level compensation
    }

    // Bitcrusher
    if (l_bitcr_enabled) {
      mix = bitcrusher.Process(mix);
    }

    // --- Filters ---
    float filtered = mix;

    // SVF is now per-voice, so we skip global processing here.

    if (l_moog_enabled) {
      filtered = moog.Process(filtered);
    }

    // Tremolo (Amplitude Modulation)
    if (l_tremolo_enabled) {
      filtered = tremolo.Process(filtered);
    }

    // Flanger (Mono)
    if (l_flanger_enabled) {
      filtered = flanger.Process(filtered);
    }

    // --- Effects Chain ---
    float out_left = filtered;
    float out_right = filtered;

    // Chorus
    if (l_chorus_enabled) {
      chorus.Process(filtered);
      out_left =
          filtered * (1.0f - l_chorus_mix) + chorus.GetLeft() * l_chorus_mix;
      out_right =
          filtered * (1.0f - l_chorus_mix) + chorus.GetRight() * l_chorus_mix;
    }

    // Phaser
    if (l_phaser_enabled) {
      float ph_left = phaser.Process(out_left);
      float ph_right = phaser.Process(out_right);
      out_left = out_left * (1.0f - l_phaser_mix) + ph_left * l_phaser_mix;
      out_right = out_right * (1.0f - l_phaser_mix) + ph_right * l_phaser_mix;
    }

    // Delay (stereo with feedback)
    if (l_delay_enabled) {
      size_t read_pos =
          (delay_write_ptr + DELAY_MAX_SAMPLES - delay_int) % DELAY_MAX_SAMPLES;
      size_t read_pos2 = (read_pos + DELAY_MAX_SAMPLES - 1) % DELAY_MAX_SAMPLES;
      float del_l = delay_buf_left[read_pos] +
                    (delay_buf_left[read_pos2] - delay_buf_left[read_pos]) *
                        delay_frac_local;
      float del_r = delay_buf_right[read_pos] +
                    (delay_buf_right[read_pos2] - delay_buf_right[read_pos]) *
                        delay_frac_local;

      delay_buf_left[delay_write_ptr] =
          out_left + del_r * l_delay_feedback * 0.7f;
      delay_buf_right[delay_write_ptr] =
          out_right + del_l * l_delay_feedback * 0.7f;
      delay_write_ptr = (delay_write_ptr + 1) % DELAY_MAX_SAMPLES;

      out_left = out_left * (1.0f - l_delay_mix) + del_l * l_delay_mix;
      out_right = out_right * (1.0f - l_delay_mix) + del_r * l_delay_mix;
    }

    // Reverb
    if (l_reverb_enabled) {
      float rev_left = 0.0f, rev_right = 0.0f;
      reverb.Process(out_left, out_right, &rev_left, &rev_right);
      out_left = out_left * (1.0f - l_reverb_mix) + rev_left * l_reverb_mix;
      out_right = out_right * (1.0f - l_reverb_mix) + rev_right * l_reverb_mix;
    }

    // Compressor (Master Bus)
    if (l_comp_enabled) {
      // Sidechain key same as input for now (standard compression)
      out_left = compressor.Process(out_left);
      out_right = compressor.Process(out_right);
    }

    // --- Musical Soft Clipper + Output ---
    // Apply gain and soft clip for warm, tube-like saturation
    float amp = 0.1f * l_master_volume;
    float clipped_l = softClip(out_left * amp * 1.5f); // Drive into soft clip
    float clipped_r = softClip(out_right * amp * 1.5f);

    // Scale to 32-bit output (already limited to ±1.0 by soft clipper)
    int32_t sl = (int32_t)(clipped_l * 2147483647.0f);
    int32_t sr = (int32_t)(clipped_r * 2147483647.0f);

    buffer[i * 2] = sl;
    buffer[i * 2 + 1] = sr;
  }
}

// MIDI Callback
void tud_midi_rx_cb(uint8_t itf) {
  uint8_t packet[4];
  while (tud_midi_packet_read(packet)) {
    uint8_t status = packet[1] & 0xF0;
    uint8_t data1 = packet[2];
    uint8_t data2 = packet[3];

    // Program Change (0xC0) - Switch synth mode
    if (status == 0xC0) {
      switch (data1) {
      case 0:
        synth_mode = SYNTH_OSC;
        break;
      case 1:
        synth_mode = SYNTH_PLUCK;
        break;
      case 2:
        synth_mode = SYNTH_STRING;
        break;
      case 3:
        synth_mode = SYNTH_STRINGVOICE;
        break;
      case 4:
        synth_mode = SYNTH_FM;
        break;
      case 5:
        synth_mode = SYNTH_WAVETABLE;
        break;
      default:
        synth_mode = SYNTH_OSC;
        break;
      }
    }

    // Control Change (0xB0) - Effects, Filters, and Toggles
    if (status == 0xB0) {
      float val = (float)data2 / 127.0f;
      bool toggle = data2 >= 64; // CC value >= 64 = ON

      switch (data1) {
      // SVF Filter controls
      case 1:
        filter_cutoff = val;
        break; // CC1  = SVF Cutoff
      case 21:
        filter_drive = val;
        break; // CC21 = SVF Drive
      case 74:
        filter_res = val;
        break; // CC74 = SVF Resonance
      case 76:
        lfo_rate = 0.1f + val * 19.9f;
        break; // CC76 = LFO Rate (0.1 - 20Hz)

      // Modulation Controls
      case 29:
        lfo_amp = val;
        break; // CC29 = LFO Amp (Global)
      case 30:
        filter_env_amt = val * 2.0f - 1.0f; // Bipolar -1 to 1
        break;                              // CC30 = Filter Env Amount
      case 31:
        filter_lfo_amt = val;
        break; // CC31 = Filter LFO Amount

      // Oscillator voice controls
      case 70:
        osc_waveform = (uint8_t)(val * 7.99f);
        break; // CC70 = Waveform (0-7)
      case 71:
        osc_pulsewidth = val;
        break; // CC71 = Pulse Width
      case 79:
        osc2_detune = val * 0.5f;
        break; // CC79 = Osc2 Detune (0-0.5 semitones)
      case 80:
        osc2_mix = val;
        break; // CC80 = Osc2 Mix

      // ADSR Envelope controls
      case 24:
        env_attack = 0.001f + val * 1.999f;
        break; // CC24 = Attack (0.001-2.0s)
      case 25:
        env_decay = 0.001f + val * 1.999f;
        break; // CC25 = Decay (0.001-2.0s)
      case 26:
        env_sustain = val;
        break; // CC26 = Sustain (0-1)
      case 27:
        env_release = 0.001f + val * 2.999f;
        break; // CC27 = Release (0.001-3.0s)

      // Pluck voice controls
      case 72:
        pluck_decay = 0.5f + val * 0.49f;
        break; // CC72 = Pluck Decay
      case 73:
        pluck_damp = val;
        break; // CC73 = Pluck Damp

      // String voice controls
      case 75:
        string_brightness = val;
        break; // CC75 = String Brightness
      case 77:
        string_damping = val;
        break; // CC77 = String Damping
      case 78:
        string_nonlinearity = val * 2.0f - 1.0f;
        break; // CC78 = String NonLinearity (-1 to 1)

      // StringVoice controls
      case 102:
        sv_brightness = val;
        break; // CC102 = SV Brightness
      case 103:
        sv_damping = val;
        break; // CC103 = SV Damping
      case 104:
        sv_structure = val;
        break; // CC104 = SV Structure
      case 105:
        sv_accent = val;
        break; // CC105 = SV Accent

      // === NEW: Portamento/Glide ===
      case 5:
        portamento_time = 0.01f + val * 0.99f;
        break; // CC5 = Portamento Time (0.01-1.0s)
      case 84:
        portamento_enabled = toggle;
        break; // CC84 = Portamento Enable

      // === NEW: Velocity Curve ===
      case 88:
        velocity_curve = (uint8_t)(val * 2.99f);
        break; // CC88 = Velocity Curve (0=Lin, 1=Exp, 2=Log)

      // === NEW: FM Synthesis controls ===
      case 89:
        fm_ratio = 0.5f + val * 7.5f;
        break; // CC89 = FM Ratio (0.5-8.0)
      case 90:
        fm_index = val * 10.0f;
        break; // CC90 = FM Index (0-10)

      // === NEW: Wavetable/VariableSaw controls ===
      case 94:
        varsaw_waveshape = val;
        break; // CC94 = Waveshape (0-1)
      case 96:
        varsaw_pw = val * 2.0f - 1.0f;
        break; // CC96 = Pulse Width (-1 to +1)

      // === NEW: Panic Button ===
      case 81:
        if (data2 > 64) {
          Panic();
        }
        break; // CC81 = Panic (>64 triggers)

      // Master Volume
      case 7:
        master_volume = 0.3f + val * 2.7f;
        break; // CC7 = Volume (0.3-3.0x)

      // Moog Filter controls
      case 22:
        moog_cutoff = val;
        break; // CC22 = Moog Cutoff
      case 23:
        moog_res = val;
        break; // CC23 = Moog Resonance

      // Enable/Disable toggles (>=64 = ON, <64 = OFF)
      case 64:
        svf_enabled = toggle;
        break; // CC64 = SVF Enable
      case 65:
        moog_enabled = toggle;
        break; // CC65 = Moog Enable
      case 66:
        chorus_enabled = toggle;
        break; // CC66 = Chorus Enable
      case 67:
        phaser_enabled = toggle;
        break; // CC67 = Phaser Enable
      case 68:
        reverb_enabled = toggle;
        break; // CC68 = Reverb Enable
      case 69:
        delay_enabled = toggle;
        break; // CC69 = Delay Enable

      // Effect mix levels
      case 91:
        reverb_mix = val;
        break; // CC91 = Reverb Mix
      case 93:
        chorus_mix = val;
        break; // CC93 = Chorus Mix
      case 95:
        phaser_mix = val;
        break; // CC95 = Phaser Mix

      // Delay controls
      case 85:
        delay_time = val;
        break; // CC85 = Delay Time
      case 86:
        delay_feedback = val;
        break; // CC86 = Delay Feedback
      case 87:
        delay_mix = val;
        break; // CC87 = Delay Mix

      // === NEW: Tremolo controls ===
      case 123:
        tremolo_enabled = toggle;
        break; // CC123 = Tremolo Enable
      case 124:
        tremolo_freq = 0.5f + val * 19.5f;
        break; // CC124 = Tremolo Freq (0.5-20 Hz)
      case 125:
        tremolo_depth = val;
        break; // CC125 = Tremolo Depth (0-1)

      // === NEW: Flanger controls ===
      case 17:
        flanger_enabled = toggle;
        break; // CC17 = Flanger Enable
      case 18:
        flanger_freq = 0.1f + val * 4.9f;
        break; // CC18 = Flanger LFO Freq (0.1-5 Hz)
      case 19:
        flanger_depth = val;
        break; // CC19 = Flanger Depth (0-1)
      case 20:
        flanger_feedback = val;
        break; // CC20 = Flanger Feedback (0-1)

      // === NEW: Overdrive control ===
      // === NEW: Overdrive control ===
      case 14:
        overdrive_enabled = toggle;
        break; // CC14 = Overdrive Enable
      case 15:
        overdrive_drive = val;
        break; // CC15 = Overdrive Drive (0-1)
      case 16:
        overdrive_level = val;
        break; // CC16 = Overdrive Level (0-1)

      // === NEW: Bitcrusher controls ===
      case 35:
        bitcrusher_enabled = toggle;
        break; // CC35 = Bitcrusher Enable
      case 36:
        bitcrush_amount = val;
        break; // CC36 = Bitcrush Amount (0-1)
      case 37:
        downsample_amount = val;
        break; // CC37 = Downsample Amount (0-1)

      // === NEW: Compressor controls ===
      case 40:
        compressor_enabled = toggle;
        break; // CC40 = Compressor Enable
      case 41:
        comp_thresh = -80.0f + val * 80.0f;
        break; // CC41 = Threshold (-80 to 0 dB)
      case 42:
        comp_ratio = 1.0f + val * 39.0f;
        break; // CC42 = Ratio (1-40)
      case 43:
        comp_attack = 0.001f + val * 0.5f; // Short attack range 1-500ms
        break;                             // CC43 = Attack
      case 44:
        comp_release = 0.001f + val * 2.0f; // Release range 1ms-2s
        break;                              // CC44 = Release

      // Arpeggiator controls
      case 108:
        arp_enabled = toggle;
        if (!toggle) {
          ArpReleaseNote();
          arp_note_count = 0;
        }
        break; // CC108 = Arp Enable
      case 109:
        arp_bpm = 60.0f + val * 180.0f; // 60-240 BPM (val is already 0-1)
        break;                          // CC109 = Arp BPM
      case 110:
        arp_gate = 0.1f + val * 0.9f; // 10-100% (val is already 0-1)
        break;                        // CC110 = Arp Gate
      case 111:
        arp_direction = data2 / 32; // 0-3 from CC value 0-127
        if (arp_direction > 3)
          arp_direction = 3;
        break; // CC111 = Arp Direction (0-3)
      case 112:
        arp_octaves = 1 + (data2 * 3 / 127); // 1-4 octaves from CC value
        if (arp_octaves > 4)
          arp_octaves = 4;
        break; // CC112 = Arp Octaves (1-4)

      // Chorus extended params
      case 113:
        chorus_lfo_freq = 0.1f + val * 4.9f;
        break; // CC113 = Chorus LFO Freq (0.1-5 Hz)
      case 114:
        chorus_lfo_depth = val;
        break; // CC114 = Chorus LFO Depth
      case 115:
        chorus_delay = val * 50.0f;
        break; // CC115 = Chorus Delay (0-50ms)
      case 116:
        chorus_feedback = val;
        break; // CC116 = Chorus Feedback

      // Phaser extended params
      case 117:
        phaser_lfo_freq = 0.1f + val * 4.9f;
        break; // CC117 = Phaser LFO Freq (0.1-5 Hz)
      case 118:
        phaser_lfo_depth = val;
        break; // CC118 = Phaser LFO Depth
      case 119:
        phaser_poles = 1 + (uint8_t)(val * 7 / 127);
        break; // CC119 = Phaser Poles (1-8)
      case 120:
        phaser_feedback = val;
        break; // CC120 = Phaser Feedback

      // Reverb extended params
      case 121:
        reverb_feedback = val;
        break; // CC121 = Reverb Decay/Feedback
      case 122:
        reverb_lpfreq = 200.0f + val * 15800.0f;
        break; // CC122 = Reverb LP Freq (200-16000 Hz)
      }
    }

    // Note On
    if (status == 0x90 && data2 > 0) {
      if (arp_enabled) {
        // Add note to arp buffer
        if (arp_note_count < 8) {
          bool was_empty = (arp_note_count == 0);
          arp_notes[arp_note_count++] = data1;
          // Sort notes for proper arpeggio
          for (int i = 0; i < arp_note_count - 1; i++) {
            for (int j = i + 1; j < arp_note_count; j++) {
              if (arp_notes[i] > arp_notes[j]) {
                uint8_t tmp = arp_notes[i];
                arp_notes[i] = arp_notes[j];
                arp_notes[j] = tmp;
              }
            }
          }
          // Reset arp state when first note is added
          if (was_empty) {
            arp_sample_counter = 0;
            arp_index = 0;
            arp_current_octave = 0;
            arp_going_up = true;
            arp_note_playing = false;
          }
        }
      } else {
        // Normal note on
        // Normal note on
        int v = AllocateVoice(data1);

        // Apply velocity curve
        float raw_velocity = (float)data2 / 127.0f;
        float final_velocity =
            applyVelocityCurve(raw_velocity, (uint8_t)velocity_curve);

        voices[v].NoteOn(data1, final_velocity);

        // LED on for note activity
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
      }
    }

    // Note Off
    if (status == 0x80 || (status == 0x90 && data2 == 0)) {
      if (arp_enabled) {
        // Remove note from arp buffer
        for (int i = 0; i < arp_note_count; i++) {
          if (arp_notes[i] == data1) {
            for (int j = i; j < arp_note_count - 1; j++) {
              arp_notes[j] = arp_notes[j + 1];
            }
            arp_note_count--;
            if (arp_index >= arp_note_count && arp_note_count > 0) {
              arp_index = 0;
            }
            break;
          }
        }
      } else {
        // Normal note off
        for (int v = 0; v < NUM_VOICES; v++) {
          if (voices[v].GetNote() == data1 && voices[v].gate) {
            voices[v].NoteOff();
            break;
          }
        }
        // Check if all voices are off, turn LED off
        bool any_active = false;
        for (int v = 0; v < NUM_VOICES; v++) {
          if (voices[v].gate) {
            any_active = true;
            break;
          }
        }
        if (!any_active) {
          HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
        }
      }
    }
  }
}

int main(void) {
  MPU_Config();
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  // Force VBUS Valid for OTG_FS BEFORE tusb_init()
  // This is CRITICAL - TinyUSB must see VBUS as valid during initialization
  // Required if the VBUS pin (PA9) is not connected or sensing is disabled
  USB2_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN; // Disable VBUS Sensing
  USB2_OTG_FS->GOTGCTL |=
      USB_OTG_GOTGCTL_BVALOEN |
      USB_OTG_GOTGCTL_BVALOVAL; // Force B-Peripheral Session Valid

  // TinyUSB Init
  tusb_init();

  // LED GPIO Init (PE3 - active low on WeAct)
  __HAL_RCC_GPIOE_CLK_ENABLE();
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &gpio);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET); // LED off initially

  AudioInit();

  // --- DaisySP Init ---

  // Initialize all voices
  for (int v = 0; v < NUM_VOICES; v++) {
    voices[v].Init(SAMPLE_RATE);
  }

  // LFO for Filter Sweep
  // lfw removed - LFO per voice

  // SVF Filter Init - REMOVED (Per-voice)

  // Moog Ladder Filter Init
  moog.Init(SAMPLE_RATE);
  moog.SetFreq(2000.0f);
  moog.SetRes(0.3f);

  // Chorus Init
  chorus.Init(SAMPLE_RATE);
  chorus.SetLfoFreq(0.5f);  // Slow LFO
  chorus.SetLfoDepth(0.3f); // Moderate depth
  chorus.SetDelayMs(10.0f); // 10ms delay
  chorus.SetFeedback(0.2f);

  // Phaser Init
  phaser.Init(SAMPLE_RATE);
  phaser.SetPoles(4);       // 4 allpass stages
  phaser.SetLfoFreq(0.3f);  // Slow sweep
  phaser.SetLfoDepth(0.8f); // Deep modulation
  phaser.SetFeedback(0.5f);

  // Delay Buffers Init (clear RAM_D2 buffers)
  memset(delay_buf_left, 0, sizeof(delay_buf_left));
  memset(delay_buf_right, 0, sizeof(delay_buf_right));

  // Reverb Init
  reverb.Init(SAMPLE_RATE);
  reverb.SetFeedback(0.9f); // Long tail
  reverb.SetLpFreq(8000.0f);

  // === NEW: Tremolo Init ===
  tremolo.Init(SAMPLE_RATE);
  tremolo.SetFreq(5.0f);
  tremolo.SetDepth(0.5f);
  tremolo.SetWaveform(Oscillator::WAVE_SIN);

  // === NEW: Flanger Init ===
  flanger.Init(SAMPLE_RATE);
  flanger.SetLfoFreq(0.5f);
  flanger.SetLfoDepth(0.5f);
  flanger.SetFeedback(0.5f);
  flanger.SetDelay(0.5f);

  // === NEW: Overdrive Init ===
  overdrive.Init();
  overdrive.SetDrive(0.5f);

  // === NEW: Bitcrusher Init ===
  bitcrusher.Init();
  bitcrusher.SetBitcrushFactor(0.0f);   // No effect
  bitcrusher.SetDownsampleFactor(0.0f); // No effect

  // === NEW: Compressor Init ===
  compressor.Init(SAMPLE_RATE);
  compressor.SetRatio(4.0f);
  compressor.SetThreshold(-20.0f);
  compressor.SetAttack(0.05f);
  compressor.SetRelease(0.2f);
  compressor.AutoMakeup(true); // Enable auto makeup gain

  // Sequencer Clock (Removed for MIDI)
  // clock.Init(4.0f, SAMPLE_RATE);

  AudioStart();

  while (1) {
    tud_task(); // TinyUSB Task

    if (req_fill_first_half) {
      req_fill_first_half = 0;
      AudioCallback(&audio_tx_buf[0], AUDIO_BLOCK_SIZE);
    }

    if (req_fill_second_half) {
      req_fill_second_half = 0;
      AudioCallback(&audio_tx_buf[2 * AUDIO_BLOCK_SIZE], AUDIO_BLOCK_SIZE);
    }
  }
}
