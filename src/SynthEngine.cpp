#include "SynthEngine.h"
#include <string.h>
#include <stdlib.h>

void SynthEngine::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    synth_mode = SYNTH_OSC;
    master_volume = 1.0f;
    voice_counter = 0;
    delay_write_ptr = 0;
    delay_frac = 0.0f;

    // Default parameter values
    svf_enabled = true;
    moog_enabled = false;
    chorus_enabled = true;
    phaser_enabled = false;
    delay_enabled = false;
    reverb_enabled = true;

    chorus_mix = 0.3f;
    phaser_mix = 0.5f;
    reverb_mix = 0.4f;

    delay_time = 0.25f;
    delay_feedback = 0.4f;
    delay_mix = 0.3f;

    chorus_lfo_freq = 0.5f;
    chorus_lfo_depth = 0.3f;
    chorus_delay = 10.0f;
    chorus_feedback = 0.2f;

    phaser_lfo_freq = 0.3f;
    phaser_lfo_depth = 0.8f;
    phaser_poles = 4;
    phaser_feedback = 0.5f;

    reverb_feedback = 0.9f;
    reverb_lpfreq = 8000.0f;

    tremolo_enabled = false;
    tremolo_freq = 5.0f;
    tremolo_depth = 0.5f;

    flanger_enabled = false;
    flanger_freq = 0.5f;
    flanger_depth = 0.5f;
    flanger_feedback = 0.5f;

    overdrive_enabled = false;
    overdrive_drive = 0.5f;
    overdrive_level = 1.0f;

    bitcrusher_enabled = false;
    bitcrush_amount = 0.0f;
    downsample_amount = 0.0f;

    compressor_enabled = false;
    comp_ratio = 4.0f;
    comp_thresh = -20.0f;
    comp_attack = 0.05f;
    comp_release = 0.2f;

    filter_cutoff = 1000.0f;
    filter_res = 0.0f;
    filter_drive = 0.0f;
    filter_env_amt = 0.0f;
    filter_lfo_amt = 0.0f;

    lfo_rate = 1.0f;
    lfo_amp = 0.0f;

    moog_cutoff = 0.5f;
    moog_res = 0.3f;

    osc_waveform = 2; // SAW
    osc_pulsewidth = 0.5f;
    osc2_detune = 0.1f;
    osc2_mix = 0.5f;

    env_attack = 0.01f;
    env_decay = 0.2f;
    env_sustain = 0.7f;
    env_release = 0.3f;

    portamento_enabled = false;
    portamento_time = 0.1f;

    velocity_curve = 0;

    fm_ratio = 2.0f;
    fm_index = 1.0f;

    varsaw_waveshape = 0.5f;
    varsaw_pw = 0.0f;

    pluck_decay = 0.95f;
    pluck_damp = 0.5f;

    string_brightness = 0.5f;
    string_damping = 0.7f;
    string_nonlinearity = 0.0f;

    sv_brightness = 0.5f;
    sv_damping = 0.5f;
    sv_structure = 0.4f;
    sv_accent = 0.8f;

    arp_enabled = false;
    arp_bpm = 120.0f;
    arp_gate = 0.5f;
    arp_direction = 0;
    arp_octaves = 1;

    arp_note_count = 0;
    arp_index = 0;
    arp_current_octave = 0;
    arp_going_up = true;
    arp_current_note = 0;

    arp_sample_counter = 0;
    arp_gate_samples = 0;
    arp_note_playing = false;

    arp_phase_accumulator = 0;
    arp_phase_increment = 0;


    // Inicialização das Vozes
    for (int v = 0; v < NUM_VOICES; v++) {
        voices[v].Init(sample_rate);
    }

    // Inicialização dos Efeitos
    moog.Init(sample_rate);
    moog.SetFreq(2000.0f);
    moog.SetRes(0.3f);

    chorus.Init(sample_rate);
    chorus.SetLfoFreq(0.5f);
    chorus.SetLfoDepth(0.3f);
    chorus.SetDelayMs(10.0f);
    chorus.SetFeedback(0.2f);

    phaser.Init(sample_rate);
    phaser.SetPoles(4);
    phaser.SetLfoFreq(0.3f);
    phaser.SetLfoDepth(0.8f);
    phaser.SetFeedback(0.5f);

    // Zera os buffers da RAM_D2 com segurança
    memset(delay_buf_left, 0, sizeof(delay_buf_left));
    memset(delay_buf_right, 0, sizeof(delay_buf_right));

    reverb.Init(sample_rate);
    reverb.SetFeedback(0.9f);
    reverb.SetLpFreq(8000.0f);

    tremolo.Init(sample_rate);
    tremolo.SetFreq(5.0f);
    tremolo.SetDepth(0.5f);
    tremolo.SetWaveform(daisysp::Oscillator::WAVE_SIN);

    flanger.Init(sample_rate);
    flanger.SetLfoFreq(0.5f);
    flanger.SetLfoDepth(0.5f);
    flanger.SetFeedback(0.5f);
    flanger.SetDelay(0.5f);

    overdrive.Init();
    overdrive.SetDrive(0.5f);

    bitcrusher.Init();
    bitcrusher.SetBitcrushFactor(0.0f);
    bitcrusher.SetDownsampleFactor(0.0f);

    compressor.Init(sample_rate);
    compressor.SetRatio(4.0f);
    compressor.SetThreshold(-20.0f);
    compressor.SetAttack(0.05f);
    compressor.SetRelease(0.2f);
    compressor.AutoMakeup(true);
}

