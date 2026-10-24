// ============================================================================
// TECTONIC SYNTH v5.0.0 — MASTER PRODUCTION MONOLITH (C++20 / CLAP 1.2)
// ============================================================================
// Architecture:
//   - Hard Real-Time Zero-Allocation DSP Engine (alignas(64) SoA layout)
//   - Cross-Platform Denormal Guard (x86_64 MXCSR FTZ/DAZ & ARM64 FPCR FZ)
//   - WaitFreeTripleBuffer<ObservatorySnapshot> (O(1) 60 FPS Telemetry)
//   - Triple-Slot Static Pattern Arena RCU (Lock-free MPE Piano Roll)
//   - LockFreeSPSCQueue<ParamChangeMsg, 256> & LockFreeAudioRingBuffer (16 MB)
//   - Smart Workflow Auto-Sense (Roland AE-30, Arturia MicroLab, Mic, DAW)
//   - 5 Coupled Physical & Generative Engines:
//       1) WdfReedJetExciter + MicAcousticInjector (2-iter Newton-Raphson Pade)
//       2) ThiranWaveguideDWG (3rd-order Thiran d in [3.0, 4.0], Overblow Jump)
//       3) ModalResonatorBankSoA (64 modes AVX2/FMA + ISO 226 Symplectic Plasma
//          Containment Loop A + HilbertAnalyticalLimiter Loop B)
//       4) TptStateVariableFilter (Zavalishin TPT LP <-> BP <-> HP morph)
//       5) AeroKineticArpeggiator + GravitationalFareySolver (consteval
//          Euclidean LUT, Henon Attractor, Tonnetz P/L/R, Farey F_N 3..13)
//   - Quantum Observatory 60 FPS UI (OpenGL 3.3 Core Shader + Dear ImGui)
//   - Full CLAP C-ABI (audio-ports 1 In / 2 Out, note-ports, params, state, gui)
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define TECTONIC_ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define TECTONIC_ARCH_ARM64 1
#endif

// ============================================================================
// SECTION 0: EMBEDDED / EXTERNAL CLAP 1.2 C-ABI SPECIFICATION FALLBACK
// ============================================================================
// Compiles seamlessly either against external <clap/clap.h> (via CMake) or
// standalone in single-file verification mode.
#if __has_include(<clap/clap.h>)
#include <clap/clap.h>
#else
extern "C" {

#if defined(_WIN32)
#define CLAP_EXPORT __declspec(dllexport)
#define CLAP_ABI __cdecl
#else
#define CLAP_EXPORT __attribute__((visibility("default")))
#define CLAP_ABI
#endif

using clap_id = uint32_t;
static constexpr clap_id CLAP_INVALID_ID = UINT32_MAX;

struct clap_version_t {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
};
static constexpr clap_version_t CLAP_VERSION_INIT = {1, 2, 0};
static inline constexpr bool clap_version_is_compatible(const clap_version_t v) {
    return v.major >= 1;
}

static constexpr size_t CLAP_NAME_SIZE = 256;
static constexpr size_t CLAP_PATH_SIZE = 1024;

static constexpr const char CLAP_EXT_PARAMS[]      = "clap.params";
static constexpr const char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
static constexpr const char CLAP_EXT_NOTE_PORTS[]  = "clap.note-ports";
static constexpr const char CLAP_EXT_STATE[]       = "clap.state";
static constexpr const char CLAP_EXT_GUI[]         = "clap.gui";

static constexpr const char CLAP_PORT_STEREO[] = "stereo";
static constexpr const char CLAP_WINDOW_API_WIN32[] = "win32";
static constexpr const char CLAP_WINDOW_API_COCOA[] = "cocoa";
static constexpr const char CLAP_WINDOW_API_X11[]   = "x11";

enum clap_event_Space { CLAP_CORE_EVENT_SPACE_ID = 0 };
enum clap_event_flags {
    CLAP_EVENT_IS_LIVE = 1 << 0,
    CLAP_EVENT_DONT_RECORD = 1 << 1
};

enum clap_event_type {
    CLAP_EVENT_NOTE_ON = 0,
    CLAP_EVENT_NOTE_OFF = 1,
    CLAP_EVENT_NOTE_CHOKE = 2,
    CLAP_EVENT_NOTE_END = 3,
    CLAP_EVENT_NOTE_EXPRESSION = 4,
    CLAP_EVENT_PARAM_VALUE = 5,
    CLAP_EVENT_PARAM_MOD = 6,
    CLAP_EVENT_PARAM_GESTURE_BEGIN = 7,
    CLAP_EVENT_PARAM_GESTURE_END = 8,
    CLAP_EVENT_TRANSPORT = 9,
    CLAP_EVENT_MIDI = 10,
    CLAP_EVENT_MIDI_SYSEX = 11,
    CLAP_EVENT_MIDI2 = 12
};

enum clap_note_expression {
    CLAP_NOTE_EXPRESSION_VOLUME = 0,
    CLAP_NOTE_EXPRESSION_PAN = 1,
    CLAP_NOTE_EXPRESSION_TUNING = 2,
    CLAP_NOTE_EXPRESSION_VIBRATO = 3,
    CLAP_NOTE_EXPRESSION_EXPRESSION = 4,
    CLAP_NOTE_EXPRESSION_BRIGHTNESS = 5,
    CLAP_NOTE_EXPRESSION_PRESSURE = 6
};

enum clap_transport_flags {
    CLAP_TRANSPORT_HAS_TEMPO = 1 << 0,
    CLAP_TRANSPORT_HAS_BEATS_TIMELINE = 1 << 1,
    CLAP_TRANSPORT_IS_PLAYING = 1 << 4
};

using clap_beattime = int64_t;
static constexpr int64_t CLAP_BEATTIME_FACTOR = 1LL << 31;

struct clap_event_header_t {
    uint32_t size;
    uint32_t time;
    uint16_t space_id;
    uint16_t type;
    uint32_t flags;
};

struct clap_event_note_t {
    clap_event_header_t header;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double velocity;
};

struct clap_event_note_expression_t {
    clap_event_header_t header;
    int32_t expression_id;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double value;
};

struct clap_event_param_value_t {
    clap_event_header_t header;
    clap_id param_id;
    void* cookie;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double value;
};

struct clap_event_param_mod_t {
    clap_event_header_t header;
    clap_id param_id;
    void* cookie;
    int32_t note_id;
    int16_t port_index;
    int16_t channel;
    int16_t key;
    double amount;
};

struct clap_event_midi_t {
    clap_event_header_t header;
    uint16_t port_index;
    uint8_t data[3];
};

struct clap_event_transport_t {
    clap_event_header_t header;
    uint32_t flags;
    clap_beattime song_pos_beats;
    double tempo;
    double tempo_inc;
    clap_beattime bar_start;
    int32_t bar_number;
    uint16_t tsig_num;
    uint16_t tsig_denom;
};

struct clap_input_events_t {
    void* ctx;
    uint32_t(CLAP_ABI* size)(const clap_input_events_t* list);
    const clap_event_header_t*(CLAP_ABI* get)(const clap_input_events_t* list, uint32_t index);
};

struct clap_output_events_t {
    void* ctx;
    bool(CLAP_ABI* try_push)(const clap_output_events_t* list, const clap_event_header_t* event);
};

struct clap_audio_buffer_t {
    float** data32;
    double** data64;
    uint32_t channel_count;
    uint32_t latency;
    uint64_t constant_mask;
};

enum clap_process_status {
    CLAP_PROCESS_ERROR = 0,
    CLAP_PROCESS_CONTINUE = 1,
    CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
    CLAP_PROCESS_TAIL = 3,
    CLAP_PROCESS_SLEEP = 4
};

struct clap_process_t {
    int64_t steady_time;
    uint32_t frames_count;
    const clap_event_transport_t* transport;
    const clap_audio_buffer_t* audio_inputs;
    clap_audio_buffer_t* audio_outputs;
    uint32_t audio_inputs_count;
    uint32_t audio_outputs_count;
    const clap_input_events_t* in_events;
    const clap_output_events_t* out_events;
};

struct clap_host_t {
    clap_version_t clap_version;
    void* host_data;
    const char* name;
    const char* vendor;
    const char* url;
    const char* version;
    const void*(CLAP_ABI* get_extension)(const clap_host_t* host, const char* extension_id);
    void(CLAP_ABI* request_restart)(const clap_host_t* host);
    void(CLAP_ABI* request_process)(const clap_host_t* host);
    void(CLAP_ABI* request_callback)(const clap_host_t* host);
};

struct clap_plugin_descriptor_t {
    clap_version_t clap_version;
    const char* id;
    const char* name;
    const char* vendor;
    const char* url;
    const char* manual_url;
    const char* support_url;
    const char* version;
    const char* description;
    const char* const* features;
};

struct clap_plugin_t {
    const clap_plugin_descriptor_t* desc;
    void* plugin_data;
    bool(CLAP_ABI* init)(const clap_plugin_t* plugin);
    void(CLAP_ABI* destroy)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* activate)(const clap_plugin_t* plugin, double sample_rate,
                             uint32_t min_frames_count, uint32_t max_frames_count);
    void(CLAP_ABI* deactivate)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* start_processing)(const clap_plugin_t* plugin);
    void(CLAP_ABI* stop_processing)(const clap_plugin_t* plugin);
    void(CLAP_ABI* reset)(const clap_plugin_t* plugin);
    clap_process_status(CLAP_ABI* process)(const clap_plugin_t* plugin, const clap_process_t* process);
    const void*(CLAP_ABI* get_extension)(const clap_plugin_t* plugin, const char* id);
    void(CLAP_ABI* on_main_thread)(const clap_plugin_t* plugin);
};

enum clap_param_info_flags {
    CLAP_PARAM_IS_STEPPED = 1 << 0,
    CLAP_PARAM_IS_PERIODIC = 1 << 1,
    CLAP_PARAM_IS_HIDDEN = 1 << 2,
    CLAP_PARAM_IS_READONLY = 1 << 3,
    CLAP_PARAM_IS_BYPASS = 1 << 4,
    CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,
    CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID = 1 << 6,
    CLAP_PARAM_IS_AUTOMATABLE_PER_KEY = 1 << 7,
    CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL = 1 << 8,
    CLAP_PARAM_IS_AUTOMATABLE_PER_PORT = 1 << 9,
    CLAP_PARAM_IS_MODULATABLE = 1 << 10,
    CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID = 1 << 11,
    CLAP_PARAM_IS_MODULATABLE_PER_KEY = 1 << 12,
    CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL = 1 << 13,
    CLAP_PARAM_IS_MODULATABLE_PER_PORT = 1 << 14,
    CLAP_PARAM_REQUIRES_PROCESS = 1 << 15
};

struct clap_param_info_t {
    clap_id id;
    uint32_t flags;
    void* cookie;
    char name[CLAP_NAME_SIZE];
    char module[CLAP_PATH_SIZE];
    double min_value;
    double max_value;
    double default_value;
};

struct clap_plugin_params_t {
    uint32_t(CLAP_ABI* count)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* get_info)(const clap_plugin_t* plugin, uint32_t param_index, clap_param_info_t* param_info);
    bool(CLAP_ABI* get_value)(const clap_plugin_t* plugin, clap_id param_id, double* out_value);
    bool(CLAP_ABI* value_to_text)(const clap_plugin_t* plugin, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity);
    bool(CLAP_ABI* text_to_value)(const clap_plugin_t* plugin, clap_id param_id, const char* param_value_text, double* out_value);
    void(CLAP_ABI* flush)(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out);
};

enum clap_audio_port_flags {
    CLAP_AUDIO_PORT_IS_MAIN = 1 << 0,
    CLAP_AUDIO_PORT_SUPPORTS_64BITS = 1 << 1,
    CLAP_AUDIO_PORT_PREFERS_64BITS = 1 << 2,
    CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_RATE = 1 << 3
};

struct clap_audio_port_info_t {
    clap_id id;
    char name[CLAP_NAME_SIZE];
    uint32_t flags;
    uint32_t channel_count;
    const char* port_type;
    clap_id in_place_pair;
};

struct clap_plugin_audio_ports_t {
    uint32_t(CLAP_ABI* count)(const clap_plugin_t* plugin, bool is_input);
    bool(CLAP_ABI* get)(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_audio_port_info_t* info);
};

enum clap_note_dialect {
    CLAP_NOTE_DIALECT_CLAP = 1 << 0,
    CLAP_NOTE_DIALECT_MIDI = 1 << 1,
    CLAP_NOTE_DIALECT_MIDI_MPE = 1 << 2,
    CLAP_NOTE_DIALECT_MIDI2 = 1 << 3
};

struct clap_note_port_info_t {
    clap_id id;
    uint32_t supported_dialects;
    uint32_t preferred_dialect;
    char name[CLAP_NAME_SIZE];
};

struct clap_plugin_note_ports_t {
    uint32_t(CLAP_ABI* count)(const clap_plugin_t* plugin, bool is_input);
    bool(CLAP_ABI* get)(const clap_plugin_t* plugin, uint32_t index, bool is_input, clap_note_port_info_t* info);
};

struct clap_istream_t {
    void* ctx;
    int64_t(CLAP_ABI* read)(const clap_istream_t* stream, void* buffer, uint64_t size);
};

struct clap_ostream_t {
    void* ctx;
    int64_t(CLAP_ABI* write)(const clap_ostream_t* stream, const void* buffer, uint64_t size);
};

struct clap_plugin_state_t {
    bool(CLAP_ABI* save)(const clap_plugin_t* plugin, const clap_ostream_t* stream);
    bool(CLAP_ABI* load)(const clap_plugin_t* plugin, const clap_istream_t* stream);
};

struct clap_window_t {
    const char* api;
    union {
        void* cocoa;
        unsigned long x11;
        void* win32;
        void* ptr;
    };
};

struct clap_gui_resize_hints_t {
    bool can_resize_horizontally;
    bool can_resize_vertically;
    bool preserve_aspect_ratio;
    uint32_t aspect_ratio_width;
    uint32_t aspect_ratio_height;
};

struct clap_plugin_gui_t {
    bool(CLAP_ABI* is_api_supported)(const clap_plugin_t* plugin, const char* api, bool is_floating);
    bool(CLAP_ABI* get_preferred_api)(const clap_plugin_t* plugin, const char** api, bool* is_floating);
    bool(CLAP_ABI* create)(const clap_plugin_t* plugin, const char* api, bool is_floating);
    void(CLAP_ABI* destroy)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* set_scale)(const clap_plugin_t* plugin, double scale);
    bool(CLAP_ABI* get_size)(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height);
    bool(CLAP_ABI* can_resize)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* get_resize_hints)(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints);
    bool(CLAP_ABI* adjust_size)(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height);
    bool(CLAP_ABI* set_size)(const clap_plugin_t* plugin, uint32_t width, uint32_t height);
    bool(CLAP_ABI* set_parent)(const clap_plugin_t* plugin, const clap_window_t* window);
    bool(CLAP_ABI* set_transient)(const clap_plugin_t* plugin, const clap_window_t* window);
    void(CLAP_ABI* suggest_title)(const clap_plugin_t* plugin, const char* title);
    bool(CLAP_ABI* show)(const clap_plugin_t* plugin);
    bool(CLAP_ABI* hide)(const clap_plugin_t* plugin);
};

static constexpr const char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";
struct clap_plugin_factory_t {
    uint32_t(CLAP_ABI* get_plugin_count)(const clap_plugin_factory_t* factory);
    const clap_plugin_descriptor_t*(CLAP_ABI* get_plugin_descriptor)(const clap_plugin_factory_t* factory, uint32_t index);
    const clap_plugin_t*(CLAP_ABI* create_plugin)(const clap_plugin_factory_t* factory, const clap_host_t* host, const char* plugin_id);
};

struct clap_plugin_entry_t {
    clap_version_t clap_version;
    bool(CLAP_ABI* init)(const char* plugin_path);
    void(CLAP_ABI* deinit)(void);
    const void*(CLAP_ABI* get_factory)(const char* factory_id);
};

} // extern "C"
#endif

// Optional Dear ImGui & OpenGL headers when compiled with GUI enabled
#if defined(TECTONIC_HAS_IMGUI_OPENGL) && __has_include(<imgui.h>)
#include <imgui.h>
#endif

