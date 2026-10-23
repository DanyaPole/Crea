#include <clap/clap.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <atomic>
#include <numbers>
#include <vector>

#include <imgui.h>

#include "gui_platform.h"

// ============================================================================
// TECTONIC EXPERIMENTAL SYNTH v5.0 — IMGUI OBSERVATORY & DSP
// ============================================================================

enum ParamIds : clap_id {
    PARAM_ENGINE_CONTINUUM = 0,
    PARAM_MORPH_X = 1,
    PARAM_CHAOS_Y = 2,
    PARAM_ENTANGLEMENT = 3,
    PARAM_PLASMA_LENSING = 4,
    PARAM_GRAVITY = 5,
    PARAM_CUTOFF = 6,
    PARAM_RESONANCE = 7,
    PARAM_FILTER_MORPH = 8,
    PARAM_ATTACK = 9,
    PARAM_DECAY = 10,
    PARAM_SPACE = 11,
    PARAM_GAIN = 12,
    PARAM_COUNT
};

static constexpr int MAX_VOICES = 16;
static constexpr int CA_SIZE = 32;
static constexpr int WG_SIZE = 2048;

struct FastRNG {
    uint32_t state = 2463534242u;
    float nextFloat() {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return (static_cast<float>(state) / static_cast<float>(0xFFFFFFFFu)) * 2.0f - 1.0f;
    }
};

struct PolySVF {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    void reset() { ic1eq = 0.0f; ic2eq = 0.0f; }
    float process(float v0, float cutoff_hz, float res, float morph, float sr) {
        float nyquist = sr * 0.47f;
        float fc = std::clamp(cutoff_hz, 25.0f, nyquist);
        float g = std::tan(std::numbers::pi_v<float> * (fc / sr));
        float k = 2.0f - 1.93f * std::clamp(res, 0.0f, 0.98f);
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1; float a3 = g * a2;
        float v3 = v0 - ic2eq;
        float v1 = a1 * ic1eq + a2 * v3;
        float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq; ic2eq = 2.0f * v2 - ic2eq;
        if (!std::isfinite(ic1eq) || !std::isfinite(ic2eq)) { reset(); return 0.0f; }
        float lp = v2; float bp = v1; float hp = v0 - k * v1 - v2; float notch = lp + hp;
        if (morph < 0.3333f) return lp * (1.0f - morph * 3.0f) + bp * (morph * 3.0f);
        if (morph < 0.6666f) return bp * (1.0f - (morph - 0.3333f) * 3.0f) + hp * ((morph - 0.3333f) * 3.0f);
        return hp * (1.0f - (morph - 0.6666f) * 3.0f) + notch * ((morph - 0.6666f) * 3.0f);
    }
};

struct QuantumEntanglementField {
    float field_x = 0.0f; float field_y = 0.0f; float plasma_energy = 0.0f; float centroid_freq = 261.6f; int active_count = 0;
    void reset() { field_x = 0.0f; field_y = 0.0f; plasma_energy = 0.0f; centroid_freq = 261.6f; active_count = 0; }
};

struct VoiceVisualState {
    bool active = false; float phase = 0.0f; float env = 0.0f; float engine_pos = 0.0f; float x = 0.0f; float y = 0.0f; float pan = 0.5f; int key = 60;
};

struct TectonicTelemetry {
    std::array<VoiceVisualState, MAX_VOICES> voices{};
    float field_x = 0.0f; float field_y = 0.0f; float plasma_energy = 0.0f; int active_count = 0;
};

struct Voice {
    bool active = false; bool held = false; int32_t note_id = -1; int16_t port_index = 0; int16_t channel = 0; int16_t key = 0; uint64_t age = 0;
    float base_freq = 440.0f; float current_freq = 440.0f; float velocity = 0.8f; float phase = 0.0f; float sub_phase = 0.0f; float env = 0.0f; float env_stage = 0.0f; float last_out = 0.0f;
    float mod_engine = 0.0f; float mod_morph_x = 0.0f; float mod_chaos_y = 0.0f; float mod_entangle = 0.0f; float mod_plasma = 0.0f; float mod_gravity = 0.0f; float mod_cutoff = 0.0f; float mod_res = 0.0f; float mod_fmorph = 0.0f;
    float expr_pitch = 0.0f; float expr_timbre = 0.0f; float expr_pressure = 0.0f; float expr_brightness = 0.0f; float expr_volume = 1.0f; float expr_pan = 0.5f;
    std::array<float, CA_SIZE> cells{}; std::array<float, CA_SIZE> next_cells{}; float ca_clock = 0.0f;
    std::array<float, 6> swarm_phases{};
    float lx = 0.1f, ly = 0.0f, lz = 0.0f; float wg_last = 0.0f;
    std::array<float, WG_SIZE> delay_line{}; int write_pos = 0; float ap_state = 0.0f; float lp_state = 0.0f;
    float soliton_u = 0.5f; float soliton_v = 0.0f; float soliton_fold = 0.0f;
    PolySVF svf{}; float dc_x1 = 0.0f; float dc_y1 = 0.0f;

    void trigger(int32_t id, int16_t port, int16_t ch, int16_t k, float vel, uint64_t current_age, FastRNG& rng, bool steal_smooth) {
        active = true; held = true; note_id = id; port_index = port; channel = ch; key = k; age = current_age;
        base_freq = 440.0f * std::pow(2.0f, (static_cast<float>(k) - 69.0f) / 12.0f); current_freq = base_freq;
        velocity = std::clamp(vel, 0.05f, 1.0f); env_stage = 0.0f;
        if (!steal_smooth) { env = 0.0f; phase = 0.0f; sub_phase = 0.0f; last_out = 0.0f; svf.reset(); dc_x1 = 0.0f; dc_y1 = 0.0f; }
        mod_engine = 0.0f; mod_morph_x = 0.0f; mod_chaos_y = 0.0f; mod_entangle = 0.0f; mod_plasma = 0.0f; mod_gravity = 0.0f; mod_cutoff = 0.0f; mod_res = 0.0f; mod_fmorph = 0.0f;
        expr_pitch = 0.0f; expr_timbre = 0.0f; expr_pressure = 0.0f; expr_brightness = 0.0f; expr_volume = 1.0f; expr_pan = 0.5f;
        for (int i = 0; i < CA_SIZE; ++i) cells[i] = rng.nextFloat();
        for (int i = 0; i < 6; ++i) swarm_phases[i] = (rng.nextFloat() * 0.5f + 0.5f) * 2.0f * std::numbers::pi_v<float>;
        lx = 0.1f + rng.nextFloat() * 0.05f; ly = 0.0f; lz = 0.0f; wg_last = 0.0f;
        for (int i = 0; i < WG_SIZE; ++i) {
            float window = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / 128.0f);
            delay_line[i] = (i < 128) ? (rng.nextFloat() * window * velocity) : 0.0f;
        }
        write_pos = 0; ap_state = 0.0f; lp_state = 0.0f; soliton_u = 0.4f + rng.nextFloat() * 0.1f; soliton_v = 0.0f; soliton_fold = 0.0f;
    }
};