int SynthEngine::AllocateVoice(uint8_t note) {
    int free_voice = -1;
    int oldest_voice = 0;
    uint32_t min_age = 0xFFFFFFFF;

    for (int i = 0; i < NUM_VOICES; i++) {
        if (voices[i].note == note && voices[i].gate) {
            return i;
        }
        if (!voices[i].gate && !voices[i].env.IsRunning()) {
            free_voice = i;
        }
        if (voices[i].age < min_age) {
            min_age = voices[i].age;
            oldest_voice = i;
        }
    }

    return (free_voice >= 0) ? free_voice : oldest_voice;
}

float SynthEngine::applyVelocityCurve(float velocity, uint8_t curve) {
    switch (curve) {
    case 0: return velocity;
    case 1: return velocity * velocity;
    case 2: return sqrtf(velocity);
    default: return velocity;
    }
}

void SynthEngine::ArpTriggerNote(uint8_t note) {
    int v = AllocateVoice(note);
    float freq = daisysp::mtof(note);

    voices[v].osc.SetFreq(freq);
    voices[v].osc2.SetFreq(freq * powf(2.0f, osc2_detune / 12.0f));
    voices[v].pluck.SetFreq(freq);
    voices[v].ks.SetFreq(freq);
    voices[v].stringVoice.SetFreq(freq);
    voices[v].fm.SetFrequency(freq);
    voices[v].varsaw.SetFreq(freq);

    voices[v].target_freq = freq;
    if (!portamento_enabled) {
        voices[v].current_freq = freq;
    }

    voices[v].velocity = 0.8f;
    voices[v].note = note;
    voices[v].gate = true;
    voices[v].trig = true;
    voices[v].age = ++voice_counter;
    voices[v].env.Retrigger(false);

    arp_current_note = note;
}

void SynthEngine::ArpReleaseNote() {
    for (int v = 0; v < NUM_VOICES; v++) {
        if (voices[v].note == arp_current_note && voices[v].gate) {
            voices[v].gate = false;
        }
    }
}