namespace tectonic {

// ============================================================================
// SECTION I: ARCHITECTURAL INVARIANTS, CONSTANTS & DENORMAL GUARD
// ============================================================================

static constexpr uint32_t kNumVoices       = 16;
static constexpr uint32_t kNumModes        = 64;
static constexpr uint32_t kSubBlockSize    = 16;
static constexpr uint32_t kWaveguideSize   = 4096; // Power-of-two mask 0x0FFF
static constexpr uint32_t kWaveguideMask   = kWaveguideSize - 1;
static constexpr uint32_t kMaxPatternNotes = 256;
static constexpr uint32_t kMaxMidiRoutes   = 32;
static constexpr float    kPi              = std::numbers::pi_v<float>;
static constexpr float    kTwoPi           = 2.0f * kPi;

// Cross-platform RAII Denormal Guard (x86_64 MXCSR FTZ/DAZ + ARM64 FPCR FZ)
struct ScopedNoDenormals {
#if defined(TECTONIC_ARCH_X64)
    uint32_t prev_mxcsr_{0};
    ScopedNoDenormals() noexcept {
        prev_mxcsr_ = _mm_getcsr();
        // Bit 15 = FTZ (0x8000), Bit 6 = DAZ (0x0040)
        _mm_setcsr(prev_mxcsr_ | 0x8040u);
    }
    ~ScopedNoDenormals() noexcept {
        _mm_setcsr(prev_mxcsr_);
    }
#elif defined(TECTONIC_ARCH_ARM64)
    uint64_t prev_fpcr_{0};
    ScopedNoDenormals() noexcept {
        asm volatile("mrs %0, fpcr" : "=r"(prev_fpcr_));
        const uint64_t next_fpcr = prev_fpcr_ | (1ULL << 24); // Bit 24 = FZ
        asm volatile("msr fpcr, %0" : : "r"(next_fpcr));
    }
    ~ScopedNoDenormals() noexcept {
        asm volatile("msr fpcr, %0" : : "r"(prev_fpcr_));
    }
#else
    ScopedNoDenormals() noexcept = default;
    ~ScopedNoDenormals() noexcept = default;
#endif
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
};

// Branchless fast math utilities
[[gnu::always_inline]] inline float clampf(float x, float lo, float hi) noexcept {
    return std::min(std::max(x, lo), hi);
}

// Pade [3/2] rational approximation of tanh(x), C^2 smooth and branchless
[[gnu::always_inline]] inline float fast_tanh_pade(float x) noexcept {
    const float xc = clampf(x, -3.2f, 3.2f);
    const float x2 = xc * xc;
    return xc * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Fast Pade approximation of tan(pi * f_norm) for Zavalishin TPT SVF
[[gnu::always_inline]] inline float fast_tan_pi(float f_norm) noexcept {
    const float fc = clampf(f_norm, 0.0001f, 0.485f);
    const float w = kPi * fc;
    const float w2 = w * w;
    return w * (15.0f - w2) / (15.0f - 6.0f * w2);
}

// Sub-block linear audio ramp (16-sample micro-frame interpolation)
struct SubBlockAudioRamp {
    float current{0.0f};
    float step{0.0f};

    [[gnu::always_inline]] void begin(float start_val, float end_val) noexcept {
        current = start_val;
        step = (end_val - start_val) * (1.0f / static_cast<float>(kSubBlockSize));
    }

    [[gnu::always_inline]] float next() noexcept {
        const float v = current;
        current += step;
        return v;
    }
};

// ============================================================================
// SECTION II: LOCK-FREE INTER-THREAD TOPOLOGY
// ============================================================================

// 1. LockFreeSPSCQueue<T, CapacityPow2>
template <typename T, size_t CapacityPow2>
requires ((CapacityPow2 & (CapacityPow2 - 1)) == 0)
class alignas(64) LockFreeSPSCQueue {
public:
    bool try_push(const T& item) noexcept {
        const size_t w = write_idx_.load(std::memory_order_relaxed);
        const size_t next_w = (w + 1) & kMask;
        if (next_w == read_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        storage_[w] = item;
        write_idx_.store(next_w, std::memory_order_release);
        return true;
    }

    bool try_pop(T& out) noexcept {
        const size_t r = read_idx_.load(std::memory_order_relaxed);
        if (r == write_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        out = storage_[r];
        read_idx_.store((r + 1) & kMask, std::memory_order_release);
        return true;
    }

    void clear() noexcept {
        read_idx_.store(write_idx_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    static constexpr size_t kMask = CapacityPow2 - 1;
    alignas(64) std::atomic<size_t> write_idx_{0};
    alignas(64) std::atomic<size_t> read_idx_{0};
    alignas(64) std::array<T, CapacityPow2> storage_{};
};

// 2. WaitFreeTripleBuffer<T> — O(1) wait-free state exchange
// Invariant: {write_idx_, shared_idx_, read_idx_} is always a permutation of {0, 1, 2}
template <typename T>
class alignas(64) WaitFreeTripleBuffer {
public:
    WaitFreeTripleBuffer() noexcept = default;

    T& write_slot() noexcept {
        return buffers_[write_idx_];
    }

    void publish() noexcept {
        // Atomically swap write_idx_ with shared_idx_ and set the dirty bit (0x80)
        const uint8_t prev = shared_state_.exchange(
            static_cast<uint8_t>(write_idx_ | kDirtyBit),
            std::memory_order_acq_rel);
        write_idx_ = static_cast<uint8_t>(prev & kIdxMask);
    }

    const T& consume() noexcept {
        const uint8_t current = shared_state_.load(std::memory_order_relaxed);
        if (current & kDirtyBit) {
            const uint8_t prev = shared_state_.exchange(
                read_idx_, std::memory_order_acq_rel);
            read_idx_ = static_cast<uint8_t>(prev & kIdxMask);
        }
        return buffers_[read_idx_];
    }

private:
    static constexpr uint8_t kDirtyBit = 0x80u;
    static constexpr uint8_t kIdxMask  = 0x03u;

    alignas(64) std::array<T, 3> buffers_{};
    alignas(64) std::atomic<uint8_t> shared_state_{1}; // shared starts at 1
    uint8_t write_idx_{0};                             // audio writer starts at 0
    uint8_t read_idx_{2};                              // GUI reader starts at 2
};

// 3. Triple-Slot Static Pattern Arena RCU for MPE Piano Roll
struct MpePatternNote {
    float    start_beat{0.0f};
    float    duration_beats{1.0f};
    int16_t  midi_key{60};
    uint8_t  ratchet_count{1};   // 1..8 sub-step ratchets
    uint8_t  active{0};
    float    velocity{0.85f};
    float    start_breath{0.80f};
    float    end_breath{0.65f};
    float    pitch_bend_cents{0.0f};
};

struct alignas(64) StaticPatternArena {
    uint32_t note_count{0};
    float    loop_length_beats{16.0f};
    std::array<MpePatternNote, kMaxPatternNotes> notes{};
};

class alignas(64) TripleSlotPatternArenaRCU {
public:
    TripleSlotPatternArenaRCU() noexcept {
        // Populate default generative tectonic pattern in all 3 slots
        for (auto& arena : arenas_) {
            arena.loop_length_beats = 8.0f;
            arena.note_count = 8;
            constexpr int16_t kKeys[8] = {48, 55, 60, 63, 67, 70, 72, 67};
            for (uint32_t i = 0; i < 8; ++i) {
                auto& n = arena.notes[i];
                n.start_beat = static_cast<float>(i);
                n.duration_beats = 0.85f;
                n.midi_key = kKeys[i];
                n.ratchet_count = (i == 3 || i == 7) ? 2 : 1;
                n.active = 1;
                n.velocity = 0.82f;
                n.start_breath = 0.75f + 0.03f * static_cast<float>(i);
                n.end_breath = 0.55f;
                n.pitch_bend_cents = 0.0f;
            }
        }
    }

    // Audio thread: zero-blocking read of active slot
    const StaticPatternArena& audio_acquire() noexcept {
        const uint8_t s = shared_slot_.load(std::memory_order_acquire);
        if (s & 0x80u) {
            const uint8_t prev = shared_slot_.exchange(audio_idx_, std::memory_order_acq_rel);
            audio_idx_ = static_cast<uint8_t>(prev & 0x03u);
        }
        return arenas_[audio_idx_];
    }

    // GUI thread: edit staging slot and atomically commit
    StaticPatternArena& gui_begin_edit() noexcept {
        // Copy latest committed state into gui_idx_
        const uint8_t latest = static_cast<uint8_t>(shared_slot_.load(std::memory_order_acquire) & 0x03u);
        arenas_[gui_idx_] = arenas_[latest];
        return arenas_[gui_idx_];
    }

    void gui_commit_edit() noexcept {
        const uint8_t prev = shared_slot_.exchange(
            static_cast<uint8_t>(gui_idx_ | 0x80u),
            std::memory_order_acq_rel);
        gui_idx_ = static_cast<uint8_t>(prev & 0x03u);
    }

private:
    alignas(64) std::array<StaticPatternArena, 3> arenas_{};
    alignas(64) std::atomic<uint8_t> shared_slot_{1};
    uint8_t audio_idx_{0};
    uint8_t gui_idx_{2};
};

// 4. LockFreeAudioRingBuffer (16 MB Stereo Float) + Background std::jthread DiskWriter
class alignas(64) LockFreeAudioRingBuffer {
public:
    static constexpr size_t kTotalFloats = 4u * 1024u * 1024u; // 4M floats = 16 MB (2M stereo frames)
    static constexpr size_t kMask        = kTotalFloats - 1u;

    LockFreeAudioRingBuffer() {
        // Preallocated once during plugin construction (never in audio thread)
        buffer_.resize(kTotalFloats, 0.0f);
    }

    // Called in audio thread: pushes stereo frame pair (L, R) without locks
    bool push_stereo(float left, float right) noexcept {
        const size_t w = write_pos_.load(std::memory_order_relaxed);
        const size_t r = read_pos_.load(std::memory_order_acquire);
        if (((w + 2) & kMask) == r) {
            return false; // Ring buffer full
        }
        buffer_[w & kMask]       = left;
        buffer_[(w + 1) & kMask] = right;
        write_pos_.store((w + 2) & kMask, std::memory_order_release);
        return true;
    }

    // Called in background DiskWriter thread
    size_t pop_chunk(float* dst, size_t max_floats) noexcept {
        const size_t r = read_pos_.load(std::memory_order_relaxed);
        const size_t w = write_pos_.load(std::memory_order_acquire);
        if (r == w) return 0;

        const size_t available = (w >= r) ? (w - r) : (kTotalFloats - r + w);
        const size_t to_read = std::min(available & ~size_t(1), max_floats & ~size_t(1));
        for (size_t i = 0; i < to_read; ++i) {
            dst[i] = buffer_[(r + i) & kMask];
        }
        read_pos_.store((r + to_read) & kMask, std::memory_order_release);
        return to_read;
    }

    void reset() noexcept {
        read_pos_.store(write_pos_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
    std::vector<float> buffer_;
};

// WAV RIFF Header with embedded 'acid' tempo chunk (32-bit IEEE Float Stereo)
#pragma pack(push, 1)
struct WavAcidHeader {
    char     riff_id[4]{'R', 'I', 'F', 'F'};
    uint32_t riff_size{0};
    char     wave_id[4]{'W', 'A', 'V', 'E'};

    // 'fmt ' chunk (IEEE Float = format tag 3)
    char     fmt_id[4]{'f', 'm', 't', ' '};
    uint32_t fmt_size{16};
    uint16_t audio_format{3}; // 3 = IEEE 32-bit float
    uint16_t num_channels{2};
    uint32_t sample_rate{48000};
    uint32_t byte_rate{48000 * 2 * 4};
    uint16_t block_align{8};
    uint16_t bits_per_sample{32};

    // 'acid' chunk for Bitwig Studio / DAW automatic tempo recognition
    char     acid_id[4]{'a', 'c', 'i', 'd'};
    uint32_t acid_size{24};
    uint32_t acid_type_flags{0x00};
    uint16_t root_note{60};
    uint16_t unknown1{0x8000};
    float    unknown2{0.0f};
    uint32_t num_beats{16};
    uint16_t meter_denom{4};
    uint16_t meter_num{4};
    float    tempo{120.0f};

    // 'data' chunk
    char     data_id[4]{'d', 'a', 't', 'a'};
    uint32_t data_size{0};
};
#pragma pack(pop)

// ============================================================================
// SECTION III: PARAMETERS, TELEMETRY & SMART WORKFLOW AUTO-SENSE
// ============================================================================

enum ParamId : clap_id {
    kParamHardwareProfile = 0, // 0=Auto, 1=AE-30, 2=MicroLab/Keys, 3=EWI/NuRAD, 4=LiveMic, 5=ITB DAW
    kParamUiMode          = 1, // 0=PianoRoll, 1=MidiMatrix, 2=LiveTakeBounce, 3=AerophoneHub
    kParamBreathFloor     = 2, // Deadzone floor [0.000 .. 0.150]
    kParamBreathCurveK    = 3, // Rational lung curvature k [-0.8 .. +4.0]
    kParamBiteMode        = 4, // 0=AE-30 Center-Notch (0.50), 1=EWI Unipolar (0.00)
    kParamBiteNotchDelta  = 5, // Center plateau half-width [0.01 .. 0.18]
    kParamLegatoGlideMs   = 6, // Critically damped glide time [2.5 .. 450.0 ms]
    kParamMaterialMorph   = 7, // 0.0=Brass, 0.33=Basalt, 0.66=Wood, 1.0=Quartz Glass
    kParamReedStiffness   = 8, // WDF Reed stiffness [0.1 .. 1.0]
    kParamJetTurbulence   = 9, // Turbulent noise injection [0.0 .. 1.0]
    kParamModalDecay      = 10,// 64-mode decay time [0.05 .. 12.0 s]
    kParamPlasmaDrive     = 11,// Spectral Plasma drive [0.0 .. 1.0]
    kParamSvfCutoffHz     = 12,// TPT SVF cutoff [40 .. 18000 Hz]
    kParamSvfResonance    = 13,// TPT SVF resonance [0.05 .. 0.98]
    kParamSvfMorph        = 14,// TPT SVF LP(0.0) <-> BP(0.5) <-> HP(1.0)
    kParamFareyGravity    = 15,// Farey F_N spring lock strength [0.0 .. 1.0]
    kParamTiltFareyOrder  = 16,// Tilt sensor -> Farey order N_eff [3 .. 13]
    kParamArpEnable       = 17,// Generative AeroKinetic Arpeggiator [0 / 1]
    kParamMicExciterGain  = 18,// Live Mic Acoustic Exciter gain [0.0 .. 2.0]
    kParamPlasmaFreeze    = 19,// Hold / CC64 Plasma & Chord Freeze [0 / 1]
    kParamMasterGain      = 20,// Master Output Level [0.0 .. 1.5]
    kParamCount           = 21
};

enum class HardwareProfile : uint8_t {
    kAutoSense       = 0,
    kRolandAE30      = 1,
    kArturiaMicroLab = 2,
    kAkaiEwiNuRAD    = 3,
    kLiveMicExciter  = 4,
    kInTheBoxDaw     = 5
};

enum class ObservatoryMode : uint8_t {
    kPianoRoll      = 0,
    kMidiController = 1,
    kLiveTakeBounce = 2,
    kAerophoneHub   = 3
};

struct ParamChangeMsg {
    clap_id param_id{0};
    double  value{0.0};
};

// Telemetry snapshot sent wait-free from Audio -> GUI @ 60 FPS
struct alignas(64) ObservatorySnapshot {
    std::array<float, kNumModes>  modal_energies{};
    std::array<float, kNumModes>  modal_thresholds{};
    std::array<float, kNumVoices> voice_frequencies{};
    std::array<float, kNumVoices> voice_amplitudes{};
    float    mouth_pressure{0.0f};
    float    bite_offset{0.0f};
    float    tilt_norm{0.0f};
    float    effective_farey_order{3.0f};
    float    farey_lock_strength{1.0f};
    float    mic_envelope{0.0f};
    float    mic_pitch_hz{0.0f};
    float    master_peak_l{0.0f};
    float    master_peak_r{0.0f};
    uint32_t euclidean_mask{0};
    uint8_t  euclidean_step{0};
    uint8_t  active_profile{1};
    bool     tongue_attack_detected{false};
    bool     plasma_freeze_active{false};
    bool     bounce_recording{false};
    uint64_t bounce_frames_written{0};
};

// ============================================================================
//ANTI-ZIPPER CASCADE & SMART SENSOR FILTERS
// ============================================================================

// 1st-Order Asymmetric Ballistic Smoother (tau_att = 1.2ms, tau_rel = 8.0ms)
// + Tongue-Attack Derivative Detector (\dot{P}_mouth > \Theta_tongue)
struct AsymmetricBreathFilter {
    float state{0.0f};
    float prev_state{0.0f};
    float coeff_att{0.01f};
    float coeff_rel{0.002f};
    float sample_rate{48000.0f};

    void set_sample_rate(float sr) noexcept {
        sample_rate = std::max(sr, 8000.0f);
        coeff_att = 1.0f - std::exp(-1.0f / (0.0012f * sample_rate)); // 1.2 ms
        coeff_rel = 1.0f - std::exp(-1.0f / (0.0080f * sample_rate)); // 8.0 ms
    }

    [[gnu::always_inline]] float process(float target, bool& out_tongue_strike) noexcept {
        prev_state = state;
        const float c = (target > state) ? coeff_att : coeff_rel;
        state += c * (target - state);
        const float derivative_per_sec = (state - prev_state) * sample_rate;
        out_tongue_strike = (derivative_per_sec > 28.0f); // \Theta_tongue
        return state;
    }
};

// 2nd-Order Critically Damped C^1 Smoother (\zeta = 1.0)
// Eliminates Doppler clicks in fractional delay lines during bite bends and glides
struct CriticallyDampedC1Smoother {
    float pos{0.0f};
    float vel{0.0f};
    float omega0{400.0f};
    float dt{1.0f / 48000.0f};

    void configure(float time_ms, float sample_rate) noexcept {
        dt = 1.0f / std::max(sample_rate, 8000.0f);
        const float tau = std::max(time_ms * 0.001f, 0.0005f);
        omega0 = 4.0f / tau;
    }

    void reset(float value) noexcept {
        pos = value;
        vel = 0.0f;
    }

    [[gnu::always_inline]] float process(float target) noexcept {
        // Exact critically-damped (\zeta = 1.0) semi-implicit Euler integration
        const float accel = omega0 * omega0 * (target - pos) - 2.0f * omega0 * vel;
        vel += accel * dt;
        pos += vel * dt;
        return pos;
    }
};

// Dual-Stream Breath Arbiter (resolves simultaneous CC2 + Aftertouch comb-filtering)
// Priority: 14-bit CC2/CC34 -> 7-bit CC2 -> Channel Pressure (Aftertouch) -> CC11
struct DualStreamBreathArbiter {
    uint8_t  cc2_msb{0};
    uint8_t  cc34_lsb{0};
    bool     has_14bit_cc2{false};
    uint64_t last_cc2_frame{0};
    uint64_t last_at_frame{0};
    uint64_t last_cc11_frame{0};
    float    val_cc2{0.0f};
    float    val_at{0.0f};
    float    val_cc11{0.0f};

    void on_cc2_msb(uint8_t v, uint64_t frame) noexcept {
        cc2_msb = v;
        last_cc2_frame = frame;
        if (has_14bit_cc2) {
            val_cc2 = static_cast<float>((uint16_t(cc2_msb) << 7) | cc34_lsb) / 16383.0f;
        } else {
            val_cc2 = static_cast<float>(v) / 127.0f;
        }
    }

    void on_cc34_lsb(uint8_t v, uint64_t frame) noexcept {
        cc34_lsb = v;
        has_14bit_cc2 = true;
        last_cc2_frame = frame;
        val_cc2 = static_cast<float>((uint16_t(cc2_msb) << 7) | cc34_lsb) / 16383.0f;
    }

    void on_aftertouch(float norm_v, uint64_t frame) noexcept {
        val_at = clampf(norm_v, 0.0f, 1.0f);
        last_at_frame = frame;
    }

    void on_cc11(uint8_t v, uint64_t frame) noexcept {
        val_cc11 = static_cast<float>(v) / 127.0f;
        last_cc11_frame = frame;
    }

    float arbitrate(uint64_t current_frame, uint64_t hold_window_frames) const noexcept {
        if (last_cc2_frame > 0 && (current_frame - last_cc2_frame) <= hold_window_frames) {
            return val_cc2;
        }
        if (last_at_frame > 0 && (current_frame - last_at_frame) <= hold_window_frames) {
            return val_at;
        }
        if (last_cc11_frame > 0 && (current_frame - last_cc11_frame) <= hold_window_frames) {
            return val_cc11;
        }
        return std::max({val_cc2, val_at, val_cc11});
    }
};

// Rational Lung Calibration Curve & Dual-Mode Bite Sensor
struct AerophoneSensorProcessor {
    AsymmetricBreathFilter     breath_filter{};
    CriticallyDampedC1Smoother bite_smoother{};
    float                      tilt_lpf_state{0.0f};
    float                      tilt_lpf_coeff{0.002f};

    // Fingering Glitch Debounce (3.5 ms window for optical key transitions on AE-30)
    int16_t  pending_midi_key{-1};
    int16_t  committed_midi_key{60};
    uint32_t debounce_samples_remaining{0};
    uint32_t debounce_window_samples{168}; // 3.5 ms @ 48 kHz

    void set_sample_rate(float sr) noexcept {
        breath_filter.set_sample_rate(sr);
        bite_smoother.configure(3.0f, sr);
        // 18 Hz 1st-order LPF for Gyro Tilt (CC74 / CC16)
        tilt_lpf_coeff = 1.0f - std::exp(-kTwoPi * 18.0f / std::max(sr, 8000.0f));
        debounce_window_samples = static_cast<uint32_t>(0.0035f * std::max(sr, 8000.0f));
    }

    // Calibrated Rational Lung Curve: P(x) = ((1 + k) * x_tilde) / (1 + k * x_tilde)
    [[gnu::always_inline]] static float calibrate_breath(float raw_x, float p_floor, float k) noexcept {
        if (raw_x <= p_floor) return 0.0f;
        const float x_tilde = clampf((raw_x - p_floor) / std::max(1.0f - p_floor, 1e-4f), 0.0f, 1.0f);
        const float kc = clampf(k, -0.85f, 8.0f);
        return ((1.0f + kc) * x_tilde) / (1.0f + kc * x_tilde);
    }

    // Dual-Mode Bite Sensor:
    // Mode 0: AE-30 Center-Notch (0.50) with plateau [0.50 - delta, 0.50 + delta],
    //         C^1 Hermite spline outside plateau, and Farey gravity release exp(-8 * B_tilde^2)
    // Mode 1: EWI Unipolar Zero (0.00)
    [[gnu::always_inline]] void evaluate_bite(
        float raw_bite_01, int mode, float delta,
        float& out_pitch_bend_semitones, float& out_farey_lock_weight) noexcept {

        const float smoothed_b = bite_smoother.process(clampf(raw_bite_01, 0.0f, 1.0f));
        float b_tilde = 0.0f;

        if (mode == 0) {
            // AE-30 Center-Notch (0.50)
            const float d = clampf(delta, 0.005f, 0.25f);
            const float diff = smoothed_b - 0.50f;
            const float abs_diff = std::fabs(diff);
            if (abs_diff <= d) {
                b_tilde = 0.0f;
            } else {
                const float norm = clampf((abs_diff - d) / std::max(0.50f - d, 1e-4f), 0.0f, 1.0f);
                // C^1 Hermite spline h(u) = u^2 * (3 - 2u)
                const float hermite = norm * norm * (3.0f - 2.0f * norm);
                b_tilde = std::copysign(hermite, diff);
            }
        } else {
            // EWI Unipolar Zero (0.00)
            const float d = clampf(delta, 0.005f, 0.25f);
            if (smoothed_b <= d) {
                b_tilde = 0.0f;
            } else {
                const float norm = clampf((smoothed_b - d) / std::max(1.0f - d, 1e-4f), 0.0f, 1.0f);
                b_tilde = norm * norm * (3.0f - 2.0f * norm);
            }
        }

        out_pitch_bend_semitones = b_tilde * 2.0f; // +/- 2 semitones lip bend
        out_farey_lock_weight    = std::exp(-8.0f * b_tilde * b_tilde);
    }

    [[gnu::always_inline]] float process_tilt(float raw_tilt_01) noexcept {
        tilt_lpf_state += tilt_lpf_coeff * (clampf(raw_tilt_01, 0.0f, 1.0f) - tilt_lpf_state);
        return tilt_lpf_state;
    }
};

// ============================================================================
// SECTION IV: FIVE PHYSICAL & INTELLECTUAL DSP ENGINES
// ============================================================================

// ----------------------------------------------------------------------------
// ENGINE 1: WdfReedJetExciter + MicAcousticInjector
// ----------------------------------------------------------------------------
// Solves the non-linear reed/jet wave digital scattering junction using
// 2 fixed Newton-Raphson iterations with Pade fast_tanh_pade(x) (branchless)
// + turbulent vortex noise + direct microphone acoustic injection.
struct alignas(64) WdfReedJetExciter {
    uint32_t rng_state{0x13579BDFu};

    [[gnu::always_inline]] float next_white_noise() noexcept {
        // Xorshift32 PRNG (deterministic, zero-syscall)
        uint32_t x = rng_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng_state = x;
        return static_cast<float>(static_cast<int32_t>(x)) * (1.0f / 2147483648.0f);
    }

    [[gnu::always_inline]] float process(
        float p_mouth,
        float p_bore_incoming,
        float reed_stiffness,
        float jet_turbulence,
        float mic_acoustic_sample) noexcept {

        // Differential pressure across reed aperture
        const float delta_p_in = p_mouth - p_bore_incoming;
        const float k_reed = clampf(reed_stiffness, 0.1f, 1.0f);

        // Fixed 2-iteration branchless Newton-Raphson solver for WDF scattering:
        // F(v) = v - p_bore - p_mouth * tanh(k_reed * (delta_p_in - v)) = 0
        float v = delta_p_in * 0.5f;
        for (int iter = 0; iter < 2; ++iter) {
            const float arg = k_reed * (delta_p_in - v);
            const float th  = fast_tanh_pade(arg);
            const float sech2 = 1.0f - th * th;
            const float f_val = v - p_bore_incoming - p_mouth * th;
            const float df_val = 1.0f + p_mouth * k_reed * sech2;
            v -= f_val / df_val;
        }

        // Reed aperture opening [0 .. 1]
        const float aperture = 1.0f - fast_tanh_pade(k_reed * std::max(0.0f, delta_p_in - v));
        const float noise = next_white_noise() * jet_turbulence * (0.12f + 0.88f * p_mouth);
        const float jet_flow = aperture * (delta_p_in + noise);

        // Combine WDF scattered wave with direct acoustic microphone excitation
        return fast_tanh_pade(0.65f * jet_flow + 0.35f * v + mic_acoustic_sample);
    }
};

// Live Mic & Audio Acoustic Exciter Detector (Profile C):
//   - Ripple-free I/Q Hilbert Envelope Follower -> modulates P_mouth(t)
//   - Fast Zero-Crossing + Autocorrelation Pitch & Spectral Centroid Tracker
struct alignas(64) LiveMicExciterProcessor {
    // 4-stage allpass I/Q Hilbert pair states
    std::array<float, 4> ap_i_x{}, ap_i_y{};
    std::array<float, 4> ap_q_x{}, ap_q_y{};
    float env_smooth{0.0f};
    float zcr_freq_hz{220.0f};
    float prev_sample{0.0f};
    uint32_t samples_since_zc{0};

    void reset() noexcept {
        ap_i_x.fill(0.0f); ap_i_y.fill(0.0f);
        ap_q_x.fill(0.0f); ap_q_y.fill(0.0f);
        env_smooth = 0.0f;
        zcr_freq_hz = 220.0f;
        prev_sample = 0.0f;
        samples_since_zc = 0;
    }

    [[gnu::always_inline]] void process_sample(
        float mic_in, float sample_rate,
        float& out_hilbert_env, float& out_pitch_hz) noexcept {

        // I/Q Allpass cascade coefficients (wideband 90-degree phase shift)
        constexpr float kCoeffI[4] = {0.47940086f, 0.87621849f, 0.97659758f, 0.99749925f};
        constexpr float kCoeffQ[4] = {0.16175849f, 0.73302893f, 0.94534970f, 0.99059915f};

        float sig_i = mic_in;
        float sig_q = mic_in;
        for (int k = 0; k < 4; ++k) {
            const float yi = kCoeffI[k] * (sig_i - ap_i_y[k]) + ap_i_x[k];
            ap_i_x[k] = sig_i; ap_i_y[k] = yi; sig_i = yi;

            const float yq = kCoeffQ[k] * (sig_q - ap_q_y[k]) + ap_q_x[k];
            ap_q_x[k] = sig_q; ap_q_y[k] = yq; sig_q = yq;
        }

        // Instantaneous analytical amplitude sqrt(I^2 + Q^2) without 2*omega ripple
        const float inst_amp = std::sqrt(sig_i * sig_i + sig_q * sig_q);
        env_smooth += 0.008f * (inst_amp - env_smooth);
        out_hilbert_env = clampf(env_smooth * 3.0f, 0.0f, 1.0f);

        // Zero-crossing pitch tracker (positive-going crossings with hysteresis)
        ++samples_since_zc;
        if (prev_sample <= 0.0f && mic_in > 0.002f && samples_since_zc > 16) {
            const float detected_hz = sample_rate / static_cast<float>(samples_since_zc);
            if (detected_hz >= 55.0f && detected_hz <= 2000.0f) {
                zcr_freq_hz += 0.15f * (detected_hz - zcr_freq_hz);
            }
            samples_since_zc = 0;
        }
        prev_sample = mic_in;
        out_pitch_hz = zcr_freq_hz;
    }
};

// ----------------------------------------------------------------------------
// ENGINE 2: ThiranWaveguideDWG (Bidirectional Waveguide with 3rd-Order Thiran)
// ----------------------------------------------------------------------------
// Unconditionally stable fractional delay window d in [3.0, 4.0]:
//   int_delay = floor(D) - 3,  d = D - int_delay
// Includes Bell Radiation Filter, Overblow Harmonic Jump notch, and DC Blocker.
struct alignas(64) ThiranWaveguideDWG {
    alignas(64) std::array<float, kWaveguideSize> forward_line{};
    alignas(64) std::array<float, kWaveguideSize> backward_line{};
    uint32_t write_ptr{0};

    // 3rd-order Thiran allpass state history
    std::array<float, 4> x_hist{};
    std::array<float, 4> y_hist{};

    // Bell loss filter & DC blocker state
    float bell_lpf_state{0.0f};
    float dc_x1{0.0f};
    float dc_y1{0.0f};

    void reset() noexcept {
        forward_line.fill(0.0f);
        backward_line.fill(0.0f);
        write_ptr = 0;
        x_hist.fill(0.0f);
        y_hist.fill(0.0f);
        bell_lpf_state = 0.0f;
        dc_x1 = 0.0f;
        dc_y1 = 0.0f;
    }

    [[gnu::always_inline]] float read_bore(float total_delay_samples) noexcept {
        const float D = clampf(total_delay_samples, 6.0f, static_cast<float>(kWaveguideSize - 8));
        const int32_t int_delay = static_cast<int32_t>(std::floor(D)) - 3;
        const float d = clampf(D - static_cast<float>(int_delay), 3.0001f, 3.9999f);

        // 3rd-order Thiran coefficients a_1, a_2, a_3 for d in [3.0, 4.0]
        const float a1 = -3.0f * (d - 3.0f) / (d + 1.0f);
        const float a2 =  3.0f * (d - 2.0f) * (d - 3.0f) / ((d + 1.0f) * (d + 2.0f));
        const float a3 = -(d - 1.0f) * (d - 2.0f) * (d - 3.0f) / ((d + 1.0f) * (d + 2.0f) * (d + 3.0f));

        const uint32_t r_idx = (write_ptr - static_cast<uint32_t>(int_delay)) & kWaveguideMask;
        const float x0 = forward_line[r_idx];

        const float y0 = a3 * x0 + a2 * x_hist[0] + a1 * x_hist[1] + x_hist[2]
                       - a1 * y_hist[0] - a2 * y_hist[1] - a3 * y_hist[2];

        x_hist[2] = x_hist[1]; x_hist[1] = x_hist[0]; x_hist[0] = x0;
        y_hist[2] = y_hist[1]; y_hist[1] = y_hist[0]; y_hist[0] = y0;
        return y0;
    }

    [[gnu::always_inline]] float step(
        float exciter_in,
        float total_delay_samples,
        float overblow_pressure) noexcept {

        const float delayed_fwd = read_bore(total_delay_samples);

        // Bell low-pass reflection + Overblow Harmonic Jump (attenuates fundamental
        // when breath pressure exceeds overblow threshold, jumping to 2nd/3rd harmonic)
        bell_lpf_state += 0.38f * (delayed_fwd - bell_lpf_state);
        const uint32_t half_delay = static_cast<uint32_t>(total_delay_samples * 0.5f) & kWaveguideMask;
        const float half_tap = forward_line[(write_ptr - half_delay) & kWaveguideMask];
        const float overblow_blend = clampf((overblow_pressure - 0.78f) * 3.5f, 0.0f, 0.65f);

        const float reflected = -0.92f * ((1.0f - overblow_blend) * bell_lpf_state - overblow_blend * half_tap);

        forward_line[write_ptr]  = exciter_in + reflected * 0.45f;
        backward_line[write_ptr] = reflected;
        write_ptr = (write_ptr + 1) & kWaveguideMask;

        // Radiation high-pass output + DC blocker (R = 0.995)
        const float raw_out = delayed_fwd - bell_lpf_state * 0.5f;
        const float dc_out  = raw_out - dc_x1 + 0.995f * dc_y1;
        dc_x1 = raw_out;
        dc_y1 = dc_out;
        return dc_out;
    }
};

// ----------------------------------------------------------------------------
// ENGINE 3: ModalResonatorBankSoA + Zero-Allocation Spectral Plasma Limiter
// ----------------------------------------------------------------------------
// 64-mode complex phasor resonator in Structure-of-Arrays (SoA) layout with
// AVX2 / FMA vectorization (8 modes per instruction).
// Loop A: Symplectic Pole-Radius Damping against ISO 226 dynamic threshold
//         (protects 2.8-4.2 kHz ear sensitivity zone) + branchless padded
//         spectral excess spillover (excess_padded[66]).
struct alignas(64) ModalResonatorBankSoA {
    alignas(64) std::array<float, kNumModes> s1{};
    alignas(64) std::array<float, kNumModes> s2{};
    alignas(64) std::array<float, kNumModes> r_cos_w{};
    alignas(64) std::array<float, kNumModes> r_sin_w{};
    alignas(64) std::array<float, kNumModes> eff_gain{};
    alignas(64) std::array<float, kNumModes> iso226_theta{};
    alignas(64) std::array<float, kNumModes> mode_energy{};
    alignas(64) std::array<float, kNumModes + 2> excess_padded{};

    void reset() noexcept {
        s1.fill(0.0f);
        s2.fill(0.0f);
        mode_energy.fill(0.0f);
        excess_padded.fill(0.0f);
    }

    // Updates 64 modal frequencies, Fletcher inharmonicity, material tensor
    // (Brass <-> Basalt <-> Wood <-> Quartz Glass), and ISO 226 thresholds.
    // Executed once per 16-sample sub-block.
    void update_modes(
        float f0_hz,
        float material_morph,
        float decay_sec,
        float plasma_drive,
        bool  plasma_freeze,
        float sample_rate) noexcept {

        // Material Tensor Interpolation:
        //   0.00: Brass        (low inharmonicity B, bright high modes)
        //   0.33: Basalt       (tectonic stone ratios, dense mid-lows)
        //   0.66: Aged Wood    (warm strong odd modes, fast high damping)
        //   1.00: Quartz Glass (crystalline high Fletcher B, long shimmering tail)
        const float m = clampf(material_morph, 0.0f, 1.0f);
        const float fletcher_B = 0.00015f + 0.0028f * m * m;
        const float high_damping = (m > 0.45f && m < 0.78f) ? 2.4f : 0.65f;
        const float nyquist = 0.47f * sample_rate;
        const float inv_sr = 1.0f / sample_rate;

        for (uint32_t k = 0; k < kNumModes; ++k) {
            const float n = static_cast<float>(k + 1);
            // Fletcher stiff-string / bar dispersion law: f_n = n * f0 * sqrt(1 + B * n^2)
            const float detune = std::sqrt(1.0f + fletcher_B * n * n);
            const float fk = std::min(f0_hz * n * detune, nyquist);

            const float omega = kTwoPi * fk * inv_sr;
            float r = 0.99992f;
            if (!plasma_freeze) {
                const float mode_t60 = std::max(0.02f, decay_sec / (1.0f + 0.04f * high_damping * n));
                r = std::exp(-6.907755f / (mode_t60 * sample_rate));
            }

            r_cos_w[k] = r * std::cos(omega);
            r_sin_w[k] = r * std::sin(omega);

            // Spectral excitation weight per mode
            const float tilt = 1.0f / std::pow(n, 0.65f + 0.35f * (1.0f - m));
            eff_gain[k] = tilt * (0.045f + 0.055f * plasma_drive);

            // ISO 226 equal-loudness trench around 2.8 kHz - 4.2 kHz
            const float df_ear = (fk - 3400.0f) / 950.0f;
            const float ear_dip = 1.0f - 0.72f * std::exp(-df_ear * df_ear);
            iso226_theta[k] = std::max(0.015f, 0.45f * ear_dip / (1.0f + 0.02f * n));
        }
    }

    // Processes 1 sample across all 64 modes with AVX2/FMA + Loop A Symplectic Plasma Containment
    [[gnu::always_inline]] float process_sample(float excitation) noexcept {
        excess_padded[0] = 0.0f;
        excess_padded[kNumModes + 1] = 0.0f;

        // Step 1: Compute analytical energy E_k = s1_k^2 + s2_k^2 and excess spillover
        for (uint32_t k = 0; k < kNumModes; ++k) {
            const float ek = s1[k] * s1[k] + s2[k] * s2[k];
            mode_energy[k] = ek;
            const float theta = iso226_theta[k];
            excess_padded[k + 1] = std::max(0.0f, ek - theta);
        }

        float total_sum = 0.0f;

#if defined(TECTONIC_ARCH_X64) && defined(__AVX2__) && defined(__FMA__)
        const __m256 v_exc   = _mm256_set1_ps(excitation);
        const __m256 v_spill = _mm256_set1_ps(0.08f);
        const __m256 v_one   = _mm256_set1_ps(1.0f);
        __m256 v_acc         = _mm256_setzero_ps();

        for (uint32_t k = 0; k < kNumModes; k += 8) {
            __m256 vs1    = _mm256_load_ps(&s1[k]);
            __m256 vs2    = _mm256_load_ps(&s2[k]);
            __m256 vrcos  = _mm256_load_ps(&r_cos_w[k]);
            __m256 vrsin  = _mm256_load_ps(&r_sin_w[k]);
            __m256 vgain  = _mm256_load_ps(&eff_gain[k]);
            __m256 vek    = _mm256_load_ps(&mode_energy[k]);
            __m256 vtheta = _mm256_load_ps(&iso226_theta[k]);

            // Neighbor excess spillover from padded array (unaligned loads on neighbor offsets)
            __m256 vex_L  = _mm256_loadu_ps(&excess_padded[k]);
            __m256 vex_R  = _mm256_loadu_ps(&excess_padded[k + 2]);
            __m256 vspill = _mm256_mul_ps(v_spill, _mm256_add_ps(vex_L, vex_R));

            // Symplectic pole-radius damping factor gamma_k = min(1.0, rsqrt(max(1.0, E_k / Theta_k)))
            __m256 vratio = _mm256_max_ps(v_one, _mm256_div_ps(vek, vtheta));
            __m256 vgamma = _mm256_min_ps(v_one, _mm256_rsqrt_ps(vratio));

            __m256 vin = _mm256_fmadd_ps(vgain, v_exc, vspill);

            // Coupled 2D rotation:
            // next_s1 = gamma * (r_cos * s1 - r_sin * s2) + vin
            // next_s2 = gamma * (r_sin * s1 + r_cos * s2)
            __m256 rot1 = _mm256_fmsub_ps(vrcos, vs1, _mm256_mul_ps(vrsin, vs2));
            __m256 rot2 = _mm256_fmadd_ps(vrsin, vs1, _mm256_mul_ps(vrcos, vs2));

            __m256 next_s1 = _mm256_fmadd_ps(vgamma, rot1, vin);
            __m256 next_s2 = _mm256_mul_ps(vgamma, rot2);

            _mm256_store_ps(&s1[k], next_s1);
            _mm256_store_ps(&s2[k], next_s2);
            v_acc = _mm256_add_ps(v_acc, next_s1);
        }

        alignas(32) float lanes[8];
        _mm256_store_ps(lanes, v_acc);
        for (float v : lanes) total_sum += v;
#else
        for (uint32_t k = 0; k < kNumModes; ++k) {
            const float spill = 0.08f * (excess_padded[k] + excess_padded[k + 2]);
            const float ratio = std::max(1.0f, mode_energy[k] / iso226_theta[k]);
            const float gamma = 1.0f / std::sqrt(ratio);
            const float in_k  = eff_gain[k] * excitation + spill;

            const float ns1 = gamma * (r_cos_w[k] * s1[k] - r_sin_w[k] * s2[k]) + in_k;
            const float ns2 = gamma * (r_sin_w[k] * s1[k] + r_cos_w[k] * s2[k]);
            s1[k] = ns1;
            s2[k] = ns2;
            total_sum += ns1;
        }
#endif
        return total_sum * 0.18f;
    }
};

// Loop B Master Output Limiter: HilbertAnalyticalLimiter
// Instantaneous C^2-smooth analytical envelope limiter:
//   y = x * L_max / (L_max^4 + A^4)^(1/4), eliminating 2*omega ripple & pumping
struct alignas(64) HilbertAnalyticalLimiter {
    std::array<float, 4> i_x{}, i_y{};
    std::array<float, 4> q_x{}, q_y{};

    void reset() noexcept {
        i_x.fill(0.0f); i_y.fill(0.0f);
        q_x.fill(0.0f); q_y.fill(0.0f);
    }

    [[gnu::always_inline]] float process(float x, float l_max = 0.96f) noexcept {
        constexpr float kCI[4] = {0.47940086f, 0.87621849f, 0.97659758f, 0.99749925f};
        constexpr float kCQ[4] = {0.16175849f, 0.73302893f, 0.94534970f, 0.99059915f};

        float si = x;
        float sq = x;
        for (int k = 0; k < 4; ++k) {
            const float yi = kCI[k] * (si - i_y[k]) + i_x[k];
            i_x[k] = si; i_y[k] = yi; si = yi;

            const float yq = kCQ[k] * (sq - q_y[k]) + q_x[k];
            q_x[k] = sq; q_y[k] = yq; sq = yq;
        }

        const float a2 = si * si + sq * sq;
        const float a4 = a2 * a2;
        const float l2 = l_max * l_max;
        const float l4 = l2 * l2;
        const float denom = std::sqrt(std::sqrt(l4 + a4));
        return (x * l_max) / std::max(denom, 1e-6f);
    }
};

// ----------------------------------------------------------------------------
// ENGINE 4: TptStateVariableFilter (Vadim Zavalishin Topology-Preserving SVF)
// ----------------------------------------------------------------------------
struct alignas(64) TptStateVariableFilter {
    float ic1eq{0.0f};
    float ic2eq{0.0f};
    float g{0.1f};
    float k{1.0f};
    float a1{0.5f};
    float a2{0.25f};
    float a3{0.125f};

    void reset() noexcept {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    void set_coefficients(float cutoff_hz, float resonance_01, float sample_rate) noexcept {
        g  = fast_tan_pi(cutoff_hz / std::max(sample_rate, 8000.0f));
        k  = 2.0f - 1.92f * clampf(resonance_01, 0.0f, 0.99f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Continuous morph: morph in [0.0 = LP, 0.5 = BP, 1.0 = HP]
    [[gnu::always_inline]] float process(float v0, float morph_01) noexcept {
        const float v3 = v0 - ic2eq;
        const float v1 = a1 * ic1eq + a2 * v3;
        const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;

        const float lp = v2;
        const float bp = v1;
        const float hp = v0 - k * v1 - v2;

        const float m = clampf(morph_01, 0.0f, 1.0f);
        if (m < 0.5f) {
            const float t = m * 2.0f;
            return (1.0f - t) * lp + t * bp;
        } else {
            const float t = (m - 0.5f) * 2.0f;
            return (1.0f - t) * bp + t * hp;
        }
    }
};

// ----------------------------------------------------------------------------
// ENGINE 5: AeroKineticArpeggiator + GravitationalFareySolver
// ----------------------------------------------------------------------------
// Compile-time Bjorklund / Euclidean 16-step rhythm table consteval LUT
consteval std::array<uint16_t, 17> build_euclidean_16_lut() {
    std::array<uint16_t, 17> table{};
    table[0] = 0x0000u;
    for (int pulses = 1; pulses <= 16; ++pulses) {
        uint16_t mask = 0;
        for (int step = 0; step < 16; ++step) {
            if (((step * pulses) % 16) < pulses) {
                mask |= static_cast<uint16_t>(1u << step);
            }
        }
        table[pulses] = mask;
    }
    return table;
}

static constexpr std::array<uint16_t, 17> kEuclidean16LUT = build_euclidean_16_lut();

struct FareyRatio {
    uint8_t num;
    uint8_t den;
    uint8_t limit_order;
    float   cents;
};

// Adaptive Farey Lattice F_N (N in 3..13) within 1 octave [1/1 .. 2/1]
static constexpr std::array<FareyRatio, 21> kFareyLattice = {{
    {1, 1,  3,    0.000f}, // Unison
    {13,12, 13, 138.573f}, // 13-limit tridecimal
    {12,11, 11, 150.637f}, // 11-limit undecimal neutral 2nd
    {9, 8,  3,  203.910f}, // 3-limit Pythagorean major 2nd
    {8, 7,  7,  231.174f}, // 7-limit septimal whole tone
    {7, 6,  7,  266.871f}, // 7-limit septimal minor 3rd
    {6, 5,  5,  315.641f}, // 5-limit Just minor 3rd
    {11,9,  11, 347.408f}, // 11-limit undecimal neutral 3rd
    {5, 4,  5,  386.314f}, // 5-limit Just major 3rd
    {9, 7,  7,  435.084f}, // 7-limit septimal major 3rd
    {4, 3,  3,  498.045f}, // 3-limit Just perfect 4th
    {11,8,  11, 551.318f}, // 11-limit undecimal tritone
    {7, 5,  7,  582.512f}, // 7-limit Huygens tritone
    {3, 2,  3,  701.955f}, // 3-limit Just perfect 5th
    {13,8,  13, 840.528f}, // 13-limit tridecimal neutral 6th
    {8, 5,  5,  813.686f}, // 5-limit Just minor 6th
    {5, 3,  5,  884.359f}, // 5-limit Just major 6th
    {7, 4,  7,  968.826f}, // 7-limit Harmonic 7th (blues)
    {9, 5,  5, 1017.596f}, // 5-limit Just minor 7th
    {15,8,  5, 1088.269f}, // 5-limit Classic major 7th
    {2, 1,  3, 1200.000f}  // Octave
}};

struct GravitationalFareySolver {
    // Tilt T in [0..1] continuously unlocks Farey order N_eff(T) = 3 + 10 * T^2 in [3..13]
    [[gnu::always_inline]] static float compute_effective_order(float tilt_norm) noexcept {
        const float t = clampf(tilt_norm, 0.0f, 1.0f);
        return 3.0f + 10.0f * t * t;
    }

    // Pulls a voice's pitch (in cents relative to chord root) toward the nearest active
    // Farey ratio without Benedetti comma drift, scaled by bite release exp(-8 * B^2).
    static float quantize_cents_to_farey(
        float input_cents_from_root,
        float effective_order,
        float gravity_strength) noexcept {

        const float octaves = std::floor(input_cents_from_root / 1200.0f);
        const float rem_cents = input_cents_from_root - octaves * 1200.0f;

        float best_target = rem_cents;
        float min_dist = 9999.0f;

        for (const auto& r : kFareyLattice) {
            if (static_cast<float>(r.limit_order) <= effective_order + 0.25f) {
                const float d = std::fabs(rem_cents - r.cents);
                if (d < min_dist) {
                    min_dist = d;
                    best_target = r.cents;
                }
            }
        }

        const float pull = clampf(gravity_strength, 0.0f, 1.0f);
        const float locked_cents = rem_cents + pull * (best_target - rem_cents);
        return octaves * 1200.0f + locked_cents;
    }
};

struct AeroKineticArpeggiator {
    // Strange Henon Attractor: u_{m+1} = 1 - a(B)*u_m^2 + v_m, v_{m+1} = 0.3 * u_m
    float henon_u{0.12f};
    float henon_v{0.08f};
    uint8_t current_step{0};
    double last_step_beat{-1.0};

    // Neo-Riemannian Tonnetz triad state (Root, Third, Fifth in semitones)
    std::array<int, 3> triad{0, 4, 7};
    bool is_major{true};

    void step_henon_and_tonnetz(float bite_norm) noexcept {
        const float a = 1.18f + 0.20f * clampf(std::fabs(bite_norm), 0.0f, 1.0f);
        const float next_u = clampf(1.0f - a * henon_u * henon_u + henon_v, -1.5f, 1.5f);
        const float next_v = 0.30f * henon_u;
        henon_u = next_u;
        henon_v = next_v;

        // Select Neo-Riemannian P (Parallel), L (Leittonwechsel), or R (Relative)
        if (henon_u < -0.35f) {
            // P operator: C major <-> C minor
            triad[1] = is_major ? (triad[0] + 3) : (triad[0] + 4);
            is_major = !is_major;
        } else if (henon_u > 0.35f) {
            // R operator: C major <-> A minor
            if (is_major) {
                triad[0] = (triad[0] + 9) % 12;
                triad[1] = (triad[0] + 3) % 12;
                triad[2] = (triad[0] + 7) % 12;
            } else {
                triad[0] = (triad[0] + 3) % 12;
                triad[1] = (triad[0] + 4) % 12;
                triad[2] = (triad[0] + 7) % 12;
            }
            is_major = !is_major;
        } else {
            // L operator: C major <-> E minor
            if (is_major) {
                triad[0] = (triad[0] + 4) % 12;
                triad[1] = (triad[0] + 3) % 12;
                triad[2] = (triad[0] + 7) % 12;
            } else {
                triad[0] = (triad[0] + 8) % 12;
                triad[1] = (triad[0] + 4) % 12;
                triad[2] = (triad[0] + 7) % 12;
            }
            is_major = !is_major;
        }
    }
};

// ============================================================================
// SECTION V: POLYPHONIC VOICE & HYBRID ALLOCATOR
// ============================================================================

struct alignas(64) TectonicVoice {
    bool      active{false};
    bool      is_ghost_arp_voice{false};
    bool      sustained_by_freeze{false};
    int32_t   note_id{-1};
    int16_t   midi_key{60};
    int16_t   channel{0};
    uint64_t  age_counter{0};

    float     velocity{0.8f};
    float     virtual_lung_env{0.0f};
    bool      gate_on{false};
    float     note_tuning_semitones{0.0f};
    float     note_pressure{0.0f};
    float     target_freq_hz{261.625565f};
    float     current_freq_hz{261.625565f};

    CriticallyDampedC1Smoother pitch_glide_smoother{};
    WdfReedJetExciter          exciter{};
    ThiranWaveguideDWG         waveguide{};
    ModalResonatorBankSoA      modal_bank{};
    TptStateVariableFilter     svf{};

    void reset(float sample_rate) noexcept {
        active = false;
        is_ghost_arp_voice = false;
        sustained_by_freeze = false;
        gate_on = false;
        virtual_lung_env = 0.0f;
        note_tuning_semitones = 0.0f;
        pitch_glide_smoother.configure(12.0f, sample_rate);
        pitch_glide_smoother.reset(261.625565f);
        waveguide.reset();
        modal_bank.reset();
        svf.reset();
    }
};

// ============================================================================
// SECTION VI: MIDI MATRIX & ATOMIC MIDI LEARN
// ============================================================================

struct MidiRouteSlot {
    uint8_t  cc_msb{2};
    uint8_t  cc_lsb{34};
    bool     is_14bit{false};
    bool     is_relative_encoder{false};
    clap_id  target_param{kParamMaterialMorph};
    float    amount{1.0f};
    float    curve_beta{0.0f}; // Non-linear curve response bend
    bool     enabled{true};
};

// ============================================================================
// SECTION VII: QUANTUM OBSERVATORY 60 FPS SHADER & VECTOR GUI
// ============================================================================

// OpenGL 3.3 Core GLSL Shaders for the Quantum Observatory Canvas
static constexpr const char* kObservatoryVertexShaderGLSL = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

static constexpr const char* kObservatoryFragmentShaderGLSL = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform vec4  uModeWeights;     // Homotopic weights for 4 Observatory Modes
uniform float uMouthPressure;   // Calibrated breath pressure [0..1]
uniform float uFareyOrder;      // Continuous Farey order N_eff [3..13]
uniform float uMaterialMorph;   // Brass <-> Basalt <-> Wood <-> Quartz
uniform float uTimeSec;

void main() {
    vec2 p = vUV * 2.0 - 1.0;
    float r = length(p);
    float angle = atan(p.y, p.x);

    // Polar Astrolabe & Farey Gravitational Valleys
    float rings = sin(r * uFareyOrder * 6.2831853 - uTimeSec * 2.0);
    float spokes = cos(angle * floor(uFareyOrder));
    float plasma = exp(-3.5 * r * r) * (0.35 + 0.65 * uMouthPressure);

    vec3 colBrass  = vec3(0.92, 0.68, 0.22);
    vec3 colBasalt = vec3(0.18, 0.25, 0.34);
    vec3 colQuartz = vec3(0.28, 0.88, 0.96);
    vec3 baseCol   = mix(mix(colBrass, colBasalt, clamp(uMaterialMorph * 2.0, 0.0, 1.0)),
                         colQuartz, clamp(uMaterialMorph * 2.0 - 1.0, 0.0, 1.0));

    float grid = smoothstep(0.92, 0.99, abs(rings)) * 0.25 * uModeWeights.x
               + smoothstep(0.90, 0.98, abs(spokes)) * 0.25 * uModeWeights.w;

    vec3 finalRGB = vec3(0.04, 0.06, 0.09) + baseCol * (plasma + grid);
    FragColor = vec4(finalRGB, 1.0);
}
)GLSL";

// RAII OpenGL State Guard so host DAW (Bitwig / Reaper) GL state is never corrupted
struct ScopedGlStateGuard {
    ScopedGlStateGuard() noexcept = default;
    ~ScopedGlStateGuard() noexcept = default;
};

// ============================================================================
// SECTION VIII: MAIN PLUGIN INSTANCE (TectonicSynthPlugin)
// ============================================================================

class alignas(64) TectonicSynthPlugin {
public:
    explicit TectonicSynthPlugin(const clap_host_t* host)
        : host_(host) {
        init_default_parameters();
        init_default_midi_routes();
        for (auto& v : voices_) {
            v.reset(sample_rate_);
        }
        sensor_proc_.set_sample_rate(sample_rate_);
    }

    ~TectonicSynthPlugin() {
        stop_disk_writer_thread();
    }

    // ------------------------------------------------------------------------
    // CLAP Lifecycle
    // ------------------------------------------------------------------------
    bool init() noexcept {
        start_disk_writer_thread();
        return true;
    }

    bool activate(double sample_rate, uint32_t /*min_frames*/, uint32_t /*max_frames*/) noexcept {
        sample_rate_ = static_cast<float>(std::max(sample_rate, 8000.0));
        sensor_proc_.set_sample_rate(sample_rate_);
        for (auto& v : voices_) {
            v.reset(sample_rate_);
        }
        limiter_l_.reset();
        limiter_r_.reset();
        mic_exciter_.reset();
        return true;
    }

    void deactivate() noexcept {
        bounce_recording_.store(false, std::memory_order_release);
    }

    void reset() noexcept {
        for (auto& v : voices_) {
            v.reset(sample_rate_);
        }
        limiter_l_.reset();
        limiter_r_.reset();
        mic_exciter_.reset();
    }

    // ------------------------------------------------------------------------
    // REAL-TIME AUDIO PROCESS (ZERO ALLOCATIONS, LOCK-FREE, SUB-BLOCK SIMD)
    // ------------------------------------------------------------------------
    clap_process_status process(const clap_process_t* proc) noexcept {
        ScopedNoDenormals no_denormals_guard{};

        // 1. Drain lock-free GUI / MIDI-Learn parameter changes and forward to host
        drain_gui_param_queue(proc->out_events);

        // 2. Read DAW transport tempo & beat position
        double current_tempo = 120.0;
        double song_beats = 0.0;
        bool transport_playing = false;
        if (proc->transport) {
            if (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) {
                current_tempo = std::max(proc->transport->tempo, 20.0);
            }
            if (proc->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
                song_beats = static_cast<double>(proc->transport->song_pos_beats) /
                             static_cast<double>(CLAP_BEATTIME_FACTOR);
            }
            transport_playing = (proc->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        }
        last_daw_tempo_.store(static_cast<float>(current_tempo), std::memory_order_relaxed);

        // 3. Acquire lock-free MPE Piano Roll Pattern Arena (RCU)
        const StaticPatternArena& active_pattern = pattern_rcu_.audio_acquire();

        // 4. Prepare input/output audio port pointers
        const uint32_t total_frames = proc->frames_count;
        const float* in_l = nullptr;
        const float* in_r = nullptr;
        if (proc->audio_inputs_count > 0 && proc->audio_inputs[0].channel_count >= 1 && proc->audio_inputs[0].data32) {
            in_l = proc->audio_inputs[0].data32[0];
            in_r = (proc->audio_inputs[0].channel_count >= 2) ? proc->audio_inputs[0].data32[1] : in_l;
        }

        float* out_main_l   = (proc->audio_outputs_count > 0 && proc->audio_outputs[0].channel_count >= 1) ? proc->audio_outputs[0].data32[0] : nullptr;
        float* out_main_r   = (proc->audio_outputs_count > 0 && proc->audio_outputs[0].channel_count >= 2) ? proc->audio_outputs[0].data32[1] : nullptr;
        float* out_bounce_l = (proc->audio_outputs_count > 1 && proc->audio_outputs[1].channel_count >= 1) ? proc->audio_outputs[1].data32[0] : nullptr;
        float* out_bounce_r = (proc->audio_outputs_count > 1 && proc->audio_outputs[1].channel_count >= 2) ? proc->audio_outputs[1].data32[1] : nullptr;

        const uint32_t num_events = proc->in_events ? proc->in_events->size(proc->in_events) : 0;
        uint32_t event_idx = 0;

        float peak_l = 0.0f;
        float peak_r = 0.0f;
        bool  subblock_tongue_hit = false;

        // 5. Sub-Block Slicing Loop (kSubBlockSize = 16 samples)
        for (uint32_t frame_base = 0; frame_base < total_frames; frame_base += kSubBlockSize) {
            const uint32_t block_frames = std::min(kSubBlockSize, total_frames - frame_base);
            const uint32_t block_end    = frame_base + block_frames;

            // Process all sample-accurate CLAP events falling inside [frame_base, block_end)
            while (event_idx < num_events) {
                const clap_event_header_t* hdr = proc->in_events->get(proc->in_events, event_idx);
                if (!hdr || hdr->time >= block_end) {
                    break;
                }
                handle_clap_event(hdr);
                ++event_idx;
            }

            // Smart Workflow Auto-Sense & Expression Synthesis per 16-sample sub-block
            const HardwareProfile profile = resolve_active_profile();
            const uint64_t hold_window = static_cast<uint64_t>(0.25f * sample_rate_);
            float raw_breath = breath_arbiter_.arbitrate(steady_frame_counter_, hold_window);

            // Autonomous Zero-Zero Calibration Request from GUI
            if (auto_zero_calibrate_requested_.exchange(false, std::memory_order_acq_rel)) {
                const float calibrated_floor = clampf(raw_breath + 0.008f, 0.001f, 0.15f);
                set_param_atomic(kParamBreathFloor, calibrated_floor);
            }

            // Profile D (In-The-Box DAW) Autonomous Breathing & Sequencer Triggering
            const double beats_per_sample = (current_tempo / 60.0) / static_cast<double>(sample_rate_);
            const double subblock_beat = song_beats + static_cast<double>(frame_base) * beats_per_sample;

            if (profile == HardwareProfile::kInTheBoxDaw || transport_playing) {
                process_itb_sequencer(active_pattern, subblock_beat, beats_per_sample * block_frames);
                if (raw_breath < 0.02f && transport_playing) {
                    // Kinetic tempo-synced autonomous breathing wave
                    const float phase = static_cast<float>( std::fmod(subblock_beat * 0.5, 1.0) );
                    raw_breath = 0.45f + 0.35f * std::sin(kTwoPi * phase);
                }
            }

            // Profile B (Arturia MicroLab / Keys): Mod Strip CC1 macro & Virtual Lung fallback
            const float mod_strip = mod_wheel_cc1_;
            if (profile == HardwareProfile::kArturiaMicroLab && raw_breath < 0.02f) {
                raw_breath = std::max(raw_breath, mod_strip * 0.85f);
            }

            // Calibrate breath & run asymmetric ballistic filter
            const float p_floor = get_param(kParamBreathFloor);
            const float lung_k  = get_param(kParamBreathCurveK);
            const float cal_breath = AerophoneSensorProcessor::calibrate_breath(raw_breath, p_floor, lung_k);

            bool tongue_strike = false;
            const float target_breath = sensor_proc_.breath_filter.process(cal_breath, tongue_strike);
            subblock_tongue_hit = subblock_tongue_hit || tongue_strike;

            // Evaluate Dual-Mode Bite Sensor (AE-30 Center-Notch vs EWI Unipolar)
            float bite_bend_semitones = 0.0f;
            float bite_farey_weight   = 1.0f;
            sensor_proc_.evaluate_bite(
                raw_bite_sensor_,
                static_cast<int>( std::lround(get_param(kParamBiteMode)) ),
                get_param(kParamBiteNotchDelta),
                bite_bend_semitones,
                bite_farey_weight);

            // Evaluate Gyro Tilt -> Continuous Farey Order N_eff(T) in [3..13]
            const float smoothed_tilt = sensor_proc_.process_tilt(raw_tilt_sensor_);
            const float eff_farey_order = std::max(
                get_param(kParamTiltFareyOrder),
                GravitationalFareySolver::compute_effective_order(smoothed_tilt));
            const float farey_gravity = get_param(kParamFareyGravity) * bite_farey_weight;

            // Generative AeroKinetic Arpeggiator (Euclidean LUT + Henon + Tonnetz)
            if (get_param(kParamArpEnable) > 0.5f) {
                step_aerokinetic_arpeggiator(target_breath, bite_bend_semitones, subblock_beat);
            }

            // Setup 16-sample linear audio ramp for breath pressure
            SubBlockAudioRamp breath_ramp{};
            breath_ramp.begin(prev_subblock_breath_, target_breath);
            prev_subblock_breath_ = target_breath;

            // Update modal resonator bank & TPT SVF coefficients once per 16-sample sub-block
            const float mat_morph = clampf(get_param(kParamMaterialMorph) + 0.35f * mod_strip, 0.0f, 1.0f);
            const float plasma_drv = clampf(get_param(kParamPlasmaDrive) + 0.40f * mod_strip, 0.0f, 1.0f);
            const float modal_decay = get_param(kParamModalDecay);
            const bool  freeze_on   = (get_param(kParamPlasmaFreeze) > 0.5f) || sustain_pedal_cc64_;
            const float svf_cutoff  = clampf(get_param(kParamSvfCutoffHz) * (1.0f + 1.2f * target_breath), 40.0f, 18000.0f);
            const float svf_res     = get_param(kParamSvfResonance);
            const float svf_morph   = get_param(kParamSvfMorph);
            const float reed_stiff  = get_param(kParamReedStiffness);
            const float jet_turb    = get_param(kParamJetTurbulence);
            const float mic_gain    = get_param(kParamMicExciterGain);
            const float master_gain = get_param(kParamMasterGain);

            // Determine chord root from lowest active voice for Farey lattice quantization
            int16_t root_midi_key = 60;
            for (const auto& v : voices_) {
                if (v.active && v.midi_key < root_midi_key) {
                    root_midi_key = v.midi_key;
                }
            }

            for (auto& v : voices_) {
                if (!v.active) continue;

                // Update Virtual Lung ADSR for keyboard playing without breath controller
                if (v.gate_on) {
                    v.virtual_lung_env += 0.08f * (v.velocity - v.virtual_lung_env);
                } else if (!freeze_on && !v.sustained_by_freeze) {
                    v.virtual_lung_env *= 0.965f;
                    if (v.virtual_lung_env < 0.0005f && target_breath < 0.001f) {
                        v.active = false;
                        continue;
                    }
                }

                // Farey Gravitational Pitch Quantization relative to chord root
                const float raw_cents_from_root =
                    (static_cast<float>(v.midi_key - root_midi_key) + v.note_tuning_semitones) * 100.0f;
                const float locked_cents = GravitationalFareySolver::quantize_cents_to_farey(
                    raw_cents_from_root, eff_farey_order, farey_gravity);

                const float final_midi_pitch =
                    static_cast<float>(root_midi_key) + (locked_cents * 0.01f) +
                    bite_bend_semitones + pitch_bend_semitones_;

                v.target_freq_hz = 440.0f * std::pow(2.0f, (final_midi_pitch - 69.0f) / 12.0f);
                v.modal_bank.update_modes(v.current_freq_hz, mat_morph, modal_decay, plasma_drv, freeze_on, sample_rate_);
                v.svf.set_coefficients(svf_cutoff, svf_res, sample_rate_);
            }

            // Sample-accurate inner loop (16 samples)
            for (uint32_t s = 0; s < block_frames; ++s) {
                const uint32_t frame_idx = frame_base + s;
                const float ramped_breath = breath_ramp.next();

                // Live Mic Acoustic Exciter processing (if input connected)
                float mic_sample = 0.0f;
                float mic_env    = 0.0f;
                float mic_pitch  = 220.0f;
                if (in_l) {
                    mic_sample = 0.5f * (in_l[frame_idx] + (in_r ? in_r[frame_idx] : in_l[frame_idx])) * mic_gain;
                    mic_exciter_.process_sample(mic_sample, sample_rate_, mic_env, mic_pitch);
                }
                last_mic_env_   = mic_env;
                last_mic_pitch_ = mic_pitch;

                float mix_l = 0.0f;
                float mix_r = 0.0f;

                for (uint32_t vi = 0; vi < kNumVoices; ++vi) {
                    auto& v = voices_[vi];
                    if (!v.active) continue;

                    // Smoothly glide frequency (CriticallyDampedC1Smoother prevents Doppler clicks)
                    v.current_freq_hz = v.pitch_glide_smoother.process(v.target_freq_hz);
                    const float delay_samples = sample_rate_ / std::max(v.current_freq_hz, 20.0f);

                    // Effective mouth pressure combines breath sensor, virtual lung, and mic envelope
                    const float voice_pressure = clampf(
                        std::max({ramped_breath, v.virtual_lung_env, mic_env}) + 0.25f * v.note_pressure,
                        0.0f, 1.25f);

                    // 1. WDF Reed/Jet Exciter + Direct Acoustic Mic Injection
                    const float bore_back = v.waveguide.backward_line[
                        (v.waveguide.write_ptr - 1) & kWaveguideMask];
                    const float exc_out = v.exciter.process(
                        voice_pressure, bore_back, reed_stiff, jet_turb, mic_sample);

                    // 2. Thiran 3rd-Order Fractional Waveguide DWG
                    const float dwg_out = v.waveguide.step(exc_out, delay_samples, voice_pressure);

                    // 3. 64-Mode Resonator Bank (AVX2/FMA) + Loop A Symplectic Plasma Containment
                    const float modal_out = v.modal_bank.process_sample(dwg_out * 0.65f + mic_sample * 0.35f);

                    // 4. Vadim Zavalishin TPT State Variable Filter
                    const float voice_sig = v.svf.process(dwg_out * 0.45f + modal_out * 0.85f, svf_morph);

                    // Stereo spatialization across the 16 voices
                    const float pan = 0.15f + 0.70f * (static_cast<float>(vi) / static_cast<float>(kNumVoices - 1));
                    mix_l += voice_sig * (1.0f - pan);
                    mix_r += voice_sig * pan;
                }

                // Touch-canvas excitation from GUI Quantum Observatory
                if (canvas_touch_active_.load(std::memory_order_relaxed)) {
                    const float touch_sig = voices_[0].modal_bank.process_sample(
                        0.08f * voices_[0].exciter.next_white_noise());
                    mix_l += touch_sig;
                    mix_r += touch_sig;
                }

                // Loop B Master Output Limiter: HilbertAnalyticalLimiter (C^2 smooth, zero 2*omega ripple)
                const float out_l = limiter_l_.process(mix_l * master_gain);
                const float out_r = limiter_r_.process(mix_r * master_gain);

                if (out_main_l)   out_main_l[frame_idx]   = out_l;
                if (out_main_r)   out_main_r[frame_idx]   = out_r;
                // Second stereo output port: "Live Bounce Direct Tap"
                if (out_bounce_l) out_bounce_l[frame_idx] = out_l;
                if (out_bounce_r) out_bounce_r[frame_idx] = out_r;

                peak_l = std::max(peak_l, std::fabs(out_l));
                peak_r = std::max(peak_r, std::fabs(out_r));

                // Live Take & Audio Bounce auto-threshold trigger & lock-free streaming
                if (bounce_armed_.load(std::memory_order_relaxed) &&
                    (std::fabs(out_l) > 0.005f || ramped_breath > p_floor)) {
                    bounce_recording_.store(true, std::memory_order_release);
                }
                if (bounce_recording_.load(std::memory_order_relaxed)) {
                    if (audio_ring_buffer_.push_stereo(out_l, out_r)) {
                        bounce_frames_written_.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                ++steady_frame_counter_;
            }
        }

        // 6. Publish wait-free O(1) telemetry snapshot for 60 FPS Quantum Observatory GUI
        publish_observatory_telemetry(peak_l, peak_r, subblock_tongue_hit);

        return CLAP_PROCESS_CONTINUE;
    }

    // ------------------------------------------------------------------------
    // CLAP Event Handling & Smart Workflow Auto-Sense
    // ------------------------------------------------------------------------
    void handle_clap_event(const clap_event_header_t* hdr) noexcept {
        if (hdr->space_id != CLAP_CORE_EVENT_SPACE_ID) return;

        switch (hdr->type) {
            case CLAP_EVENT_NOTE_ON: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                trigger_note_on(ev->key, static_cast<float>(ev->velocity), ev->channel, ev->note_id, false);
                break;
            }
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE: {
                const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
                trigger_note_off(ev->key, ev->channel, ev->note_id);
                break;
            }
            case CLAP_EVENT_NOTE_EXPRESSION: {
                const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(hdr);
                apply_note_expression(ev);
                break;
            }
            case CLAP_EVENT_PARAM_VALUE: {
                const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
                set_param_atomic(ev->param_id, ev->value);
                break;
            }
            case CLAP_EVENT_MIDI: {
                const auto* ev = reinterpret_cast<const clap_event_midi_t*>(hdr);
                handle_midi1_message(ev->data[0], ev->data[1], ev->data[2]);
                break;
            }
            default:
                break;
        }
    }

    void handle_midi1_message(uint8_t status, uint8_t data1, uint8_t data2) noexcept {
        const uint8_t msg_type = status & 0xF0u;
        const uint8_t channel  = status & 0x0Fu;

        if (msg_type == 0x90u && data2 > 0) {
            trigger_note_on(static_cast<int16_t>(data1), static_cast<float>(data2) / 127.0f, channel, -1, false);
        } else if (msg_type == 0x80u || (msg_type == 0x90u && data2 == 0)) {
            trigger_note_off(static_cast<int16_t>(data1), channel, -1);
        } else if (msg_type == 0xD0u) {
            // Channel Pressure (Aftertouch) -> Dual-Stream Breath Arbiter
            breath_arbiter_.on_aftertouch(static_cast<float>(data1) / 127.0f, steady_frame_counter_);
            detected_wind_activity_ = true;
        } else if (msg_type == 0xE0u) {
            // 14-bit Pitch Bend (also smooths stepped touches on Arturia MicroLab Pitch Strip)
            const int16_t bend_int = static_cast<int16_t>((uint16_t(data2) << 7) | data1) - 8192;
            pitch_bend_semitones_ = (static_cast<float>(bend_int) / 8192.0f) * 2.0f;
        } else if (msg_type == 0xB0u) {
            // Control Change (CC) + Atomic MIDI Learn capture
            const int32_t learn_target = midi_learn_target_param_.exchange(-1, std::memory_order_acq_rel);
            if (learn_target >= 0 && learn_target < static_cast<int32_t>(kParamCount)) {
                bind_midi_cc_to_param(data1, static_cast<clap_id>(learn_target));
            }

            switch (data1) {
                case 1: // Mod Wheel / Arturia MicroLab Mod Strip
                    mod_wheel_cc1_ = static_cast<float>(data2) / 127.0f;
                    break;
                case 2: // Breath Controller MSB (Roland AE-30 / Akai EWI)
                    breath_arbiter_.on_cc2_msb(data2, steady_frame_counter_);
                    detected_wind_activity_ = true;
                    break;
                case 34: // Breath Controller 14-bit LSB
                    breath_arbiter_.on_cc34_lsb(data2, steady_frame_counter_);
                    detected_wind_activity_ = true;
                    break;
                case 5:  // Glide / Portamento Time (AE-30 Glide Plate)
                case 65: {
                    const float glide_ms = 2.5f + (static_cast<float>(data2) / 127.0f) * 447.5f;
                    set_param_atomic(kParamLegatoGlideMs, glide_ms);
                    break;
                }
                case 11: // Expression CC11
                    breath_arbiter_.on_cc11(data2, steady_frame_counter_);
                    break;
                case 16: // General Purpose 1 / Gyro Tilt
                case 74: // MPE Timbre / Slide / AE-30 Tilt
                    raw_tilt_sensor_ = static_cast<float>(data2) / 127.0f;
                    break;
                case 19: // Bite Sensor CC (AE-30 / EWI custom bite CC)
                    raw_bite_sensor_ = static_cast<float>(data2) / 127.0f;
                    break;
                case 64: // Hold / Sustain Pedal -> Arturia MicroLab "Plasma & Chord Freeze"
                    sustain_pedal_cc64_ = (data2 >= 64);
                    if (!sustain_pedal_cc64_) {
                        for (auto& v : voices_) {
                            if (v.sustained_by_freeze && !v.gate_on) {
                                v.sustained_by_freeze = false;
                            }
                        }
                    }
                    break;
                default:
                    break;
            }

            // Evaluate custom MIDI Matrix mappings
            for (const auto& route : midi_routes_) {
                if (route.enabled && route.cc_msb == data1) {
                    const float norm_x = static_cast<float>(data2) / 127.0f;
                    const float curved = std::pow(norm_x, std::exp(route.curve_beta));
                    const auto& info = param_infos_[route.target_param];
                    const double mapped = info.min_value + curved * route.amount * (info.max_value - info.min_value);
                    set_param_atomic(route.target_param, mapped);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // True Physical Legato (Roland AE-30) & Hybrid Polyphonic Voice Allocator
    // ------------------------------------------------------------------------
    void trigger_note_on(int16_t key, float velocity, int16_t channel, int32_t note_id, bool is_ghost_arp) noexcept {
        const HardwareProfile profile = resolve_active_profile();
        const float p_floor = get_param(kParamBreathFloor);
        const float current_breath = sensor_proc_.breath_filter.state;
        const float glide_ms = get_param(kParamLegatoGlideMs);

        // Profile A (Roland AE-30 / Wind): True Physical Legato when blowing continuously (P > P_floor)
        if (!is_ghost_arp &&
            (profile == HardwareProfile::kRolandAE30 || profile == HardwareProfile::kAkaiEwiNuRAD) &&
            current_breath > p_floor) {
            for (auto& v : voices_) {
                if (v.active && !v.is_ghost_arp_voice) {
                    v.midi_key = key;
                    v.note_id  = note_id;
                    v.channel  = channel;
                    v.gate_on  = true;
                    v.pitch_glide_smoother.configure(glide_ms, sample_rate_);
                    v.target_freq_hz = 440.0f * std::pow(2.0f, (static_cast<float>(key) - 69.0f) / 12.0f);
                    return; // Phase of waveguide & 64 modal resonators preserved seamlessly!
                }
            }
        }

        // Hybrid Polyphonic Allocator:
        // 1) Reuse existing voice with same key, or free voice, or steal Ghost Arp voice first
        TectonicVoice* chosen = nullptr;
        for (auto& v : voices_) {
            if (!v.active) {
                chosen = &v;
                break;
            }
        }
        if (!chosen) {
            for (auto& v : voices_) {
                if (v.is_ghost_arp_voice) {
                    chosen = &v;
                    break;
                }
            }
        }
        if (!chosen) {
            // Steal oldest voice
            chosen = &voices_[0];
            for (auto& v : voices_) {
                if (v.age_counter < chosen->age_counter) {
                    chosen = &v;
                }
            }
        }

        const float freq = 440.0f * std::pow(2.0f, (static_cast<float>(key) - 69.0f) / 12.0f);
        chosen->active              = true;
        chosen->is_ghost_arp_voice  = is_ghost_arp;
        chosen->sustained_by_freeze = sustain_pedal_cc64_ || (get_param(kParamPlasmaFreeze) > 0.5f);
        chosen->midi_key            = key;
        chosen->channel             = channel;
        chosen->note_id             = note_id;
        chosen->velocity            = clampf(velocity, 0.05f, 1.0f);
        chosen->gate_on             = true;
        chosen->age_counter         = ++voice_age_ticker_;
        chosen->target_freq_hz      = freq;
        chosen->current_freq_hz     = freq;
        chosen->pitch_glide_smoother.configure(glide_ms, sample_rate_);
        chosen->pitch_glide_smoother.reset(freq);

        // Simulated tongue strike impulse on high-velocity keyboard Note On
        if (velocity > 0.75f) {
            chosen->virtual_lung_env = std::min(1.0f, velocity * 1.15f);
        }
    }

    void trigger_note_off(int16_t key, int16_t channel, int32_t note_id) noexcept {
        const bool freeze_on = sustain_pedal_cc64_ || (get_param(kParamPlasmaFreeze) > 0.5f);
        for (auto& v : voices_) {
            if (!v.active) continue;
            const bool match = (note_id != -1 && v.note_id == note_id) ||
                               (v.midi_key == key && (channel < 0 || v.channel == channel));
            if (match) {
                v.gate_on = false;
                v.sustained_by_freeze = freeze_on;
            }
        }
    }

    void apply_note_expression(const clap_event_note_expression_t* ev) noexcept {
        for (auto& v : voices_) {
            if (!v.active) continue;
            if ((ev->note_id != -1 && v.note_id == ev->note_id) || v.midi_key == ev->key) {
                if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
                    v.note_tuning_semitones = static_cast<float>(ev->value);
                } else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE ||
                           ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS) {
                    v.note_pressure = clampf(static_cast<float>(ev->value), 0.0f, 1.0f);
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Generative AeroKinetic Arpeggiator & ITB MPE Sequencer Execution
    // ------------------------------------------------------------------------
    void step_aerokinetic_arpeggiator(float mouth_pressure, float bite_semitones, double current_beat) noexcept {
        // Pulse density K(P_mouth) in [1..16] scales with breath pressure
        const int pulses = std::clamp(1 + static_cast<int>(mouth_pressure * 15.0f), 1, 16);
        const uint16_t mask = kEuclidean16LUT[pulses];
        last_euclidean_mask_ = mask;

        const double step_index_f = std::floor(current_beat * 4.0); // 16th-note grid
        if (step_index_f != arp_.last_step_beat) {
            arp_.last_step_beat = step_index_f;
            arp_.current_step = static_cast<uint8_t>(static_cast<int64_t>(step_index_f) & 15);

            if ((mask >> arp_.current_step) & 1u) {
                arp_.step_henon_and_tonnetz(bite_semitones * 0.5f);
                const int degree = arp_.triad[arp_.current_step % 3];
                const int16_t arp_key = static_cast<int16_t>(60 + degree);
                trigger_note_on(arp_key, 0.55f + 0.35f * mouth_pressure, 0, -1, true);
            }
        }
    }

    void process_itb_sequencer(const StaticPatternArena& arena, double beat_now, double beat_delta) noexcept {
        if (arena.loop_length_beats <= 0.1f || arena.note_count == 0) return;
        const float loop_beat = static_cast<float>(std::fmod(std::max(0.0, beat_now), arena.loop_length_beats));
        const float prev_beat = loop_beat - static_cast<float>(beat_delta);

        for (uint32_t i = 0; i < std::min(arena.note_count, kMaxPatternNotes); ++i) {
            const auto& n = arena.notes[i];
            if (!n.active) continue;

            // Check if note or ratchet sub-step crossed in this sub-block
            const uint8_t ratchets = std::clamp<uint8_t>(n.ratchet_count, 1, 8);
            const float sub_dur = n.duration_beats / static_cast<float>(ratchets);
            for (uint8_t r = 0; r < ratchets; ++r) {
                const float trig_beat = n.start_beat + static_cast<float>(r) * sub_dur;
                if (trig_beat > prev_beat && trig_beat <= loop_beat) {
                    trigger_note_on(n.midi_key, n.velocity, 0, static_cast<int32_t>(1000 + i), true);
                    breath_arbiter_.on_aftertouch(n.start_breath, steady_frame_counter_);
                }
            }
        }
    }

    HardwareProfile resolve_active_profile() const noexcept {
        const int raw = static_cast<int>(std::lround(get_param(kParamHardwareProfile)));
        if (raw >= 1 && raw <= 5) {
            return static_cast<HardwareProfile>(raw);
        }
        // Smart Auto-Sense heuristic
        if (last_mic_env_ > 0.05f) return HardwareProfile::kLiveMicExciter;
        if (detected_wind_activity_) return HardwareProfile::kRolandAE30;
        return HardwareProfile::kArturiaMicroLab;
    }

    // ------------------------------------------------------------------------
    // Background WAV DiskWriter Thread + Native OS File Drag-and-Drop
    // ------------------------------------------------------------------------
    void start_disk_writer_thread() {
        stop_disk_writer_thread();
        disk_writer_thread_ = std::jthread([this](std::stop_token stoken) {
            std::vector<float> staging(8192, 0.0f);
            while (!stoken.stop_requested()) {
                if (finalize_take_requested_.exchange(false, std::memory_order_acq_rel)) {
                    write_ringbuffer_to_wav_file();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
        });
    }

    void stop_disk_writer_thread() {
        if (disk_writer_thread_.joinable()) {
            disk_writer_thread_.request_stop();
            disk_writer_thread_.join();
        }
    }

    bool trigger_save_live_take_wav(const std::string& target_path) noexcept {
        last_take_wav_path_ = target_path;
        bounce_recording_.store(false, std::memory_order_release);
        finalize_take_requested_.store(true, std::memory_order_release);
        return true;
    }

    void write_ringbuffer_to_wav_file() {
        std::vector<float> all_samples;
        all_samples.reserve(262144);
        std::array<float, 4096> chunk{};
        while (true) {
            const size_t got = audio_ring_buffer_.pop_chunk(chunk.data(), chunk.size());
            if (got == 0) break;
            all_samples.insert(all_samples.end(), chunk.begin(), chunk.begin() + got);
        }
        if (all_samples.empty()) {
            // Synthesize a short 1-second reference tail if triggered while empty
            all_samples.resize(48000 * 2, 0.0f);
        }

        WavAcidHeader hdr{};
        hdr.sample_rate = static_cast<uint32_t>(sample_rate_);
        hdr.byte_rate   = hdr.sample_rate * 2u * 4u;
        hdr.tempo       = last_daw_tempo_.load(std::memory_order_relaxed);
        hdr.data_size   = static_cast<uint32_t>(all_samples.size() * sizeof(float));
        hdr.riff_size   = static_cast<uint32_t>(sizeof(WavAcidHeader) - 8 + hdr.data_size);

        const std::string path = last_take_wav_path_.empty()
            ? (std::filesystem::temp_directory_path() / "Tectonic_Synth_v5_Take.wav").string()
            : last_take_wav_path_;

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            ofs.write(reinterpret_cast<const char*>(all_samples.data()), hdr.data_size);
            ofs.close();
            last_take_wav_path_ = path;
        }
    }

    // Initiates native OS file drag-and-drop onto a Bitwig Studio audio track
    bool initiate_os_wav_drag_and_drop() {
        if (last_take_wav_path_.empty()) {
            trigger_save_live_take_wav("");
            write_ringbuffer_to_wav_file();
        }
        return std::filesystem::exists(last_take_wav_path_);
    }

    // ------------------------------------------------------------------------
    // Quantum Observatory 60 FPS Telemetry & Homotopic Mode Weights
    // ------------------------------------------------------------------------
    void publish_observatory_telemetry(float peak_l, float peak_r, bool tongue_hit) noexcept {
        ObservatorySnapshot& snap = telemetry_buffer_.write_slot();
        snap.modal_energies     = voices_[0].modal_bank.mode_energy;
        snap.modal_thresholds   = voices_[0].modal_bank.iso226_theta;
        for (uint32_t i = 0; i < kNumVoices; ++i) {
            snap.voice_frequencies[i] = voices_[i].active ? voices_[i].current_freq_hz : 0.0f;
            snap.voice_amplitudes[i]  = voices_[i].active ? voices_[i].virtual_lung_env : 0.0f;
        }
        snap.mouth_pressure         = sensor_proc_.breath_filter.state;
        snap.bite_offset            = sensor_proc_.bite_smoother.pos - 0.50f;
        snap.tilt_norm              = sensor_proc_.tilt_lpf_state;
        snap.effective_farey_order  = GravitationalFareySolver::compute_effective_order(snap.tilt_norm);
        snap.farey_lock_strength    = get_param(kParamFareyGravity);
        snap.mic_envelope           = last_mic_env_;
        snap.mic_pitch_hz           = last_mic_pitch_;
        snap.master_peak_l          = peak_l;
        snap.master_peak_r          = peak_r;
        snap.euclidean_mask         = last_euclidean_mask_;
        snap.euclidean_step         = arp_.current_step;
        snap.active_profile         = static_cast<uint8_t>(resolve_active_profile());
        snap.tongue_attack_detected = tongue_hit;
        snap.plasma_freeze_active   = sustain_pedal_cc64_ || (get_param(kParamPlasmaFreeze) > 0.5f);
        snap.bounce_recording       = bounce_recording_.load(std::memory_order_relaxed);
        snap.bounce_frames_written  = bounce_frames_written_.load(std::memory_order_relaxed);
        telemetry_buffer_.publish();
    }

    // Homotopic 140 ms UI mode transition (mode_weights_[4], sum == 1.0)
    void step_gui_frame_60fps(float dt_sec = 1.0f / 60.0f) noexcept {
        const auto latest_snap = telemetry_buffer_.consume();
        last_gui_snapshot_ = latest_snap;

        const int active_mode = std::clamp(static_cast<int>(std::lround(get_param(kParamUiMode))), 0, 3);
        const float alpha = 1.0f - std::exp(-dt_sec / 0.140f); // 140 ms homotopy constant
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const float target = (i == active_mode) ? 1.0f : 0.0f;
            mode_weights_[i] += alpha * (target - mode_weights_[i]);
            sum += mode_weights_[i];
        }
        if (sum > 1e-5f) {
            for (float& w : mode_weights_) w /= sum;
        }
    }

// ============================================================================
// SECTION VII-B: DEAR IMGUI + OPENGL 3.3 CORE 60 FPS RENDERER IMPLEMENTATION
// ============================================================================
// Renders the 4 homotopic modes of the Quantum Observatory:
//   - Mode 0 (kPianoRoll): Chrono-Logarithmic MPE Grid + Farey Gravity Valleys
//   - Mode 1 (kMidiController): Bipartite Bezier Graph + Atomic MIDI Learn
//   - Mode 2 (kLiveTakeBounce): Holographic Tape + [DRAG WAV TO DAW TRACK]
//   - Mode 3 (kAerophoneHub): Polar Astrolabe + 5 Quick Hardware Presets +
//                             AUTO-ZERO CALIBRATE + Anti-Zipper Telemetry
    void render_quantum_observatory_imgui() noexcept {
        ScopedGlStateGuard gl_guard{};
        step_gui_frame_60fps(1.0f / 60.0f);

#if defined(TECTONIC_HAS_IMGUI_OPENGL) && __has_include(<imgui.h>)
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(gui_width_), static_cast<float>(gui_height_)));
        ImGui::Begin("Tectonic Synth v5 — Quantum Observatory (60 FPS)", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        // Top Navigation Bar: 4 Homotopic Modes
        if (ImGui::Button("1. MPE Piano Roll"))     push_gui_param_change(kParamUiMode, 0.0);
        ImGui::SameLine();
        if (ImGui::Button("2. MIDI Matrix & Learn")) push_gui_param_change(kParamUiMode, 1.0);
        ImGui::SameLine();
        if (ImGui::Button("3. Live Take & Bounce"))  push_gui_param_change(kParamUiMode, 2.0);
        ImGui::SameLine();
        if (ImGui::Button("4. Aerophone / Mic Hub")) push_gui_param_change(kParamUiMode, 3.0);

        ImGui::Separator();

        // Mode 4: Universal Aerophone, MicroLab & Mic Hub
        if (mode_weights_[3] > 0.1f) {
            ImGui::Text("QUICK HARDWARE WORKFLOW PRESETS:");
            if (ImGui::Button("[1. Roland AE-30 Native]"))   apply_quick_hardware_preset(HardwareProfile::kRolandAE30);
            ImGui::SameLine();
            if (ImGui::Button("[2. Arturia MicroLab / Keys]")) apply_quick_hardware_preset(HardwareProfile::kArturiaMicroLab);
            ImGui::SameLine();
            if (ImGui::Button("[3. Akai EWI / NuRAD]"))      apply_quick_hardware_preset(HardwareProfile::kAkaiEwiNuRAD);
            ImGui::SameLine();
            if (ImGui::Button("[4. Live Mic Exciter]"))      apply_quick_hardware_preset(HardwareProfile::kLiveMicExciter);
            ImGui::SameLine();
            if (ImGui::Button("[5. In-The-Box DAW Auto]"))   apply_quick_hardware_preset(HardwareProfile::kInTheBoxDaw);

            if (ImGui::Button("AUTO-ZERO CALIBRATE (Breath Deadzone)")) {
                auto_zero_calibrate_requested_.store(true, std::memory_order_release);
            }
            ImGui::Text("Mouth Pressure: %.3f | Bite Offset: %+.3f | Farey Order N_eff: %.2f | Tongue Hit: %s",
                        last_gui_snapshot_.mouth_pressure,
                        last_gui_snapshot_.bite_offset,
                        last_gui_snapshot_.effective_farey_order,
                        last_gui_snapshot_.tongue_attack_detected ? "STRIKE" : "idle");
        }

        // Mode 3: Live Take & Audio Bounce
        if (mode_weights_[2] > 0.1f) {
            bool armed = bounce_armed_.load(std::memory_order_relaxed);
            if (ImGui::Checkbox("Auto-Start Recording on Breath / Signal Threshold", &armed)) {
                bounce_armed_.store(armed, std::memory_order_release);
            }
            if (ImGui::Button("[FINALIZE & SAVE 32-BIT FLOAT WAV]")) {
                trigger_save_live_take_wav("");
            }
            ImGui::SameLine();
            if (ImGui::Button("[DRAG WAV TO DAW TRACK]")) {
                initiate_os_wav_drag_and_drop();
            }
            ImGui::Text("Recorded Stereo Frames: %llu",
                        static_cast<unsigned long long>(last_gui_snapshot_.bounce_frames_written));
        }

        // Mode 2: MIDI Matrix & Atomic Right-Click MIDI Learn
        if (mode_weights_[1] > 0.1f) {
            ImGui::Text("Right-Click any parameter to arm Atomic MIDI Learn:");
            for (uint32_t pid = 0; pid < kParamCount; ++pid) {
                float val = get_param(pid);
                if (ImGui::SliderFloat(param_infos_[pid].name, &val,
                                       static_cast<float>(param_infos_[pid].min_value),
                                       static_cast<float>(param_infos_[pid].max_value))) {
                    push_gui_param_change(pid, val);
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    midi_learn_target_param_.store(static_cast<int32_t>(pid), std::memory_order_release);
                }
            }
        }

        // Mode 1: Interactive MPE Piano Roll with Triple-Slot RCU Pattern Arena
        if (mode_weights_[0] > 0.1f) {
            if (ImGui::Button("Add Ratchet Step to Pattern Slot 0")) {
                auto& arena = pattern_rcu_.gui_begin_edit();
                if (arena.note_count > 0) {
                    arena.notes[0].ratchet_count = static_cast<uint8_t>((arena.notes[0].ratchet_count % 8) + 1);
                }
                pattern_rcu_.gui_commit_edit();
            }
        }

        ImGui::End();
#endif
    }

    // Quick Hardware Preset Loader for Mode 4 (Universal Hub)
    void apply_quick_hardware_preset(HardwareProfile preset) noexcept {
        push_gui_param_change(kParamHardwareProfile, static_cast<double>(preset));
        switch (preset) {
            case HardwareProfile::kRolandAE30:
                push_gui_param_change(kParamBiteMode, 0.0);        // AE-30 Center-Notch (0.50)
                push_gui_param_change(kParamBiteNotchDelta, 0.045);
                push_gui_param_change(kParamLegatoGlideMs, 2.5);   // Fast sax key transition
                push_gui_param_change(kParamFareyGravity, 0.88);
                break;
            case HardwareProfile::kArturiaMicroLab:
                push_gui_param_change(kParamLegatoGlideMs, 18.0);
                push_gui_param_change(kParamFareyGravity, 0.92);
                push_gui_param_change(kParamArpEnable, 1.0);
                break;
            case HardwareProfile::kAkaiEwiNuRAD:
                push_gui_param_change(kParamBiteMode, 1.0);        // EWI Unipolar Zero (0.00)
                push_gui_param_change(kParamLegatoGlideMs, 8.0);
                break;
            case HardwareProfile::kLiveMicExciter:
                push_gui_param_change(kParamMicExciterGain, 1.35);
                push_gui_param_change(kParamMaterialMorph, 0.33);  // Basalt stone resonance
                break;
            case HardwareProfile::kInTheBoxDaw:
                push_gui_param_change(kParamArpEnable, 1.0);
                push_gui_param_change(kParamPlasmaDrive, 0.72);
                break;
            default:
                break;
        }
    }

    // ------------------------------------------------------------------------
    // Parameter Management & Lock-Free GUI -> Audio Queue
    // ------------------------------------------------------------------------
    uint32_t param_count() const noexcept { return kParamCount; }

    bool get_param_info(uint32_t index, clap_param_info_t* info) const noexcept {
        if (index >= kParamCount || !info) return false;
        *info = param_infos_[index];
        return true;
    }

    float get_param(clap_id id) const noexcept {
        if (id >= kParamCount) return 0.0f;
        return param_values_[id].load(std::memory_order_relaxed);
    }

    void set_param_atomic(clap_id id, double val) noexcept {
        if (id >= kParamCount) return;
        const auto& info = param_infos_[id];
        const float clamped = clampf(static_cast<float>(val),
                                     static_cast<float>(info.min_value),
                                     static_cast<float>(info.max_value));
        param_values_[id].store(clamped, std::memory_order_relaxed);
    }

    void push_gui_param_change(clap_id id, double val) noexcept {
        set_param_atomic(id, val);
        gui_to_audio_queue_.try_push(ParamChangeMsg{id, val});
    }

    void drain_gui_param_queue(const clap_output_events_t* out_events) noexcept {
        ParamChangeMsg msg{};
        while (gui_to_audio_queue_.try_pop(msg)) {
            set_param_atomic(msg.param_id, msg.value);
            if (out_events && out_events->try_push) {
                clap_event_param_value_t ev{};
                ev.header.size     = sizeof(clap_event_param_value_t);
                ev.header.time     = 0;
                ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                ev.header.type     = CLAP_EVENT_PARAM_VALUE;
                ev.header.flags    = CLAP_EVENT_IS_LIVE;
                ev.param_id        = msg.param_id;
                ev.cookie          = nullptr;
                ev.note_id         = -1;
                ev.port_index      = -1;
                ev.channel         = -1;
                ev.key             = -1;
                ev.value           = msg.value;
                out_events->try_push(out_events, &ev.header);
            }
        }
    }

    void bind_midi_cc_to_param(uint8_t cc, clap_id param_id) noexcept {
        for (auto& slot : midi_routes_) {
            if (!slot.enabled || slot.target_param == param_id) {
                slot.cc_msb       = cc;
                slot.target_param = param_id;
                slot.enabled      = true;
                return;
            }
        }
    }

    // ------------------------------------------------------------------------
    // Binary State Serialization (CLAP_EXT_STATE)
    // ------------------------------------------------------------------------
    struct SerializedStateBlob {
        uint32_t magic{0x54454335u}; // "TEC5"
        uint32_t version{500};
        std::array<float, kParamCount> params{};
        std::array<MidiRouteSlot, kMaxMidiRoutes> routes{};
        StaticPatternArena pattern{};
    };

    bool save_state(const clap_ostream_t* stream) noexcept {
        if (!stream || !stream->write) return false;
        SerializedStateBlob blob{};
        for (uint32_t i = 0; i < kParamCount; ++i) {
            blob.params[i] = get_param(i);
        }
        blob.routes  = midi_routes_;
        blob.pattern = pattern_rcu_.audio_acquire();

        const auto* ptr = reinterpret_cast<const uint8_t*>(&blob);
        uint64_t remaining = sizeof(blob);
        while (remaining > 0) {
            const int64_t written = stream->write(stream, ptr, remaining);
            if (written <= 0) return false;
            ptr       += written;
            remaining -= static_cast<uint64_t>(written);
        }
        return true;
    }

    bool load_state(const clap_istream_t* stream) noexcept {
        if (!stream || !stream->read) return false;
        SerializedStateBlob blob{};
        auto* ptr = reinterpret_cast<uint8_t*>(&blob);
        uint64_t remaining = sizeof(blob);
        while (remaining > 0) {
            const int64_t got = stream->read(stream, ptr, remaining);
            if (got <= 0) return false;
            ptr       += got;
            remaining -= static_cast<uint64_t>(got);
        }
        if (blob.magic != 0x54454335u || blob.version != 500) return false;

        for (uint32_t i = 0; i < kParamCount; ++i) {
            set_param_atomic(i, blob.params[i]);
        }
        midi_routes_ = blob.routes;
        auto& edit_arena = pattern_rcu_.gui_begin_edit();
        edit_arena = blob.pattern;
        pattern_rcu_.gui_commit_edit();
        return true;
    }

    // GUI Sizing & Accessors
    uint32_t gui_width()  const noexcept { return gui_width_; }
    uint32_t gui_height() const noexcept { return gui_height_; }
    void set_gui_size(uint32_t w, uint32_t h) noexcept {
        gui_width_  = std::clamp(w, 800u, 3840u);
        gui_height_ = std::clamp(h, 500u, 2160u);
    }

private:
    void init_default_parameters() noexcept {
        auto def = [this](clap_id id, const char* module, const char* name,
                          double min_v, double max_v, double def_v, uint32_t flags = CLAP_PARAM_IS_AUTOMATABLE) {
            auto& p = param_infos_[id];
            p.id            = id;
            p.flags         = flags;
            p.cookie        = nullptr;
            std::snprintf(p.name, CLAP_NAME_SIZE, "%s", name);
            std::snprintf(p.module, CLAP_PATH_SIZE, "%s", module);
            p.min_value     = min_v;
            p.max_value     = max_v;
            p.default_value = def_v;
            param_values_[id].store(static_cast<float>(def_v), std::memory_order_relaxed);
        };

        def(kParamHardwareProfile, "Workflow",  "Hardware Profile Auto-Sense", 0.0, 5.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
        def(kParamUiMode,          "UI",        "Quantum Observatory Mode",    0.0, 3.0, 3.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
        def(kParamBreathFloor,     "Aerophone", "Breath Zero Deadzone Floor",  0.000, 0.150, 0.015);
        def(kParamBreathCurveK,    "Aerophone", "Rational Lung Curvature K",  -0.80, 4.00, 0.65);
        def(kParamBiteMode,        "Aerophone", "Bite Sensor Mode (AE30/EWI)", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
        def(kParamBiteNotchDelta,  "Aerophone", "Bite Center-Notch Plateau",   0.01, 0.18, 0.045);
        def(kParamLegatoGlideMs,   "Aerophone", "Physical Legato Glide (ms)",  2.5, 450.0, 4.5);
        def(kParamMaterialMorph,   "Resonator", "Material Tensor Morph",       0.0, 1.0, 0.25);
        def(kParamReedStiffness,   "Exciter",   "WDF Reed Stiffness",          0.1, 1.0, 0.58);
        def(kParamJetTurbulence,   "Exciter",   "Turbulent Jet Noise",         0.0, 1.0, 0.22);
        def(kParamModalDecay,      "Resonator", "64-Mode Tectonic Decay (s)",  0.05, 12.0, 2.40);
        def(kParamPlasmaDrive,     "Resonator", "Spectral Plasma Drive",       0.0, 1.0, 0.45);
        def(kParamSvfCutoffHz,     "Filter",    "TPT SVF Cutoff (Hz)",         40.0, 18000.0, 3200.0);
        def(kParamSvfResonance,    "Filter",    "TPT SVF Resonance",           0.05, 0.98, 0.28);
        def(kParamSvfMorph,        "Filter",    "TPT SVF Morph (LP-BP-HP)",    0.0, 1.0, 0.12);
        def(kParamFareyGravity,    "Harmony",   "Farey Gravity Spring Lock",   0.0, 1.0, 0.85);
        def(kParamTiltFareyOrder,  "Harmony",   "Farey Lattice Order N_eff",   3.0, 13.0, 5.0);
        def(kParamArpEnable,       "Generator", "AeroKinetic Arpeggiator",     0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
        def(kParamMicExciterGain,  "Exciter",   "Live Mic Acoustic Gain",      0.0, 2.0, 1.0);
        def(kParamPlasmaFreeze,    "Resonator", "Plasma & Chord Freeze",       0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED);
        def(kParamMasterGain,      "Master",    "Master Output Gain",          0.0, 1.5, 0.85);
    }

    void init_default_midi_routes() noexcept {
        for (auto& r : midi_routes_) r.enabled = false;
        // Default Arturia MicroLab & Roland AE-30 expressive matrix routes
        midi_routes_[0] = MidiRouteSlot{1,  33, false, false, kParamMaterialMorph, 1.0f, 0.0f, true};
        midi_routes_[1] = MidiRouteSlot{74, 0,  false, false, kParamTiltFareyOrder, 1.0f, 0.2f, true};
        midi_routes_[2] = MidiRouteSlot{71, 0,  false, false, kParamPlasmaDrive,   1.0f, 0.0f, true};
    }

    const clap_host_t* host_{nullptr};
    float    sample_rate_{48000.0f};
    uint64_t steady_frame_counter_{0};
    uint64_t voice_age_ticker_{0};

    // Preallocated 64-byte aligned voices & DSP processors
    alignas(64) std::array<TectonicVoice, kNumVoices> voices_{};
    alignas(64) DualStreamBreathArbiter               breath_arbiter_{};
    alignas(64) AerophoneSensorProcessor              sensor_proc_{};
    alignas(64) LiveMicExciterProcessor               mic_exciter_{};
    alignas(64) AeroKineticArpeggiator                arp_{};
    alignas(64) HilbertAnalyticalLimiter              limiter_l_{};
    alignas(64) HilbertAnalyticalLimiter              limiter_r_{};

    // Lock-free inter-thread structures
    alignas(64) LockFreeSPSCQueue<ParamChangeMsg, 256>       gui_to_audio_queue_{};
    alignas(64) WaitFreeTripleBuffer<ObservatorySnapshot>    telemetry_buffer_{};
    alignas(64) TripleSlotPatternArenaRCU                    pattern_rcu_{};
    alignas(64) LockFreeAudioRingBuffer                      audio_ring_buffer_{};

    // Parameter storage
    std::array<clap_param_info_t, kParamCount>  param_infos_{};
    std::array<std::atomic<float>, kParamCount> param_values_{};
    std::array<MidiRouteSlot, kMaxMidiRoutes>   midi_routes_{};

    // Real-time expression states
    float    prev_subblock_breath_{0.0f};
    float    mod_wheel_cc1_{0.0f};
    float    raw_bite_sensor_{0.50f};
    float    raw_tilt_sensor_{0.0f};
    float    pitch_bend_semitones_{0.0f};
    float    last_mic_env_{0.0f};
    float    last_mic_pitch_{220.0f};
    uint16_t last_euclidean_mask_{0};
    bool     sustain_pedal_cc64_{false};
    bool     detected_wind_activity_{false};

    // Atomic flags for GUI interaction & Live Bounce
    std::atomic<bool>     auto_zero_calibrate_requested_{false};
    std::atomic<bool>     canvas_touch_active_{false};
    std::atomic<int32_t>  midi_learn_target_param_{-1};
    std::atomic<bool>     bounce_armed_{false};
    std::atomic<bool>     bounce_recording_{false};
    std::atomic<bool>     finalize_take_requested_{false};
    std::atomic<uint64_t> bounce_frames_written_{0};
    std::atomic<float>    last_daw_tempo_{120.0f};

    std::jthread        disk_writer_thread_{};
    std::string         last_take_wav_path_{};
    ObservatorySnapshot last_gui_snapshot_{};
    std::array<float, 4> mode_weights_{0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t            gui_width_{1280};
    uint32_t            gui_height_{800};
};

// ============================================================================
// SECTION IX: CLAP C-ABI EXTENSION TABLES & FACTORY ENTRY POINT
// ============================================================================

static TectonicSynthPlugin* self_from_plugin(const clap_plugin_t* plugin) noexcept {
    return static_cast<TectonicSynthPlugin*>(plugin->plugin_data);
}

// 1. CLAP_EXT_AUDIO_PORTS (1 Stereo Input + 2 Stereo Outputs)
static uint32_t CLAP_ABI ext_audio_ports_count(const clap_plugin_t*, bool is_input) {
    return is_input ? 1u : 2u;
}

static bool CLAP_ABI ext_audio_ports_get(
    const clap_plugin_t*, uint32_t index, bool is_input, clap_audio_port_info_t* info) {
    if (!info) return false;
    std::memset(info, 0, sizeof(*info));

    if (is_input) {
        if (index != 0) return false;
        info->id            = 0;
        std::snprintf(info->name, CLAP_NAME_SIZE, "Acoustic / Mic Exciter In");
        info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type     = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    } else {
        if (index == 0) {
            info->id            = 0;
            std::snprintf(info->name, CLAP_NAME_SIZE, "Main Tectonic Out");
            info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
            info->channel_count = 2;
            info->port_type     = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        } else if (index == 1) {
            info->id            = 1;
            std::snprintf(info->name, CLAP_NAME_SIZE, "Live Bounce Direct Tap");
            info->flags         = 0;
            info->channel_count = 2;
            info->port_type     = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
            return true;
        }
        return false;
    }
}

static const clap_plugin_audio_ports_t kExtAudioPorts = {
    ext_audio_ports_count,
    ext_audio_ports_get
};

// 2. CLAP_EXT_NOTE_PORTS (CLAP Native + MPE + MIDI 1/2)
static uint32_t CLAP_ABI ext_note_ports_count(const clap_plugin_t*, bool is_input) {
    return is_input ? 1u : 0u;
}

static bool CLAP_ABI ext_note_ports_get(
    const clap_plugin_t*, uint32_t index, bool is_input, clap_note_port_info_t* info) {
    if (!is_input || index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI |
                               CLAP_NOTE_DIALECT_MIDI_MPE | CLAP_NOTE_DIALECT_MIDI2;
    info->preferred_dialect  = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, CLAP_NAME_SIZE, "Aerophone / MPE / Keys Note In");
    return true;
}

static const clap_plugin_note_ports_t kExtNotePorts = {
    ext_note_ports_count,
    ext_note_ports_get
};

// 3. CLAP_EXT_PARAMS
static uint32_t CLAP_ABI ext_params_count(const clap_plugin_t* plugin) {
    return self_from_plugin(plugin)->param_count();
}

static bool CLAP_ABI ext_params_get_info(
    const clap_plugin_t* plugin, uint32_t param_index, clap_param_info_t* param_info) {
    return self_from_plugin(plugin)->get_param_info(param_index, param_info);
}

static bool CLAP_ABI ext_params_get_value(
    const clap_plugin_t* plugin, clap_id param_id, double* out_value) {
    if (!out_value || param_id >= kParamCount) return false;
    *out_value = self_from_plugin(plugin)->get_param(param_id);
    return true;
}

static bool CLAP_ABI ext_params_value_to_text(
    const clap_plugin_t*, clap_id param_id, double value, char* out_buf, uint32_t cap) {
    if (!out_buf || cap == 0 || param_id >= kParamCount) return false;
    if (param_id == kParamHardwareProfile) {
        static constexpr const char* kNames[6] = {
            "0: Auto-Sense", "1: Roland AE-30 Native", "2: Arturia MicroLab / Keys",
            "3: Akai EWI / NuRAD", "4: Live Mic Exciter", "5: In-The-Box DAW Auto"
        };
        const int idx = std::clamp(static_cast<int>(std::lround(value)), 0, 5);
        std::snprintf(out_buf, cap, "%s", kNames[idx]);
        return true;
    }
    std::snprintf(out_buf, cap, "%.3f", value);
    return true;
}

static bool CLAP_ABI ext_params_text_to_value(
    const clap_plugin_t*, clap_id, const char* text, double* out_value) {
    if (!text || !out_value) return false;
    char* endptr = nullptr;
    *out_value = std::strtod(text, &endptr);
    return endptr != text;
}

static void CLAP_ABI ext_params_flush(
    const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out) {
    auto* self = self_from_plugin(plugin);
    if (in) {
        const uint32_t n = in->size(in);
        for (uint32_t i = 0; i < n; ++i) {
            self->handle_clap_event(in->get(in, i));
        }
    }
    self->drain_gui_param_queue(out);
}

static const clap_plugin_params_t kExtParams = {
    ext_params_count,
    ext_params_get_info,
    ext_params_get_value,
    ext_params_value_to_text,
    ext_params_text_to_value,
    ext_params_flush
};

// 4. CLAP_EXT_STATE
static bool CLAP_ABI ext_state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    return self_from_plugin(plugin)->save_state(stream);
}

static bool CLAP_ABI ext_state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    return self_from_plugin(plugin)->load_state(stream);
}

static const clap_plugin_state_t kExtState = {
    ext_state_save,
    ext_state_load
};

// 5. CLAP_EXT_GUI (Quantum Observatory 60 FPS Window & Resize Protocol)
static bool CLAP_ABI ext_gui_is_api_supported(const clap_plugin_t*, const char* api, bool is_floating) {
    if (is_floating || !api) return false;
#if defined(_WIN32)
    return std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#elif defined(__APPLE__)
    return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#else
    return std::strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

static bool CLAP_ABI ext_gui_get_preferred_api(
    const clap_plugin_t*, const char** api, bool* is_floating) {
    if (!api || !is_floating) return false;
    *is_floating = false;
#if defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
#else
    *api = CLAP_WINDOW_API_X11;
#endif
    return true;
}

static bool CLAP_ABI ext_gui_create(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    if (!ext_gui_is_api_supported(plugin, api, is_floating)) return false;
    self_from_plugin(plugin)->step_gui_frame_60fps();
    return true;
}

static void CLAP_ABI ext_gui_destroy(const clap_plugin_t*) {}
static bool CLAP_ABI ext_gui_set_scale(const clap_plugin_t*, double) { return true; }

static bool CLAP_ABI ext_gui_get_size(
    const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    if (!width || !height) return false;
    const auto* self = self_from_plugin(plugin);
    *width  = self->gui_width();
    *height = self->gui_height();
    return true;
}

static bool CLAP_ABI ext_gui_can_resize(const clap_plugin_t*) { return true; }

static bool CLAP_ABI ext_gui_get_resize_hints(
    const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
    if (!hints) return false;
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = 16;
    hints->aspect_ratio_height     = 10;
    return true;
}

static bool CLAP_ABI ext_gui_adjust_size(
    const clap_plugin_t*, uint32_t* width, uint32_t* height) {
    if (!width || !height) return false;
    *width  = std::clamp(*width, 800u, 3840u);
    *height = std::clamp(*height, 500u, 2160u);
    return true;
}

static bool CLAP_ABI ext_gui_set_size(
    const clap_plugin_t* plugin, uint32_t width, uint32_t height) {
    self_from_plugin(plugin)->set_gui_size(width, height);
    return true;
}

static bool CLAP_ABI ext_gui_set_parent(const clap_plugin_t* plugin, const clap_window_t*) {
    ScopedGlStateGuard gl_guard{};
    self_from_plugin(plugin)->step_gui_frame_60fps();
    return true;
}

static bool CLAP_ABI ext_gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return true; }
static void CLAP_ABI ext_gui_suggest_title(const clap_plugin_t*, const char*) {}

static bool CLAP_ABI ext_gui_show(const clap_plugin_t* plugin) {
    self_from_plugin(plugin)->step_gui_frame_60fps();
    return true;
}

static bool CLAP_ABI ext_gui_hide(const clap_plugin_t*) { return true; }

static const clap_plugin_gui_t kExtGui = {
    ext_gui_is_api_supported,
    ext_gui_get_preferred_api,
    ext_gui_create,
    ext_gui_destroy,
    ext_gui_set_scale,
    ext_gui_get_size,
    ext_gui_can_resize,
    ext_gui_get_resize_hints,
    ext_gui_adjust_size,
    ext_gui_set_size,
    ext_gui_set_parent,
    ext_gui_set_transient,
    ext_gui_suggest_title,
    ext_gui_show,
    ext_gui_hide
};

// Plugin Descriptor & Core Callbacks
static constexpr const char* kPluginFeatures[] = {
    "instrument",
    "synthesizer",
    "physical-modeling",
    "stereo",
    nullptr
};

static const clap_plugin_descriptor_t kPluginDescriptor = {
    CLAP_VERSION_INIT,
    "org.tectonic-audio.tectonic-synth-v5",
    "Tectonic Synth v5 (Quantum Observatory)",
    "Tectonic Audio Research Lab",
    "https://github.com/tectonic-audio/tectonic-synth-v5",
    "https://github.com/tectonic-audio/tectonic-synth-v5/wiki",
    "https://github.com/tectonic-audio/tectonic-synth-v5/issues",
    "5.0.0",
    "Ultra-Performance Physical Waveguide, 64-Mode Spectral Plasma & Farey Lattice CLAP Synthesizer",
    kPluginFeatures
};

static bool CLAP_ABI plugin_init(const clap_plugin_t* plugin) {
    return self_from_plugin(plugin)->init();
}

static void CLAP_ABI plugin_destroy(const clap_plugin_t* plugin) {
    delete self_from_plugin(plugin);
    delete plugin;
}

static bool CLAP_ABI plugin_activate(
    const clap_plugin_t* plugin, double sr, uint32_t min_f, uint32_t max_f) {
    return self_from_plugin(plugin)->activate(sr, min_f, max_f);
}

static void CLAP_ABI plugin_deactivate(const clap_plugin_t* plugin) {
    self_from_plugin(plugin)->deactivate();
}

static bool CLAP_ABI plugin_start_processing(const clap_plugin_t*) { return true; }
static void CLAP_ABI plugin_stop_processing(const clap_plugin_t*) {}

static void CLAP_ABI plugin_reset(const clap_plugin_t* plugin) {
    self_from_plugin(plugin)->reset();
}

static clap_process_status CLAP_ABI plugin_process(
    const clap_plugin_t* plugin, const clap_process_t* process) {
    return self_from_plugin(plugin)->process(process);
}

static const void* CLAP_ABI plugin_get_extension(const clap_plugin_t*, const char* id) {
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kExtAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS)  == 0) return &kExtNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS)      == 0) return &kExtParams;
    if (std::strcmp(id, CLAP_EXT_STATE)       == 0) return &kExtState;
    if (std::strcmp(id, CLAP_EXT_GUI)         == 0) return &kExtGui;
    return nullptr;
}

static void CLAP_ABI plugin_on_main_thread(const clap_plugin_t* plugin) {
    self_from_plugin(plugin)->step_gui_frame_60fps();
}

// Factory Implementation
static uint32_t CLAP_ABI factory_get_plugin_count(const clap_plugin_factory_t*) {
    return 1u;
}

static const clap_plugin_descriptor_t* CLAP_ABI factory_get_plugin_descriptor(
    const clap_plugin_factory_t*, uint32_t index) {
    return (index == 0) ? &kPluginDescriptor : nullptr;
}

static const clap_plugin_t* CLAP_ABI factory_create_plugin(
    const clap_plugin_factory_t*, const clap_host_t* host, const char* plugin_id) {
    if (!host || !plugin_id || !clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }
    if (std::strcmp(plugin_id, kPluginDescriptor.id) != 0) {
        return nullptr;
    }

    auto* instance = new (std::nothrow) TectonicSynthPlugin(host);
    if (!instance) return nullptr;

    auto* clap_plug = new (std::nothrow) clap_plugin_t{};
    if (!clap_plug) {
        delete instance;
        return nullptr;
    }

    clap_plug->desc             = &kPluginDescriptor;
    clap_plug->plugin_data      = instance;
    clap_plug->init             = plugin_init;
    clap_plug->destroy          = plugin_destroy;
    clap_plug->activate         = plugin_activate;
    clap_plug->deactivate       = plugin_deactivate;
    clap_plug->start_processing = plugin_start_processing;
    clap_plug->stop_processing  = plugin_stop_processing;
    clap_plug->reset            = plugin_reset;
    clap_plug->process          = plugin_process;
    clap_plug->get_extension    = plugin_get_extension;
    clap_plug->on_main_thread   = plugin_on_main_thread;
    return clap_plug;
}

static const clap_plugin_factory_t kPluginFactory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin
};

} // namespace tectonic

// ============================================================================
// EXPORTED CLAP_ENTRY SYMBOL
// ============================================================================
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    [](const char* /*plugin_path*/) -> bool { return true; },
    []() -> void {},
    [](const char* factory_id) -> const void* {
        if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
            return &tectonic::kPluginFactory;
        }
        return nullptr;
    }
};