struct StereoSpaceDiffuser {
    static constexpr int DL1 = 1493; static constexpr int DL2 = 2179; static constexpr int DL3 = 3187; static constexpr int DL4 = 4421;
    std::array<float, DL1> buf1{}; std::array<float, DL2> buf2{}; std::array<float, DL3> buf3{}; std::array<float, DL4> buf4{};
    int i1 = 0, i2 = 0, i3 = 0, i4 = 0; float lfo_phase = 0.0f; float dampL = 0.0f, dampR = 0.0f;
    void reset() { buf1.fill(0.0f); buf2.fill(0.0f); buf3.fill(0.0f); buf4.fill(0.0f); i1 = i2 = i3 = i4 = 0; dampL = dampR = 0.0f; }
    void process(float inL, float inR, float space_amt, float plasma_energy, float sr, float& outL, float& outR) {
        if (space_amt <= 0.001f) { outL = inL; outR = inR; return; }
        lfo_phase += (0.42f + plasma_energy * 0.8f) / sr; if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;
        float mod = std::sin(lfo_phase * 2.0f * std::numbers::pi_v<float>);
        float d1 = buf1[i1]; float v1 = inL + d1 * 0.5f; buf1[i1] = v1; float apL = d1 - v1 * 0.5f; if (++i1 >= DL1) i1 = 0;
        float d2 = buf2[i2]; float v2 = inR + d2 * 0.5f; buf2[i2] = v2; float apR = d2 - v2 * 0.5f; if (++i2 >= DL2) i2 = 0;
        float fb = 0.36f + space_amt * 0.53f; float rL = buf3[i3]; float rR = buf4[i4];
        float shimL = rL + 0.08f * space_amt * (2.0f * rL * rL - 0.15f); float shimR = rR + 0.08f * space_amt * (2.0f * rR * rR - 0.15f);
        dampL = dampL * 0.34f + shimL * 0.66f; dampR = dampR * 0.34f + shimR * 0.66f;
        buf3[i3] = std::tanh(apL + dampR * fb + mod * 0.0025f);
        buf4[i4] = std::tanh(apR - dampL * fb - mod * 0.0025f);
        if (++i3 >= DL3) i3 = 0; if (++i4 >= DL4) i4 = 0;
        float wet = space_amt * 0.58f;
        outL = inL * (1.0f - wet * 0.32f) + dampL * wet; outR = inR * (1.0f - wet * 0.32f) + dampR * wet;
    }
};

struct TectonicPlugin {
    const clap_host_t* host = nullptr;
    float sample_rate = 48000.0f; uint64_t voice_counter = 0; FastRNG rng;
    std::array<std::atomic<float>, PARAM_COUNT> params{0.35f, 0.40f, 0.44f, 0.45f, 0.30f, 0.45f, 0.74f, 0.26f, 0.0f, 0.015f, 1.8f, 0.38f, 0.76f};
    std::array<float, PARAM_COUNT> global_mod{};
    std::array<Voice, MAX_VOICES> voices{};
    QuantumEntanglementField qfield{};
    StereoSpaceDiffuser diffuser{};
    TectonicTelemetry telemetry{};
    ImGuiContext* imgui_context = nullptr;
    void* platform_gui = nullptr;
    std::atomic<uint32_t> gui_pending_values{0};
    std::atomic<uint32_t> gui_pending_gesture_begin{0};
    std::atomic<uint32_t> gui_pending_gesture_end{0};

    static void sendNoteEnd(const clap_output_events_t* out_events, const Voice& v, uint32_t sample_time) {
        if (!out_events || !out_events->try_push) return;
        clap_event_note_t ev{}; ev.header.size = sizeof(clap_event_note_t); ev.header.time = sample_time;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID; ev.header.type = CLAP_EVENT_NOTE_END;
        ev.note_id = v.note_id; ev.port_index = v.port_index; ev.channel = v.channel; ev.key = v.key;
        out_events->try_push(out_events, &ev.header);
    }

    void triggerNote(int32_t note_id, int16_t port, int16_t ch, int16_t key, float vel, uint32_t time, const clap_output_events_t* out_events) {
        Voice* target = nullptr; bool stolen = false;
        for (auto& v : voices) { if (!v.active) { target = &v; break; } }
        if (!target) {
            float min_env = 999.0f;
            for (auto& v : voices) { if (!v.held && v.env < min_env) { min_env = v.env; target = &v; } }
        }
        if (!target) {
            uint64_t oldest = UINT64_MAX;
            for (auto& v : voices) { if (v.age < oldest) { oldest = v.age; target = &v; } }
        }
        if (target->active) { sendNoteEnd(out_events, *target, time); stolen = true; }
        target->trigger(note_id, port, ch, key, vel, ++voice_counter, rng, stolen);
    }