uint8_t SynthEngine::ArpGetNextNote() {
    if (arp_note_count == 0) return 0;

    uint8_t base_note = arp_notes[arp_index];
    uint8_t note = base_note + (arp_current_octave * 12);

    switch (arp_direction) {
    case 0:
        arp_index++;
        if (arp_index >= arp_note_count) {
            arp_index = 0;
            arp_current_octave++;
            if (arp_current_octave >= arp_octaves) arp_current_octave = 0;
        }
        break;
    case 1:
        arp_index--;
        if (arp_index < 0) {
            arp_index = arp_note_count - 1;
            arp_current_octave--;
            if (arp_current_octave < 0) arp_current_octave = arp_octaves - 1;
        }
        break;
    case 2:
        if (arp_going_up) {
            arp_index++;
            if (arp_index >= arp_note_count) {
                if (arp_current_octave < arp_octaves - 1) {
                    arp_current_octave++;
                    arp_index = 0;
                } else {
                    arp_going_up = false;
                    arp_index = arp_note_count - 2;
                    if (arp_index < 0) arp_index = 0;
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
                    if (arp_index >= arp_note_count) arp_index = 0;
                }
            }
        }
        break;
    case 3:
        arp_index = rand() % arp_note_count;
        arp_current_octave = rand() % arp_octaves;
        break;
    }

    return note;
}

void SynthEngine::ArpUpdateTiming() {
    float beats_per_sample = arp_bpm / (60.0f * sample_rate_);
    arp_phase_increment = (uint32_t)(beats_per_sample * 4294967296.0f);
}

void SynthEngine::Panic() {
    for (int i = 0; i < NUM_VOICES; i++) {
        voices[i].gate = false;
        voices[i].trig = false;
        voices[i].velocity = 0.0f;
        voices[i].env.SetSustainLevel(0.0f);
        voices[i].age = 0;
    }
    arp_note_count = 0;
    arp_note_playing = false;
}

