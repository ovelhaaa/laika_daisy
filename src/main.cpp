#include "audio_sai.h"
#include "daisysp-lgpl.h" // Includes ReverbSc, Pluck
#include "daisysp.h"
#include "stm32h7xx_hal.h"
#include "system.h"
#include <stdlib.h>
#include <tusb.h>

using namespace daisysp;

#define SAMPLE_RATE 48000
#define NUM_VOICES 4
#define PLUCK_BUF_SIZE 256

// Synthesis engine types (selectable via MIDI Program Change)
enum SynthMode {
  SYNTH_OSC = 0,         // Subtractive oscillator + ADSR
  SYNTH_PLUCK = 1,       // Simple Karplus-Strong pluck
  SYNTH_STRING = 2,      // Karplus-Strong with damping/brightness
  SYNTH_STRINGVOICE = 3, // Extended KS with accent/structure (Rings-style)
};

volatile SynthMode synth_mode = SYNTH_OSC;

// Voice structure for polyphony
struct Voice {
  // Synthesis engines
  Oscillator osc;
  Oscillator osc2; // Second oscillator for unison/detune
  Pluck pluck;
  float pluck_buf[PLUCK_BUF_SIZE];
  String string;
  StringVoice stringVoice;

  // Envelope and state
  Adsr env;
  bool gate;
  bool trig;
  uint8_t note;
  float velocity;
  uint32_t age;
};

Voice voices[NUM_VOICES];
uint32_t voice_counter = 0;

Oscillator lfw;  // Shared LFO for filter sweep
Svf filter;      // SVF (State Variable Filter)
MoogLadder moog; // Moog Ladder Filter

// Effects chain
Chorus chorus;
Phaser phaser;
ReverbSc reverb;

// Stereo Delay - buffers in RAM_D2 (~667ms max at 48kHz)
#define DELAY_MAX_SAMPLES 32000
__attribute__((section(".ram_d2"))) float delay_buf_left[DELAY_MAX_SAMPLES];
__attribute__((section(".ram_d2"))) float delay_buf_right[DELAY_MAX_SAMPLES];
size_t delay_write_ptr = 0;
float delay_frac = 0.0f;

// Enable/disable toggles (controlled via MIDI CC 64-69)
volatile bool svf_enabled = true;
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

// SVF Filter parameters
volatile float filter_cutoff = 0.5f;
volatile float filter_drive = 0.2f;
volatile float lfo_rate = 0.2f;

// Moog Filter parameters
volatile float moog_cutoff = 0.5f;
volatile float moog_res = 0.3f;

// Oscillator voice parameters
volatile uint8_t osc_waveform = 2; // Default: SAW
volatile float osc_pulsewidth = 0.5f;
volatile float osc2_detune =
    0.1f;                       // Detune in semitones (0-1 maps to 0-24 cents)
volatile float osc2_mix = 0.5f; // 0 = osc1 only, 1 = osc2 only, 0.5 = equal mix

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