    void handleEvent(const clap_event_header_t* hdr, const clap_output_events_t* out_events) {
        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (hdr->type) {
            case CLAP_EVENT_NOTE_ON: {
                auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                triggerNote(ev->note_id, ev->port_index, ev->channel, ev->key, static_cast<float>(ev->velocity), hdr->time, out_events);
                break;
            }
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE: {
                auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                for (auto& v : voices) {
                    if (v.active && ((ev->note_id != -1 && v.note_id == ev->note_id) ||
                        (ev->note_id == -1 && (ev->key == -1 || v.key == ev->key) && (ev->channel == -1 || v.channel == ev->channel)))) {
                        v.held = false;
                        if (hdr->type == CLAP_EVENT_NOTE_CHOKE) { v.active = false; v.env = 0.0f; sendNoteEnd(out_events, v, hdr->time); }
                    }
                }
                break;
            }
            case CLAP_EVENT_PARAM_VALUE: {
                auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
                if (ev->param_id < PARAM_COUNT) params[ev->param_id].store(static_cast<float>(ev->value), std::memory_order_relaxed);
                break;
            }
            case CLAP_EVENT_PARAM_MOD: {
                auto* ev = reinterpret_cast<const clap_event_param_mod_t*>(hdr);
                float amt = static_cast<float>(ev->amount);
                if (ev->note_id != -1 || ev->key != -1 || ev->channel != -1) {
                    for (auto& v : voices) {
                        if (!v.active) continue;
                        bool match = (ev->note_id != -1 && v.note_id == ev->note_id) ||
                                     (ev->note_id == -1 && (ev->key == -1 || v.key == ev->key) && (ev->channel == -1 || v.channel == ev->channel));
                        if (match) {
                            switch (ev->param_id) {
                                case PARAM_ENGINE_CONTINUUM: v.mod_engine = amt; break;
                                case PARAM_MORPH_X:          v.mod_morph_x = amt; break;
                                case PARAM_CHAOS_Y:          v.mod_chaos_y = amt; break;
                                case PARAM_ENTANGLEMENT:     v.mod_entangle = amt; break;
                                case PARAM_PLASMA_LENSING:   v.mod_plasma = amt; break;
                                case PARAM_GRAVITY:          v.mod_gravity = amt; break;
                                case PARAM_CUTOFF:           v.mod_cutoff = amt; break;
                                case PARAM_RESONANCE:        v.mod_res = amt; break;
                                case PARAM_FILTER_MORPH:     v.mod_fmorph = amt; break;
                                default: break;
                            }
                        }
                    }
                } else if (ev->param_id < PARAM_COUNT) {
                    global_mod[ev->param_id] = amt;
                }
                break;
            }
        }
    }

    static float evalChladniSmooth(float phase, float x_scaled, float y_scaled) {
        constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
        float n0 = 1.0f + std::floor(x_scaled); float fx = x_scaled - std::floor(x_scaled);
        float m0 = 1.0f + std::floor(y_scaled); float fy = y_scaled - std::floor(y_scaled);
        float cx = std::cos((n0) * phase * two_pi) * (1.0f - fx) + std::cos((n0 + 1.0f) * phase * two_pi) * fx;
        float cy = std::cos((m0) * phase * two_pi) * (1.0f - fy) + std::cos((m0 + 1.0f) * phase * two_pi) * fy;
        return 0.5f + 0.485f * (cx * cy);
    }

    float evalSingleEngine(Voice& v, int eng_id, float dt, float freq, float x, float y, float entangle, float plasma, float fundamental) {
        constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
        float peer_field_x = (qfield.active_count > 1) ? (qfield.field_x - v.last_out * 0.5f) / static_cast<float>(qfield.active_count - 1) : (v.last_out * 0.35f);
        float peer_field_y = qfield.field_y;

        if (eng_id == 0) {
            v.ca_clock += dt * (0.35f + y * 3.8f + entangle * std::abs(peer_field_x) * 1.5f);
            if (v.ca_clock >= 1.0f) {
                v.ca_clock -= 1.0f;
                float rule_gain = 1.65f + x * 2.4f + entangle * peer_field_y * 0.55f;
                for (int i = 0; i < CA_SIZE; ++i) {
                    float left = v.cells[(i - 1 + CA_SIZE) % CA_SIZE]; float center = v.cells[i]; float right = v.cells[(i + 1) % CA_SIZE];
                    v.next_cells[i] = std::sin(((left + center + right) * 0.333333f + (left - 2.0f * center + right) * 0.25f * y) * rule_gain * std::numbers::pi_v<float> + 0.08f * std::sin(two_pi * (static_cast<float>(i)/CA_SIZE + v.phase)));
                }
                v.cells = v.next_cells;
            }
            float scan = evalChladniSmooth(v.phase, x * 4.99f, y * 4.99f);
            float pos = scan * static_cast<float>(CA_SIZE - 1);
            int idx0 = static_cast<int>(pos); int idx1 = (idx0 + 1) % CA_SIZE; float frac = pos - static_cast<float>(idx0);
            return v.cells[idx0] * (1.0f - frac) + v.cells[idx1] * frac;
        } else if (eng_id == 1) {
            constexpr std::array<float, 6> ratios = {1.0f, 1.004f, 1.4983f, 1.997f, 2.618f, 3.992f};
            float coupling_K = y * 0.22f; std::array<float, 6> next_phases = v.swarm_phases; float sum_out = 0.0f;
            float global_angle = std::atan2(peer_field_y, peer_field_x + 1e-6f);
            for (int i = 0; i < 6; ++i) {
                float d_theta = two_pi * dt * (1.0f + (ratios[i] - 1.0f) * x);
                float interaction = 0.0f;
                for (int j = 0; j < 6; ++j) interaction += std::sin(v.swarm_phases[j] - v.swarm_phases[i]);
                d_theta += (coupling_K / 6.0f) * interaction + entangle * 0.045f * std::sin(global_angle - v.swarm_phases[i]);
                next_phases[i] += d_theta; if (next_phases[i] >= two_pi) next_phases[i] -= two_pi;
                sum_out += std::sin(next_phases[i]);
            }
            v.swarm_phases = next_phases;
            return std::tanh(sum_out * 0.48f);
        } else if (eng_id == 2) {
            float l_dt = std::min(dt * 0.18f * (0.4f + y * 2.2f), 0.015f);
            float dx1 = 10.0f * (v.ly - v.lx); float dy1 = v.lx * ((24.0f + x * 14.0f) - v.lz) - v.ly; float dz1 = v.lx * v.ly - (8.0f/3.0f) * v.lz;
            v.lx = 30.0f * std::tanh((v.lx + l_dt * dx1) / 30.0f); v.ly = 30.0f * std::tanh((v.ly + l_dt * dy1) / 30.0f); v.lz = std::clamp(v.lz + l_dt * dz1, 0.0f, 55.0f);
            float folded = std::sin((fundamental + 0.35f * std::sin(v.phase * 4.0f * std::numbers::pi_v<float>)) * (1.0f + v.lx * 0.08f * y + x * 3.8f) * std::numbers::pi_v<float>);
            v.wg_last = std::tanh(folded + v.wg_last * (0.25f + y * 0.52f));
            return v.wg_last;
        } else if (eng_id == 3) {
            float exact_period = std::clamp(sample_rate / freq, 4.0f, static_cast<float>(WG_SIZE - 4));
            int delay_int = static_cast<int>(exact_period); float frac = exact_period - static_cast<float>(delay_int);
            float delayed = v.delay_line[(v.write_pos - delay_int + WG_SIZE) % WG_SIZE] * (1.0f - frac) + v.delay_line[(v.write_pos - delay_int - 1 + WG_SIZE) % WG_SIZE] * frac;
            float ap_out = - (0.45f * (x - 0.5f)) * delayed + v.ap_state; v.ap_state = delayed + (0.45f * (x - 0.5f)) * ap_out;
            v.lp_state = v.lp_state * (0.12f - x * 0.08f) + std::sin((ap_out + std::sin(v.phase * two_pi * 2.0f) * 0.1f) * (1.05f + y * 0.85f)) * (0.88f + x * 0.08f);
            v.delay_line[v.write_pos] = std::tanh(v.lp_state * 0.996f); v.write_pos = (v.write_pos + 1) % WG_SIZE;
            return v.lp_state;
        } else {
            float sech = 1.0f / std::cosh((v.phase - 0.5f) * (1.2f + x * 8.5f) * 2.5f);
            float omega = two_pi * dt * (1.0f + std::floor(x * 4.0f) + y * 2.0f);
            v.soliton_u = std::tanh(v.soliton_u + v.soliton_v * 0.35f);
            v.soliton_v = std::tanh(v.soliton_v + (-omega * v.soliton_u * (1.0f + plasma * 0.8f * v.soliton_u * v.soliton_u) + omega * (sech * sech * 2.0f - 0.65f)) * 0.35f);
            v.soliton_fold = 0.6f * std::sin((v.soliton_u * (1.2f + y * 2.5f) + (sech * sech * 2.0f - 0.65f)) * std::numbers::pi_v<float>) + 0.4f * v.soliton_fold;
            return v.soliton_fold;
        }
    }