void SynthEngine::Process(float* out_l, float* out_r, size_t size) {
    // Synth mode and master
    SynthMode l_synth_mode = synth_mode;
    float l_master_volume = master_volume;

    // Oscillator parameters
    uint8_t l_osc_waveform = osc_waveform;
    float l_osc_pulsewidth = osc_pulsewidth;
    float l_osc2_detune = osc2_detune;
    float l_osc2_mix = osc2_mix;

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
    bool l_arp_enabled = arp_enabled;
    float l_arp_bpm = arp_bpm;
    float l_arp_gate = arp_gate;

    // Portamento parameters
    bool l_portamento_enabled = portamento_enabled;
    float l_portamento_time = portamento_time;

    // Update effect parameters ONCE per block
    if (l_moog_enabled) {
        float moog_freq = 100.0f + (l_moog_cutoff * 7900.0f);
        moog.SetFreq(moog_freq);
        moog.SetRes(l_moog_res);
    }

    if (l_chorus_enabled) {
        chorus.SetLfoFreq(chorus_lfo_freq);
        chorus.SetLfoDepth(chorus_lfo_depth);
        chorus.SetDelayMs(chorus_delay);
        chorus.SetFeedback(chorus_feedback);
    }

    if (l_phaser_enabled) {
        phaser.SetLfoFreq(phaser_lfo_freq);
        phaser.SetLfoDepth(phaser_lfo_depth);
        phaser.SetPoles(phaser_poles);
        phaser.SetFeedback(phaser_feedback);
    }

    if (l_reverb_enabled) {
        reverb.SetFeedback(l_reverb_feedback);
        reverb.SetLpFreq(l_reverb_lpfreq);
    }

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

    // Update Voice Parameters
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
    p.fm_ratio = fm_ratio;
    p.fm_index = fm_index;
    p.varsaw_waveshape = varsaw_waveshape;
    p.varsaw_pw = varsaw_pw;
    p.portamento_enabled = l_portamento_enabled;
    p.portamento_time = l_portamento_time;
    p.velocity_curve = (float)velocity_curve;
    p.filter_cutoff = 20.0f + (l_filter_cutoff * l_filter_cutoff * 12000.0f);
    p.filter_res = l_filter_res;
    p.filter_env_amt = l_filter_env_amt;
    p.filter_lfo_amt = l_filter_lfo_amt;
    p.lfo_rate = l_lfo_rate * 20.0f;
    p.lfo_amp = l_lfo_amp;

    for (int v = 0; v < NUM_VOICES; v++) {
        voices[v].UpdateParams(p, sample_rate_);
    }

    uint32_t l_arp_phase_inc = (uint32_t)((l_arp_bpm / (60.0f * sample_rate_)) * 4294967296.0f);
    uint32_t l_arp_gate_threshold = (uint32_t)(l_arp_gate * 4294967296.0f);

    float delay_samples = l_delay_time * (DELAY_MAX_SAMPLES - 1);
    size_t delay_int = (size_t)delay_samples;
    float delay_frac_local = delay_samples - (float)delay_int;

    for (size_t i = 0; i < size; i++) {
        if (l_arp_enabled && arp_note_count > 0) {
            uint32_t prev_phase = arp_phase_accumulator;
            arp_phase_accumulator += l_arp_phase_inc;

            if (arp_phase_accumulator < prev_phase) {
                uint8_t note = ArpGetNextNote();
                if (note > 0) {
                    ArpTriggerNote(note);
                    arp_note_playing = true;
                }
            }

            if (arp_note_playing && arp_phase_accumulator >= l_arp_gate_threshold) {
                ArpReleaseNote();
                arp_note_playing = false;
            }
        }

        float mix = 0.0f;
        for (int v = 0; v < NUM_VOICES; v++) {
            mix += voices[v].Process();
        }

        mix *= (1.0f / NUM_VOICES);

        if (l_overdrive_enabled) {
            mix = overdrive.Process(mix);
            mix *= l_overdrive_level;
        }

        if (l_bitcr_enabled) {
            mix = bitcrusher.Process(mix);
        }

        float filtered = mix;

        if (l_moog_enabled) {
            filtered = moog.Process(filtered);
        }

        if (l_tremolo_enabled) {
            filtered = tremolo.Process(filtered);
        }

        if (l_flanger_enabled) {
            filtered = flanger.Process(filtered);
        }

        float out_left = filtered;
        float out_right = filtered;

        if (l_chorus_enabled) {
            chorus.Process(filtered);
            out_left = filtered * (1.0f - l_chorus_mix) + chorus.GetLeft() * l_chorus_mix;
            out_right = filtered * (1.0f - l_chorus_mix) + chorus.GetRight() * l_chorus_mix;
        }

        if (l_phaser_enabled) {
            float ph_left = phaser.Process(out_left);
            float ph_right = phaser.Process(out_right);
            out_left = out_left * (1.0f - l_phaser_mix) + ph_left * l_phaser_mix;
            out_right = out_right * (1.0f - l_phaser_mix) + ph_right * l_phaser_mix;
        }

        if (l_delay_enabled) {
            size_t read_pos = (delay_write_ptr + DELAY_MAX_SAMPLES - delay_int) % DELAY_MAX_SAMPLES;
            size_t read_pos2 = (read_pos + DELAY_MAX_SAMPLES - 1) % DELAY_MAX_SAMPLES;
            float del_l = delay_buf_left[read_pos] + (delay_buf_left[read_pos2] - delay_buf_left[read_pos]) * delay_frac_local;
            float del_r = delay_buf_right[read_pos] + (delay_buf_right[read_pos2] - delay_buf_right[read_pos]) * delay_frac_local;

            delay_buf_left[delay_write_ptr] = out_left + del_r * l_delay_feedback * 0.7f;
            delay_buf_right[delay_write_ptr] = out_right + del_l * l_delay_feedback * 0.7f;
            delay_write_ptr = (delay_write_ptr + 1) % DELAY_MAX_SAMPLES;

            out_left = out_left * (1.0f - l_delay_mix) + del_l * l_delay_mix;
            out_right = out_right * (1.0f - l_delay_mix) + del_r * l_delay_mix;
        }

        if (l_reverb_enabled) {
            float rev_left = 0.0f, rev_right = 0.0f;
            reverb.Process(out_left, out_right, &rev_left, &rev_right);
            out_left = out_left * (1.0f - l_reverb_mix) + rev_left * l_reverb_mix;
            out_right = out_right * (1.0f - l_reverb_mix) + rev_right * l_reverb_mix;
        }

        if (l_comp_enabled) {
            out_left = compressor.Process(out_left);
            out_right = compressor.Process(out_right);
        }

        float amp = 0.1f * l_master_volume;
        out_l[i] = softClip(out_left * amp * 1.5f);
        out_r[i] = softClip(out_right * amp * 1.5f);
    }
}