// Arpeggiator: trigger a note with current settings
void ArpTriggerNote(uint8_t note) {
  int v = AllocateVoice(note);
  float freq = mtof(note);

  voices[v].osc.SetFreq(freq);
  voices[v].osc2.SetFreq(freq * powf(2.0f, osc2_detune / 12.0f));
  voices[v].pluck.SetFreq(freq);
  voices[v].string.SetFreq(freq);
  voices[v].stringVoice.SetFreq(freq);

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

void AudioCallback(int32_t *buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {

    // --- Arpeggiator Processing ---
    if (arp_enabled && arp_note_count > 0) {
      float step_duration = (60.0f / arp_bpm) * SAMPLE_RATE; // Samples per beat
      uint32_t step_samples = (uint32_t)step_duration;
      arp_gate_samples = (uint32_t)(step_duration * arp_gate);

      arp_sample_counter++;

      // Gate off
      if (arp_note_playing && arp_sample_counter >= arp_gate_samples) {
        ArpReleaseNote();
        arp_note_playing = false;
      }

      // Next step
      if (arp_sample_counter >= step_samples) {
        arp_sample_counter = 0;
        uint8_t note = ArpGetNextNote();
        if (note > 0) {
          ArpTriggerNote(note);
          arp_note_playing = true;
        }
      }
    }

    // --- LFO for Filter Sweep ---
    lfw.SetFreq(lfo_rate);
    float lfo_out = lfw.Process();
    // Base cutoff (200-8000 Hz) + LFO modulation
    float base_freq = 200.0f + (filter_cutoff * 7800.0f);
    float cutoff = base_freq + (lfo_out * base_freq * 0.5f);
    filter.SetFreq(cutoff);
    filter.SetDrive(filter_drive);

    // --- Mix all voices ---
    float mix = 0.0f;
    for (int v = 0; v < NUM_VOICES; v++) {
      float sig = 0.0f;

      switch (synth_mode) {
      case SYNTH_OSC: {
        // Dual oscillator with detune (unison/supersaw)
        voices[v].osc.SetWaveform(osc_waveform);
        voices[v].osc.SetPw(osc_pulsewidth);
        voices[v].osc2.SetWaveform(osc_waveform);
        voices[v].osc2.SetPw(osc_pulsewidth);

        float sig1 = voices[v].osc.Process();
        float sig2 = voices[v].osc2.Process();

        // Mix oscillators: 0 = osc1 only, 1 = osc2 only, 0.5 = equal
        sig = sig1 * (1.0f - osc2_mix) + sig2 * osc2_mix;
        sig *= voices[v].env.Process(voices[v].gate) * voices[v].velocity;
        break;
      }

      case SYNTH_PLUCK: {
        // Simple Karplus-Strong pluck - apply current settings
        voices[v].pluck.SetDecay(pluck_decay);
        voices[v].pluck.SetDamp(pluck_damp);
        float trig = voices[v].trig ? 1.0f : 0.0f;
        sig = voices[v].pluck.Process(trig);
        voices[v].trig = false;
        sig *= voices[v].velocity;
        break;
      }

      case SYNTH_STRING: {
        // Karplus-Strong String - apply current settings
        voices[v].string.SetBrightness(string_brightness);
        voices[v].string.SetDamping(string_damping);
        voices[v].string.SetNonLinearity(string_nonlinearity);
        float excitation = voices[v].gate
                               ? ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.3f
                               : 0.0f;
        sig = voices[v].string.Process(excitation);
        sig *= voices[v].velocity;
        break;
      }

      case SYNTH_STRINGVOICE:
        // Extended KS (Rings-style) - apply current settings
        voices[v].stringVoice.SetBrightness(sv_brightness);
        voices[v].stringVoice.SetDamping(sv_damping);
        voices[v].stringVoice.SetStructure(sv_structure);
        voices[v].stringVoice.SetAccent(sv_accent);
        if (voices[v].trig) {
          voices[v].stringVoice.Trig();
          voices[v].trig = false;
        }
        sig = voices[v].stringVoice.Process();
        sig *= voices[v].velocity;
        break;
      }

      mix += sig;
    }

    // Scale down to prevent clipping
    mix *= (1.0f / NUM_VOICES);

    // --- Filters (can stack both) ---
    float filtered = mix;

    // SVF Filter
    if (svf_enabled) {
      filter.Process(filtered);
      filtered = filter.Low();
    }

    // Moog Ladder Filter
    if (moog_enabled) {
      float moog_freq = 100.0f + (moog_cutoff * 7900.0f);
      moog.SetFreq(moog_freq);
      moog.SetRes(moog_res);
      filtered = moog.Process(filtered);
    }

    // --- Effects Chain (with enable toggles) ---
    float out_left = filtered;
    float out_right = filtered;

    // Chorus
    if (chorus_enabled) {
      chorus.SetLfoFreq(chorus_lfo_freq);
      chorus.SetLfoDepth(chorus_lfo_depth);
      chorus.SetDelayMs(chorus_delay);
      chorus.SetFeedback(chorus_feedback);
      chorus.Process(filtered);
      out_left = filtered * (1.0f - chorus_mix) + chorus.GetLeft() * chorus_mix;
      out_right =
          filtered * (1.0f - chorus_mix) + chorus.GetRight() * chorus_mix;
    }

    // Phaser
    if (phaser_enabled) {
      phaser.SetLfoFreq(phaser_lfo_freq);
      phaser.SetLfoDepth(phaser_lfo_depth);
      phaser.SetPoles(phaser_poles);
      phaser.SetFeedback(phaser_feedback);
      float ph_left = phaser.Process(out_left);
      float ph_right = phaser.Process(out_right);
      out_left = out_left * (1.0f - phaser_mix) + ph_left * phaser_mix;
      out_right = out_right * (1.0f - phaser_mix) + ph_right * phaser_mix;
    }

    // Delay (stereo with feedback)
    if (delay_enabled) {
      // Calculate delay in samples with interpolation
      float delay_samples = delay_time * (DELAY_MAX_SAMPLES - 1);
      size_t delay_int = (size_t)delay_samples;
      float frac = delay_samples - (float)delay_int;

      // Read with linear interpolation
      size_t read_pos =
          (delay_write_ptr + DELAY_MAX_SAMPLES - delay_int) % DELAY_MAX_SAMPLES;
      size_t read_pos2 = (read_pos + DELAY_MAX_SAMPLES - 1) % DELAY_MAX_SAMPLES;
      float del_l =
          delay_buf_left[read_pos] +
          (delay_buf_left[read_pos2] - delay_buf_left[read_pos]) * frac;
      float del_r =
          delay_buf_right[read_pos] +
          (delay_buf_right[read_pos2] - delay_buf_right[read_pos]) * frac;

      // Write with cross-feedback for stereo width
      delay_buf_left[delay_write_ptr] =
          out_left + del_r * delay_feedback * 0.7f;
      delay_buf_right[delay_write_ptr] =
          out_right + del_l * delay_feedback * 0.7f;
      delay_write_ptr = (delay_write_ptr + 1) % DELAY_MAX_SAMPLES;

      out_left = out_left * (1.0f - delay_mix) + del_l * delay_mix;
      out_right = out_right * (1.0f - delay_mix) + del_r * delay_mix;
    }

    // Reverb
    if (reverb_enabled) {
      float rev_fb = reverb_feedback; // Local copy to avoid volatile binding
      float rev_lp = reverb_lpfreq;
      reverb.SetFeedback(rev_fb);
      reverb.SetLpFreq(rev_lp);
      float rev_left = 0.0f, rev_right = 0.0f;
      reverb.Process(out_left, out_right, &rev_left, &rev_right);
      out_left = out_left * (1.0f - reverb_mix) + rev_left * reverb_mix;
      out_right = out_right * (1.0f - reverb_mix) + rev_right * reverb_mix;
    }

    // --- Output with Master Volume ---
    float amp = 0.1f * master_volume; // Base safe level * user volume
    int32_t sl = (int32_t)(out_left * amp * 2147483647.0f);
    int32_t sr = (int32_t)(out_right * amp * 2147483647.0f);

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
        filter.SetRes(val);
        break; // CC74 = SVF Resonance
      case 76:
        lfo_rate = 0.1f + val * 5.0f;
        break; // CC76 = LFO Rate

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
        int v = AllocateVoice(data1);
        float freq = mtof(data1);

        voices[v].osc.SetFreq(freq);
        voices[v].osc2.SetFreq(freq * powf(2.0f, osc2_detune / 12.0f));
        voices[v].pluck.SetFreq(freq);
        voices[v].string.SetFreq(freq);
        voices[v].stringVoice.SetFreq(freq);

        voices[v].velocity = (float)data2 / 127.0f;
        voices[v].note = data1;
        voices[v].gate = true;
        voices[v].trig = true;
        voices[v].age = ++voice_counter;
        voices[v].env.Retrigger(false);

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
          if (voices[v].note == data1 && voices[v].gate) {
            voices[v].gate = false;
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
    // Oscillator engine
    voices[v].osc.Init(SAMPLE_RATE);
    voices[v].osc.SetWaveform(Oscillator::WAVE_SAW);
    voices[v].osc.SetAmp(1.0f);

    // Second oscillator for unison/detune
    voices[v].osc2.Init(SAMPLE_RATE);
    voices[v].osc2.SetWaveform(Oscillator::WAVE_SAW);
    voices[v].osc2.SetAmp(1.0f);

    // Pluck/Karplus-Strong engine
    voices[v].pluck.Init(SAMPLE_RATE, voices[v].pluck_buf, PLUCK_BUF_SIZE,
                         PLUCK_MODE_RECURSIVE);
    voices[v].pluck.SetAmp(1.0f);
    voices[v].pluck.SetDecay(0.95f);
    voices[v].pluck.SetDamp(0.5f);

    // String engine (KS with damping/brightness)
    voices[v].string.Init(SAMPLE_RATE);
    voices[v].string.SetBrightness(0.5f);
    voices[v].string.SetDamping(0.7f);

    // StringVoice engine (Rings-style extended KS)
    voices[v].stringVoice.Init(SAMPLE_RATE);
    voices[v].stringVoice.SetBrightness(0.5f);
    voices[v].stringVoice.SetDamping(0.5f);
    voices[v].stringVoice.SetStructure(
        0.4f);                             // Mix of curved bridge & dispersion
    voices[v].stringVoice.SetAccent(0.8f); // Strong accent

    // ADSR Envelope
    voices[v].env.Init(SAMPLE_RATE);
    voices[v].env.SetAttackTime(0.01f);
    voices[v].env.SetDecayTime(0.2f);
    voices[v].env.SetSustainLevel(0.7f);
    voices[v].env.SetReleaseTime(0.3f);

    voices[v].gate = false;
    voices[v].trig = false;
    voices[v].note = 0;
    voices[v].velocity = 0.0f;
    voices[v].age = 0;
  }

  // LFO for Filter Sweep
  lfw.Init(SAMPLE_RATE);
  lfw.SetWaveform(Oscillator::WAVE_TRI);
  lfw.SetFreq(0.2f);
  lfw.SetAmp(1.0f);

  // SVF Filter Init
  filter.Init(SAMPLE_RATE);
  filter.SetRes(0.5f);
  filter.SetDrive(0.2f);

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