    void renderVoice(Voice& v, float attack_sec, float decay_sec, uint32_t sample_time, const clap_output_events_t* out_events, float& outL, float& outR, float& sum_fx, float& sum_fy, float& sum_energy, float& sum_freq, int voice_idx) {
        if (!v.active) return;
        float eng_pos = std::clamp(params[PARAM_ENGINE_CONTINUUM].load(std::memory_order_relaxed) + global_mod[PARAM_ENGINE_CONTINUUM] + v.mod_engine * 4.0f, 0.0f, 4.0f);
        float x = std::clamp(params[PARAM_MORPH_X].load(std::memory_order_relaxed) + global_mod[PARAM_MORPH_X] + v.mod_morph_x + v.expr_timbre * 0.5f, 0.0f, 1.0f);
        float y = std::clamp(params[PARAM_CHAOS_Y].load(std::memory_order_relaxed) + global_mod[PARAM_CHAOS_Y] + v.mod_chaos_y + v.expr_pressure * 0.6f, 0.0f, 1.0f);
        float entangle = std::clamp(params[PARAM_ENTANGLEMENT].load(std::memory_order_relaxed) + global_mod[PARAM_ENTANGLEMENT] + v.mod_entangle, 0.0f, 1.0f);
        float plasma = std::clamp(params[PARAM_PLASMA_LENSING].load(std::memory_order_relaxed) + global_mod[PARAM_PLASMA_LENSING] + v.mod_plasma, 0.0f, 1.0f);
        float grav = std::clamp(params[PARAM_GRAVITY].load(std::memory_order_relaxed) + global_mod[PARAM_GRAVITY] + v.mod_gravity, 0.0f, 1.0f);
        float cutoff_norm = std::clamp(params[PARAM_CUTOFF].load(std::memory_order_relaxed) + global_mod[PARAM_CUTOFF] + v.mod_cutoff + v.expr_brightness * 0.4f + v.velocity * 0.15f, 0.0f, 1.0f);
        float res_norm = std::clamp(params[PARAM_RESONANCE].load(std::memory_order_relaxed) + global_mod[PARAM_RESONANCE] + v.mod_res, 0.0f, 0.96f);
        float fmorph = std::clamp(params[PARAM_FILTER_MORPH].load(std::memory_order_relaxed) + global_mod[PARAM_FILTER_MORPH] + v.mod_fmorph, 0.0f, 1.0f);

        float target_freq = v.base_freq * std::pow(2.0f, v.expr_pitch / 12.0f);
        if (qfield.active_count > 1 && plasma > 0.01f) {
            float ratio = target_freq / std::max(20.0f, qfield.centroid_freq);
            float harmonic_freq = qfield.centroid_freq * std::max(0.25f, std::round(ratio * 4.0f) * 0.25f);
            target_freq *= (1.0f + (std::clamp(harmonic_freq / target_freq, 0.97f, 1.03f) - 1.0f) * plasma * 0.22f);
        }
        v.current_freq = std::clamp(target_freq, 16.0f, sample_rate * 0.45f);
        float dt = v.current_freq / sample_rate;

        v.phase += dt; if (v.phase >= 1.0f) v.phase -= 1.0f;
        v.sub_phase += dt * 0.5f; if (v.sub_phase >= 1.0f) v.sub_phase -= 1.0f;

        constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
        float fundamental = std::sin(v.phase * two_pi);
        float sub_octave = std::sin(v.sub_phase * two_pi);
        float anchor = fundamental * 0.72f + sub_octave * 0.28f;

        int eng0 = std::clamp(static_cast<int>(std::floor(eng_pos)), 0, 4);
        int eng1 = std::min(eng0 + 1, 4);
        float eng_frac = eng_pos - static_cast<float>(eng0);

        float sig0 = evalSingleEngine(v, eng0, dt, v.current_freq, x, y, entangle, plasma, fundamental);
        float raw_sig = sig0;
        if (eng1 != eng0 && eng_frac > 0.001f) {
            float sig1 = evalSingleEngine(v, eng1, dt, v.current_freq, x, y, entangle, plasma, fundamental);
            raw_sig = sig0 * std::cos(eng_frac * 0.5f * std::numbers::pi_v<float>) + sig1 * std::sin(eng_frac * 0.5f * std::numbers::pi_v<float>);
        }

        if (qfield.active_count > 1 && plasma > 0.005f) {
            float peer_sig = (qfield.field_x - v.last_out * 0.5f) / static_cast<float>(qfield.active_count - 1);
            float plasma_ring = std::sin((raw_sig + peer_sig * (0.8f + y)) * std::numbers::pi_v<float>);
            raw_sig = raw_sig * (1.0f - plasma * 0.42f) + plasma_ring * (plasma * 0.48f);
        }

        float dc_out = raw_sig - v.dc_x1 + 0.995f * v.dc_y1;
        v.dc_x1 = raw_sig; v.dc_y1 = dc_out;

        float mixed = dc_out * (1.0f - grav * 0.60f) + anchor * (grav * 0.85f);
        float cutoff_hz = 40.0f * std::pow(450.0f, std::clamp(cutoff_norm + entangle * qfield.plasma_energy * 0.18f, 0.0f, 1.0f));
        float filtered = v.svf.process(mixed, cutoff_hz, res_norm, fmorph, sample_rate);

        if (v.env_stage < 0.5f) {
            v.env += 1.0f / (std::max(0.0015f, attack_sec) * sample_rate);
            if (v.env >= 1.0f) { v.env = 1.0f; v.env_stage = 1.0f; }
        } else if (!v.held) {
            v.env -= (1.0f / (std::max(0.04f, decay_sec) * sample_rate)) * (0.35f + 0.65f * v.env);
            if (v.env <= 0.0001f) {
                v.env = 0.0f; v.active = false; v.last_out = 0.0f;
                sendNoteEnd(out_events, v, sample_time);
                return;
            }
        }

        float final_sig = filtered * v.env * v.velocity * v.expr_volume;
        v.last_out = final_sig;

        sum_fx += final_sig; sum_fy += std::cos(v.phase * two_pi) * v.env;
        sum_energy += std::abs(final_sig); sum_freq += v.base_freq;

        if (voice_idx >= 0 && voice_idx < MAX_VOICES) {
            telemetry.voices[voice_idx] = {true, v.phase, v.env, eng_pos, x, y, std::clamp(0.5f + 0.22f * std::sin(static_cast<float>(v.key) * 1.3f), 0.05f, 0.95f), v.key};
        }

        float pan = std::clamp(0.5f + 0.22f * std::sin(static_cast<float>(v.key) * 1.3f) + (v.expr_pan - 0.5f), 0.05f, 0.95f);
        outL += final_sig * std::cos(pan * 0.5f * std::numbers::pi_v<float>);
        outR += final_sig * std::sin(pan * 0.5f * std::numbers::pi_v<float>);
    }
};