void SynthEngine::ProcessMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    if (status == 0xC0) {
        switch (data1) {
        case 0: synth_mode = SYNTH_OSC; break;
        case 1: synth_mode = SYNTH_PLUCK; break;
        case 2: synth_mode = SYNTH_STRING; break;
        case 3: synth_mode = SYNTH_STRINGVOICE; break;
        case 4: synth_mode = SYNTH_FM; break;
        case 5: synth_mode = SYNTH_WAVETABLE; break;
        default: synth_mode = SYNTH_OSC; break;
        }
    }

    if (status == 0xB0) {
        float val = (float)data2 / 127.0f;
        bool toggle = data2 >= 64;

        switch (data1) {
        case 1: filter_cutoff = val; break;
        case 21: filter_drive = val; break;
        case 74: filter_res = val; break;
        case 76: lfo_rate = 0.1f + val * 19.9f; break;

        case 29: lfo_amp = val; break;
        case 30: filter_env_amt = val * 2.0f - 1.0f; break;
        case 31: filter_lfo_amt = val; break;

        case 70: osc_waveform = (uint8_t)(val * 7.99f); break;
        case 71: osc_pulsewidth = val; break;
        case 79: osc2_detune = val * 0.5f; break;
        case 80: osc2_mix = val; break;

        case 24: env_attack = 0.001f + val * 1.999f; break;
        case 25: env_decay = 0.001f + val * 1.999f; break;
        case 26: env_sustain = val; break;
        case 27: env_release = 0.001f + val * 2.999f; break;

        case 72: pluck_decay = 0.5f + val * 0.49f; break;
        case 73: pluck_damp = val; break;

        case 75: string_brightness = val; break;
        case 77: string_damping = val; break;
        case 78: string_nonlinearity = val * 2.0f - 1.0f; break;

        case 102: sv_brightness = val; break;
        case 103: sv_damping = val; break;
        case 104: sv_structure = val; break;
        case 105: sv_accent = val; break;

        case 5: portamento_time = 0.01f + val * 0.99f; break;
        case 84: portamento_enabled = toggle; break;

        case 88: velocity_curve = (uint8_t)(val * 2.99f); break;

        case 89: fm_ratio = 0.5f + val * 7.5f; break;
        case 90: fm_index = val * 10.0f; break;

        case 94: varsaw_waveshape = val; break;
        case 96: varsaw_pw = val * 2.0f - 1.0f; break;

        case 81: if (data2 > 64) Panic(); break;

        case 7: master_volume = 0.3f + val * 2.7f; break;

        case 22: moog_cutoff = val; break;
        case 23: moog_res = val; break;

        case 64: svf_enabled = toggle; break;
        case 65: moog_enabled = toggle; break;
        case 66: chorus_enabled = toggle; break;
        case 67: phaser_enabled = toggle; break;
        case 68: reverb_enabled = toggle; break;
        case 69: delay_enabled = toggle; break;

        case 91: reverb_mix = val; break;
        case 93: chorus_mix = val; break;
        case 95: phaser_mix = val; break;

        case 85: delay_time = val; break;
        case 86: delay_feedback = val; break;
        case 87: delay_mix = val; break;

        case 123: tremolo_enabled = toggle; break;
        case 124: tremolo_freq = 0.5f + val * 19.5f; break;
        case 125: tremolo_depth = val; break;

        case 17: flanger_enabled = toggle; break;
        case 18: flanger_freq = 0.1f + val * 4.9f; break;
        case 19: flanger_depth = val; break;
        case 20: flanger_feedback = val; break;

        case 14: overdrive_enabled = toggle; break;
        case 15: overdrive_drive = val; break;
        case 16: overdrive_level = val; break;

        case 35: bitcrusher_enabled = toggle; break;
        case 36: bitcrush_amount = val; break;
        case 37: downsample_amount = val; break;

        case 40: compressor_enabled = toggle; break;
        case 41: comp_thresh = -80.0f + val * 80.0f; break;
        case 42: comp_ratio = 1.0f + val * 39.0f; break;
        case 43: comp_attack = 0.001f + val * 0.5f; break;
        case 44: comp_release = 0.001f + val * 2.0f; break;

        case 108:
            arp_enabled = toggle;
            if (!toggle) {
                ArpReleaseNote();
                arp_note_count = 0;
            }
            break;
        case 109: arp_bpm = 60.0f + val * 180.0f; break;
        case 110: arp_gate = 0.1f + val * 0.9f; break;
        case 111:
            arp_direction = data2 / 32;
            if (arp_direction > 3) arp_direction = 3;
            break;
        case 112:
            arp_octaves = 1 + (data2 * 3 / 127);
            if (arp_octaves > 4) arp_octaves = 4;
            break;

        case 113: chorus_lfo_freq = 0.1f + val * 4.9f; break;
        case 114: chorus_lfo_depth = val; break;
        case 115: chorus_delay = val * 50.0f; break;
        case 116: chorus_feedback = val; break;

        case 117: phaser_lfo_freq = 0.1f + val * 4.9f; break;
        case 118: phaser_lfo_depth = val; break;
        case 119: phaser_poles = 1 + (uint8_t)(val * 7 / 127); break;
        case 120: phaser_feedback = val; break;

        case 121: reverb_feedback = val; break;
        case 122: reverb_lpfreq = 200.0f + val * 15800.0f; break;
        }
    }

    if (status == 0x90 && data2 > 0) {
        if (arp_enabled) {
            if (arp_note_count < 8) {
                bool was_empty = (arp_note_count == 0);
                arp_notes[arp_note_count++] = data1;
                for (int i = 0; i < arp_note_count - 1; i++) {
                    for (int j = i + 1; j < arp_note_count; j++) {
                        if (arp_notes[i] > arp_notes[j]) {
                            uint8_t tmp = arp_notes[i];
                            arp_notes[i] = arp_notes[j];
                            arp_notes[j] = tmp;
                        }
                    }
                }
                if (was_empty) {
                    arp_sample_counter = 0;
                    arp_index = 0;
                    arp_current_octave = 0;
                    arp_going_up = true;
                    arp_note_playing = false;
                }
            }
        } else {
            int v = AllocateVoice(data1);
            float raw_velocity = (float)data2 / 127.0f;
            float final_velocity = applyVelocityCurve(raw_velocity, velocity_curve);
            voices[v].NoteOn(data1, final_velocity);

            // Se ainda quisermos acender um LED no hardware, isso deve ser tratado fora
            // ou através de um callback no SynthEngine.
            // Por enquanto, o LED é gerenciado pela ausência desta lógica aqui ou removido
            // para ser completamente desacoplado (como solicitado).
        }
    }

    if (status == 0x80 || (status == 0x90 && data2 == 0)) {
        if (arp_enabled) {
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
            for (int v = 0; v < NUM_VOICES; v++) {
                if (voices[v].GetNote() == data1 && voices[v].gate) {
                    voices[v].NoteOff();
                    break;
                }
            }
        }
    }
}