static uint32_t CLAP_ABI paramsCount(const clap_plugin_t*) { return PARAM_COUNT; }
static bool CLAP_ABI paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) {
    if (index >= PARAM_COUNT) return false;
    std::memset(info, 0, sizeof(clap_param_info_t));
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE | CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID;
    const char* names[] = {
        "4D Engine Continuum", "X: Structure & Morph", "Y: Chaos & Turbulence",
        "Quantum Entanglement", "Plasma Bus & Lensing", "Gravity (Sub-Anchor)",
        "Filter Cutoff", "Filter Resonance", "Filter Morph",
        "Attack (sec)", "Release / Tail (sec)", "Abyssal Shimmer Space", "Master Gain"
    };
    std::snprintf(info->name, sizeof(info->name), "%s", names[index]);
    info->min_value = (index == PARAM_ATTACK) ? 0.002 : ((index == PARAM_DECAY) ? 0.05 : 0.0);
    info->max_value = (index == PARAM_ENGINE_CONTINUUM) ? 4.0 : ((index == PARAM_ATTACK) ? 3.0 : ((index == PARAM_DECAY) ? 8.0 : 1.0));
    constexpr double defaults[PARAM_COUNT] = {0.35, 0.40, 0.44, 0.45, 0.30, 0.45, 0.74, 0.26, 0.0, 0.015, 1.8, 0.38, 0.76};
    info->default_value = defaults[index];
    return true;
}
static bool CLAP_ABI paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value) {
    if (id >= PARAM_COUNT || !value) return false;
    *value = static_cast<TectonicPlugin*>(plugin->plugin_data)->params[id].load(std::memory_order_relaxed);
    return true;
}
static bool CLAP_ABI paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size) {
    if (id == 0) std::snprintf(display, size, "%.2f [Engine]", value);
    else std::snprintf(display, size, "%.2f", value);
    return true;
}
static bool CLAP_ABI paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value) {
    *value = std::atof(display);
    return true;
}
struct HostParamsExtension {
    void (CLAP_ABI *rescan)(const clap_host_t* host, uint32_t flags);
    void (CLAP_ABI *clear)(const clap_host_t* host, clap_id param_id, uint32_t flags);
    void (CLAP_ABI *request_flush)(const clap_host_t* host);
};

static bool pushParamGesture(const clap_output_events_t* out, clap_id id, uint16_t type) {
    if (!out || !out->try_push) return false;
    clap_event_param_gesture_t event{};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type;
    event.param_id = id;
    return out->try_push(out, &event.header);
}

static void flushGuiParameters(TectonicPlugin* plug, const clap_output_events_t* out) {
    const uint32_t begins = plug->gui_pending_gesture_begin.exchange(0, std::memory_order_acq_rel);
    const uint32_t values = plug->gui_pending_values.exchange(0, std::memory_order_acq_rel);
    const uint32_t ends = plug->gui_pending_gesture_end.exchange(0, std::memory_order_acq_rel);
    if (!(begins | values | ends)) return;

    for (clap_id id = 0; id < PARAM_COUNT; ++id) {
        const uint32_t bit = 1u << id;
        if ((begins & bit) && !pushParamGesture(out, id, CLAP_EVENT_PARAM_GESTURE_BEGIN)) {
            plug->gui_pending_gesture_begin.fetch_or(begins & ~((bit << 1) - 1u), std::memory_order_release);
            plug->gui_pending_values.fetch_or(values & ~((bit << 1) - 1u), std::memory_order_release);
            plug->gui_pending_gesture_end.fetch_or(ends & ~((bit << 1) - 1u), std::memory_order_release);
            break;
        }

        if (values & bit) {
            clap_event_param_value_t event{};
            event.header.size = sizeof(event);
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.param_id = id;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = plug->params[id].load(std::memory_order_relaxed);
            if (!out || !out->try_push || !out->try_push(out, &event.header)) {
                plug->gui_pending_values.fetch_or(values & ~((bit << 1) - 1u), std::memory_order_release);
                plug->gui_pending_gesture_end.fetch_or(ends & ~((bit << 1) - 1u), std::memory_order_release);
                break;
            }
        }

        if ((ends & bit) && !pushParamGesture(out, id, CLAP_EVENT_PARAM_GESTURE_END)) {
            plug->gui_pending_gesture_end.fetch_or(ends & ~((bit << 1) - 1u), std::memory_order_release);
            break;
        }
    }
}

static void CLAP_ABI paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (in && in->size && in->get)
        for (uint32_t i = 0; i < in->size(in); ++i) plug->handleEvent(in->get(in, i), out);
    flushGuiParameters(plug, out);
}
static const clap_plugin_params_t s_params_ext = { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

static bool CLAP_ABI stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    uint32_t magic = 0x54454334;
    if (stream->write(stream, &magic, sizeof(magic)) != sizeof(magic)) return false;
    for (size_t i = 0; i < PARAM_COUNT; ++i) {
        const float value = plug->params[i].load(std::memory_order_relaxed);
        if (stream->write(stream, &value, sizeof(value)) != sizeof(value)) return false;
    }
    return true;
}
static bool CLAP_ABI stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    uint32_t magic = 0;
    if (stream->read(stream, &magic, sizeof(magic)) != sizeof(magic)) return false;
    if (magic != 0x54454334) return false;
    std::array<float, PARAM_COUNT> values{};
    for (size_t i = 0; i < PARAM_COUNT; ++i) {
        if (stream->read(stream, &values[i], sizeof(values[i])) != sizeof(values[i])) return false;
    }
    for (size_t i = 0; i < PARAM_COUNT; ++i)
        plug->params[i].store(values[i], std::memory_order_relaxed);
    return true;
}
static const clap_plugin_state_t s_state_ext = { stateSave, stateLoad };

extern "C" void tectonicDrawGui(void* plugin_data) {
    auto* plug = static_cast<TectonicPlugin*>(plugin_data);
    if (!plug) return;

    uint32_t gesture_begins = 0;
    uint32_t gesture_ends = 0;
    bool params_changed = false;
    const auto slider = [&](const char* label, ParamIds id, float minimum, float maximum) {
        float value = plug->params[id].load(std::memory_order_relaxed);
        if (ImGui::SliderFloat(label, &value, minimum, maximum)) {
            plug->params[id].store(value, std::memory_order_relaxed);
            plug->gui_pending_values.fetch_or(1u << id, std::memory_order_release);
            params_changed = true;
        }
        if (ImGui::IsItemActivated())
            gesture_begins |= 1u << id;
        if (ImGui::IsItemDeactivated())
            gesture_ends |= 1u << id;
    };

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(960.0f, 600.0f), ImGuiCond_Always);
    ImGui::Begin("Tectonic Observatory", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.25f, 0.92f, 0.88f, 1.0f), "TECTONIC  /  EXPERIMENTAL SYNTH");
    ImGui::SameLine();
    ImGui::TextDisabled("VERSION 5.0.0  |  16-VOICE FIELD");
    ImGui::Separator();

    ImGui::Columns(2, "controls", false);
    ImGui::Text("ENGINE CONTINUUM");
    slider("Engine", PARAM_ENGINE_CONTINUUM, 0.0f, 4.0f);
    ImGui::Text("STRUCTURE / MORPH");
    const ImVec2 pad_size(420.0f, 185.0f);
    const ImVec2 pad_origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("Continuum XY", pad_size);
    if (ImGui::IsItemActivated()) {
        gesture_begins |= (1u << PARAM_MORPH_X) | (1u << PARAM_CHAOS_Y);
    }
    const ImVec2 pad_end(pad_origin.x + pad_size.x, pad_origin.y + pad_size.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pad_origin, pad_end, IM_COL32(8, 21, 35, 255), 8.0f);
    draw->AddRect(pad_origin, pad_end, IM_COL32(29, 93, 112, 255), 8.0f);
    for (int i = 1; i < 8; ++i) {
        const float x = pad_origin.x + pad_size.x * static_cast<float>(i) / 8.0f;
        draw->AddLine(ImVec2(x, pad_origin.y), ImVec2(x, pad_end.y), IM_COL32(22, 57, 70, 180));
    }
    for (int i = 1; i < 4; ++i) {
        const float y = pad_origin.y + pad_size.y * static_cast<float>(i) / 4.0f;
        draw->AddLine(ImVec2(pad_origin.x, y), ImVec2(pad_end.x, y), IM_COL32(22, 57, 70, 180));
    }
    if (ImGui::IsItemActive()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float x = std::clamp((mouse.x - pad_origin.x) / pad_size.x, 0.0f, 1.0f);
        const float y = 1.0f - std::clamp((mouse.y - pad_origin.y) / pad_size.y, 0.0f, 1.0f);
        const float old_x = plug->params[PARAM_MORPH_X].load(std::memory_order_relaxed);
        const float old_y = plug->params[PARAM_CHAOS_Y].load(std::memory_order_relaxed);
        if (x != old_x || y != old_y) {
            plug->params[PARAM_MORPH_X].store(x, std::memory_order_relaxed);
            plug->params[PARAM_CHAOS_Y].store(y, std::memory_order_relaxed);
            plug->gui_pending_values.fetch_or((1u << PARAM_MORPH_X) | (1u << PARAM_CHAOS_Y), std::memory_order_release);
            params_changed = true;
        }
    }
    if (ImGui::IsItemDeactivated())
        gesture_ends |= (1u << PARAM_MORPH_X) | (1u << PARAM_CHAOS_Y);
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.0f);
    const ImVec2 marker(
        pad_origin.x + plug->params[PARAM_MORPH_X].load(std::memory_order_relaxed) * pad_size.x,
        pad_origin.y + (1.0f - plug->params[PARAM_CHAOS_Y].load(std::memory_order_relaxed)) * pad_size.y);
    draw->AddCircle(marker, 12.0f + pulse * 4.0f, IM_COL32(38, 240, 218, 255), 32, 2.0f);
    draw->AddCircleFilled(marker, 4.0f, IM_COL32(227, 255, 250, 255));
    ImGui::Text("X  %.2f", plug->params[PARAM_MORPH_X].load(std::memory_order_relaxed));
    ImGui::SameLine(130.0f);
    ImGui::Text("Y  %.2f", plug->params[PARAM_CHAOS_Y].load(std::memory_order_relaxed));
    ImGui::NextColumn();

    ImGui::Text("QUANTUM FIELD");
    slider("Entanglement", PARAM_ENTANGLEMENT, 0.0f, 1.0f);
    slider("Plasma Lensing", PARAM_PLASMA_LENSING, 0.0f, 1.0f);
    slider("Gravity", PARAM_GRAVITY, 0.0f, 1.0f);
    ImGui::Spacing();
    ImGui::Text("FILTER / ENVELOPE");
    slider("Cutoff", PARAM_CUTOFF, 0.0f, 1.0f);
    slider("Resonance", PARAM_RESONANCE, 0.0f, 0.96f);
    slider("Filter Morph", PARAM_FILTER_MORPH, 0.0f, 1.0f);
    slider("Attack", PARAM_ATTACK, 0.002f, 3.0f);
    slider("Release", PARAM_DECAY, 0.05f, 8.0f);
    ImGui::Spacing();
    ImGui::Text("SPACE / OUTPUT");
    slider("Space", PARAM_SPACE, 0.0f, 1.0f);
    slider("Master Gain", PARAM_GAIN, 0.0f, 1.0f);
    ImGui::Columns(1);
    ImGui::End();

    if (gesture_begins)
        plug->gui_pending_gesture_begin.fetch_or(gesture_begins, std::memory_order_release);
    if (gesture_ends)
        plug->gui_pending_gesture_end.fetch_or(gesture_ends, std::memory_order_release);

    if ((params_changed || gesture_begins || gesture_ends) && plug->host && plug->host->get_extension) {
        auto* host_params = static_cast<const HostParamsExtension*>(plug->host->get_extension(plug->host, CLAP_EXT_PARAMS));
        if (host_params && host_params->request_flush)
            host_params->request_flush(plug->host);
    }
}

static bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*, const char* api, bool is_floating) {
    if (is_floating || !api) return false;
#if defined(_WIN32)
    return !std::strcmp(api, CLAP_WINDOW_API_WIN32);
#elif defined(__APPLE__)
    return !std::strcmp(api, CLAP_WINDOW_API_NS_VIEW);
#else
    return false;
#endif
}
static bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* is_floating) {
    if (!api || !is_floating) return false;
    *is_floating = false;
#if defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
    *api = CLAP_WINDOW_API_NS_VIEW;
#else
    return false;
#endif
    return true;
}
static bool CLAP_ABI guiCreate(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    if (!api || is_floating || !guiIsApiSupported(plugin, api, is_floating)) return false;
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (plug->imgui_context) return false;
    plug->imgui_context = ImGui::CreateContext();
    if (!plug->imgui_context) return false;
    ImGui::SetCurrentContext(plug->imgui_context);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.035f, 0.065f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.07f, 0.12f, 0.17f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.08f, 0.27f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.37f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.15f, 0.82f, 0.75f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.36f, 1.0f, 0.87f, 1.0f);
    return true;
}
static void CLAP_ABI guiDestroy(const clap_plugin_t* plugin) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (plug->platform_gui) {
        tectonicGuiDestroy(plug->platform_gui, plug->imgui_context);
        plug->platform_gui = nullptr;
    }
    if (plug->imgui_context) {
        ImGui::SetCurrentContext(plug->imgui_context);
        ImGui::DestroyContext(plug->imgui_context);
        plug->imgui_context = nullptr;
    }
}
static bool CLAP_ABI guiSetScale(const clap_plugin_t*, double) { return true; }
static bool CLAP_ABI guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { *w = 960; *h = 600; return true; }
static bool CLAP_ABI guiCanResize(const clap_plugin_t*) { return false; }
static bool CLAP_ABI guiSetSize(const clap_plugin_t*, uint32_t, uint32_t) { return true; }
static bool CLAP_ABI guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
#if defined(_WIN32)
    if (!window || !window->win32) return false;
    return tectonicGuiAttach(plug, window->win32, plug->imgui_context, &plug->platform_gui);
#elif defined(__APPLE__)
    if (!window || !window->ns_view) return false;
    return tectonicGuiAttach(plug, window->ns_view, plug->imgui_context, &plug->platform_gui);
#else
    (void)window;
    return false;
#endif
}
static bool CLAP_ABI guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return true; }
static void CLAP_ABI guiSuggestTitle(const clap_plugin_t*, const char*) {}
static bool CLAP_ABI guiShow(const clap_plugin_t* plugin) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (!plug->platform_gui) return false;
    tectonicGuiShow(plug->platform_gui, true);
    return true;
}
static bool CLAP_ABI guiHide(const clap_plugin_t* plugin) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (!plug->platform_gui) return false;
    tectonicGuiShow(plug->platform_gui, false);
    return true;
}
static const clap_plugin_gui_t s_gui_ext = { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

static uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t*, bool is_input) { return is_input ? 0 : 1; }
static bool CLAP_ABI audioPortsGet(const clap_plugin_t*, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    if (is_input || index > 0) return false;
    info->id = 0; std::snprintf(info->name, sizeof(info->name), "Main Out");
    info->channel_count = 2; info->flags = CLAP_AUDIO_PORT_IS_MAIN; info->port_type = CLAP_PORT_STEREO; info->in_place_pair = CLAP_INVALID_ID; return true;
}
static const clap_plugin_audio_ports_t s_audio_ports_ext = { audioPortsCount, audioPortsGet };

static uint32_t CLAP_ABI notePortsCount(const clap_plugin_t*, bool is_input) { return is_input ? 1 : 0; }
static bool CLAP_ABI notePortsGet(const clap_plugin_t*, uint32_t index, bool is_input, clap_note_port_info_t* info) {
    if (!is_input || index > 0) return false;
    info->id = 0; info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP; std::snprintf(info->name, sizeof(info->name), "Note In"); return true;
}
static const clap_plugin_note_ports_t s_note_ports_ext = { notePortsCount, notePortsGet };
static bool CLAP_ABI voiceInfoGet(const clap_plugin_t*, clap_voice_info_t* info) { info->voice_count = MAX_VOICES; info->voice_capacity = MAX_VOICES; info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES; return true; }
static const clap_plugin_voice_info_t s_voice_info_ext = { voiceInfoGet };

static clap_process_status CLAP_ABI pluginProcess(const clap_plugin_t* plugin, const clap_process_t* process) {
    auto* plug = static_cast<TectonicPlugin*>(plugin->plugin_data);
    if (process->audio_outputs_count == 0 || !process->audio_outputs[0].data32) return CLAP_PROCESS_SLEEP;
    float* outL = process->audio_outputs[0].data32[0]; float* outR = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : outL;
    uint32_t ev_index = 0; const uint32_t num_events = process->in_events ? process->in_events->size(process->in_events) : 0;

    for (uint32_t s = 0; s < process->frames_count; ++s) {
        while (ev_index < num_events) {
            const clap_event_header_t* hdr = process->in_events->get(process->in_events, ev_index);
            if (hdr->time > s) break; plug->handleEvent(hdr, process->out_events); ev_index++;
        }
        float voicesL = 0.0f; float voicesR = 0.0f; float sum_fx = 0.0f, sum_fy = 0.0f, sum_energy = 0.0f, sum_freq = 0.0f; int active_cnt = 0;
        for (int v = 0; v < MAX_VOICES; ++v) {
            if (plug->voices[v].active) {
                active_cnt++;
                plug->renderVoice(plug->voices[v], std::clamp(plug->params[PARAM_ATTACK].load(std::memory_order_relaxed), 0.002f, 3.0f), std::clamp(plug->params[PARAM_DECAY].load(std::memory_order_relaxed), 0.05f, 8.0f), s, process->out_events, voicesL, voicesR, sum_fx, sum_fy, sum_energy, sum_freq, v);
            } else {
                plug->telemetry.voices[v].active = false;
            }
        }
        plug->qfield.active_count = active_cnt;
        if (active_cnt > 0) {
            float inv_n = 1.0f / static_cast<float>(active_cnt);
            plug->qfield.field_x = std::tanh(sum_fx * 0.6f); plug->qfield.field_y = std::tanh(sum_fy * inv_n);
            plug->qfield.plasma_energy = plug->qfield.plasma_energy * 0.992f + (sum_energy * inv_n) * 0.008f;
            plug->qfield.centroid_freq = plug->qfield.centroid_freq * 0.995f + (sum_freq * inv_n) * 0.005f;
        } else {
            plug->qfield.field_x *= 0.99f; plug->qfield.field_y *= 0.99f; plug->qfield.plasma_energy *= 0.99f;
        }
        float spacedL = 0.0f, spacedR = 0.0f;
        plug->diffuser.process(voicesL, voicesR, std::clamp(plug->params[PARAM_SPACE].load(std::memory_order_relaxed), 0.0f, 1.0f), plug->qfield.plasma_energy, plug->sample_rate, spacedL, spacedR);
        const float gain = std::clamp(plug->params[PARAM_GAIN].load(std::memory_order_relaxed), 0.0f, 1.0f);
        outL[s] = std::tanh(spacedL * gain);
        if (outR != outL) outR[s] = std::tanh(spacedR * gain);
    }
    return CLAP_PROCESS_CONTINUE;
}

static const char* const s_features[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr };
static const clap_plugin_descriptor_t s_descriptor = {
    .clap_version = CLAP_VERSION_INIT, .id = "com.experimental.tectonic-synth",
    .name = "Tectonic Experimental Synth v5", .vendor = "Experimental Audio Lab",
    .url = "https://clapdb.org", .manual_url = "", .support_url = "", .version = "5.0.0",
    .description = "Dear ImGui Observatory, Quantum Entanglement & 4D Continuum Synth", .features = s_features
};

static bool CLAP_ABI pluginInit(const clap_plugin_t*) { return true; }
static void CLAP_ABI pluginDestroy(const clap_plugin_t* p) { delete static_cast<TectonicPlugin*>(p->plugin_data); delete p; }
static bool CLAP_ABI pluginActivate(const clap_plugin_t* p, double sr, uint32_t, uint32_t) {
    auto* plug = static_cast<TectonicPlugin*>(p->plugin_data); plug->sample_rate = static_cast<float>(sr > 8000 ? sr : 48000); plug->qfield.reset(); plug->diffuser.reset(); return true;
}
static void CLAP_ABI pluginDeactivate(const clap_plugin_t*) {}
static bool CLAP_ABI pluginStartProcessing(const clap_plugin_t*) { return true; }
static void CLAP_ABI pluginStopProcessing(const clap_plugin_t*) {}
static void CLAP_ABI pluginReset(const clap_plugin_t* p) {
    auto* plug = static_cast<TectonicPlugin*>(p->plugin_data); for (auto& v : plug->voices) v.active = false; plug->qfield.reset(); plug->diffuser.reset();
}
static const void* CLAP_ABI pluginGetExtension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &s_params_ext;
    if (!std::strcmp(id, CLAP_EXT_STATE)) return &s_state_ext;
    if (!std::strcmp(id, CLAP_EXT_GUI)) return &s_gui_ext;
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &s_audio_ports_ext;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS)) return &s_note_ports_ext;
    if (!std::strcmp(id, CLAP_EXT_VOICE_INFO)) return &s_voice_info_ext;
    return nullptr;
}
static void CLAP_ABI pluginOnMainThread(const clap_plugin_t*) {}

static uint32_t CLAP_ABI factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* CLAP_ABI factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t idx) { return idx == 0 ? &s_descriptor : nullptr; }
static const clap_plugin_t* CLAP_ABI factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (!id || std::strcmp(id, s_descriptor.id) != 0) return nullptr;
    auto* plug_data = new TectonicPlugin(); plug_data->host = host;
    return new clap_plugin_t{ &s_descriptor, plug_data, pluginInit, pluginDestroy, pluginActivate, pluginDeactivate, pluginStartProcessing, pluginStopProcessing, pluginReset, pluginProcess, pluginGetExtension, pluginOnMainThread };
}
static const clap_plugin_factory_t s_factory = { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

static const void* CLAP_ABI entryGetFactory(const char* fid) {
    return fid && !std::strcmp(fid, CLAP_PLUGIN_FACTORY_ID) ? &s_factory : nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = [](const char*) { return true; },
    .deinit = []() {},
    .get_factory = entryGetFactory
};
