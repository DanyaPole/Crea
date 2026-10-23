#include "clap/clap.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr char kPluginId[] = "com.experimental.tectonic-synth";
constexpr char kPluginName[] = "Tectonic Synth";
constexpr char kVendor[] = "Experimental";
constexpr char kUrl[] = "https://example.invalid/tectonic";
constexpr char kManualUrl[] = "https://example.invalid/tectonic/manual";
constexpr char kSupportUrl[] = "https://example.invalid/tectonic/support";
constexpr char kVersion[] = "4.0.1";
constexpr char kDescription[] = "A minimal CLAP synthesizer plugin for Tectonic.";

constexpr const char *kFeatures[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

enum : clap_id {
    kParamGain = 0,
    kParamTune = 1,
};

struct TectonicPlugin {
    clap_plugin_t plugin;
    clap_plugin_descriptor_t descriptor;
    clap_plugin_params_t params;
    clap_plugin_audio_ports_t audio_ports;
    clap_plugin_note_ports_t note_ports;
    clap_plugin_voice_info_t voice_info;
    clap_plugin_state_t state;
    clap_plugin_gui_t gui;

    bool active = false;
    double sample_rate = 44100.0;
    uint32_t min_frames = 0;
    uint32_t max_frames = 0;
    double gain = 0.7;
    double tune = 0.0;
};

TectonicPlugin *as_plugin(const clap_plugin_t *plugin) {
    return reinterpret_cast<TectonicPlugin *>(const_cast<clap_plugin_t *>(plugin));
}

uint32_t clap_plugin_params_count(const clap_plugin_t *plugin) {
    (void)plugin;
    return 2;
}

bool clap_plugin_params_get_info(const clap_plugin_t *plugin, uint32_t param_index, clap_param_info_t *param_info) {
    if (param_info == nullptr) {
        return false;
    }

    if (param_index >= 2) {
        return false;
    }

    const TectonicPlugin *self = as_plugin(plugin);
    if (self == nullptr) {
        return false;
    }

    const char *name = (param_index == kParamGain) ? "Gain" : "Tune";
    const double min_value = (param_index == kParamGain) ? 0.0 : -12.0;
    const double max_value = (param_index == kParamGain) ? 1.0 : 12.0;
    const double default_value = (param_index == kParamGain) ? 0.7 : 0.0;
    const clap_id id = (param_index == kParamGain) ? kParamGain : kParamTune;
    const clap_param_info_flags flags = CLAP_PARAM_IS_AUTOMATABLE;

    std::memset(param_info, 0, sizeof(*param_info));
    param_info->id = id;
    param_info->flags = flags;
    param_info->cookie = nullptr;
    std::snprintf(param_info->name, sizeof(param_info->name), "%s", name);
    std::snprintf(param_info->module, sizeof(param_info->module), "%s", "Tectonic");
    param_info->min_value = min_value;
    param_info->max_value = max_value;
    param_info->default_value = default_value;
    return true;
}

bool clap_plugin_params_get_value(const clap_plugin_t *plugin, clap_id param_id, double *out_value) {
    if (out_value == nullptr) {
        return false;
    }

    TectonicPlugin *self = as_plugin(plugin);
    switch (param_id) {
        case kParamGain:
            *out_value = self->gain;
            return true;
        case kParamTune:
            *out_value = self->tune;
            return true;
        default:
            return false;
    }
}

bool clap_plugin_params_value_to_text(const clap_plugin_t *plugin, clap_id param_id, double value, char *out_buffer, uint32_t out_buffer_capacity) {
    if (out_buffer == nullptr || out_buffer_capacity == 0) {
        return false;
    }

    (void)plugin;
    if (param_id == kParamGain) {
        std::snprintf(out_buffer, out_buffer_capacity, "%.3f", value);
        return true;
    }
    if (param_id == kParamTune) {
        std::snprintf(out_buffer, out_buffer_capacity, "%.2f semitones", value);
        return true;
    }
    return false;
}

bool clap_plugin_params_text_to_value(const clap_plugin_t *plugin, clap_id param_id, const char *param_value_text, double *out_value) {
    if (param_value_text == nullptr || out_value == nullptr) {
        return false;
    }

    char *parse_end = nullptr;
    const double parsed = std::strtod(param_value_text, &parse_end);
    if (parse_end == param_value_text) {
        return false;
    }

    switch (param_id) {
        case kParamGain:
            if (parsed < 0.0 || parsed > 1.0) {
                return false;
            }
            *out_value = parsed;
            return true;
        case kParamTune:
            *out_value = parsed;
            return true;
        default:
            return false;
    }
}

void clap_plugin_params_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
    (void)plugin;
    (void)in;
    (void)out;
}

uint32_t clap_plugin_audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
    (void)plugin;
    return is_input ? 0U : 1U;
}

bool clap_plugin_audio_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input, clap_audio_port_info_t *info) {
    if (info == nullptr) {
        return false;
    }

    if (is_input || index != 0U) {
        return false;
    }

    (void)plugin;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", "Main Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

uint32_t clap_plugin_note_ports_count(const clap_plugin_t *plugin, bool is_input) {
    (void)plugin;
    return is_input ? 1U : 0U;
}

bool clap_plugin_note_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input, clap_note_port_info_t *info) {
    if (info == nullptr || !is_input || index != 0U) {
        return false;
    }

    (void)plugin;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "Midi Input");
    return true;
}

bool clap_plugin_voice_info_get(const clap_plugin_t *plugin, clap_voice_info_t *info) {
    if (info == nullptr) {
        return false;
    }

    (void)plugin;
    info->voice_count = 64;
    info->voice_capacity = 64;
    info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES;
    return true;
}

bool clap_plugin_state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
    if (stream == nullptr || stream->write == nullptr) {
        return false;
    }

    TectonicPlugin *self = as_plugin(plugin);
    const double payload[2] = {self->gain, self->tune};
    const int64_t written = stream->write(stream, payload, sizeof(payload));
    return written == static_cast<int64_t>(sizeof(payload));
}

bool clap_plugin_state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
    if (stream == nullptr || stream->read == nullptr) {
        return false;
    }

    TectonicPlugin *self = as_plugin(plugin);
    double payload[2] = {0.0, 0.0};
    const int64_t read = stream->read(stream, payload, sizeof(payload));
    if (read != static_cast<int64_t>(sizeof(payload))) {
        return false;
    }

    self->gain = payload[0];
    self->tune = payload[1];
    return true;
}

bool clap_plugin_gui_is_api_supported(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    (void)plugin;
    (void)api;
    (void)is_floating;
    return false;
}

bool clap_plugin_gui_get_preferred_api(const clap_plugin_t *plugin, const char **api, bool *is_floating) {
    (void)plugin;
    (void)api;
    (void)is_floating;
    return false;
}

bool clap_plugin_gui_create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    (void)plugin;
    (void)api;
    (void)is_floating;
    return false;
}

void clap_plugin_gui_destroy(const clap_plugin_t *plugin) {
    (void)plugin;
}

bool clap_plugin_gui_set_scale(const clap_plugin_t *plugin, double scale) {
    (void)plugin;
    (void)scale;
    return false;
}

bool clap_plugin_gui_get_size(const clap_plugin_t *plugin, uint32_t *width, uint32_t *height) {
    (void)plugin;
    (void)width;
    (void)height;
    return false;
}

bool clap_plugin_gui_can_resize(const clap_plugin_t *plugin) {
    (void)plugin;
    return false;
}

bool clap_plugin_gui_set_size(const clap_plugin_t *plugin, uint32_t width, uint32_t height) {
    (void)plugin;
    (void)width;
    (void)height;
    return false;
}

bool clap_plugin_gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
    (void)plugin;
    (void)window;
    return false;
}

bool clap_plugin_gui_set_transient(const clap_plugin_t *plugin, const clap_window_t *window) {
    (void)plugin;
    (void)window;
    return false;
}

void clap_plugin_gui_suggest_title(const clap_plugin_t *plugin, const char *title) {
    (void)plugin;
    (void)title;
}

bool clap_plugin_gui_show(const clap_plugin_t *plugin) {
    (void)plugin;
    return false;
}

bool clap_plugin_gui_hide(const clap_plugin_t *plugin) {
    (void)plugin;
    return false;
}

bool clap_plugin_init(const clap_plugin_t *plugin) {
    (void)plugin;
    return true;
}

void clap_plugin_destroy(const clap_plugin_t *plugin) {
    delete as_plugin(plugin);
}

bool clap_plugin_activate(const clap_plugin_t *plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) {
    TectonicPlugin *self = as_plugin(plugin);
    self->active = true;
    self->sample_rate = sample_rate;
    self->min_frames = min_frames_count;
    self->max_frames = max_frames_count;
    return true;
}

void clap_plugin_deactivate(const clap_plugin_t *plugin) {
    TectonicPlugin *self = as_plugin(plugin);
    self->active = false;
}

bool clap_plugin_start_processing(const clap_plugin_t *plugin) {
    (void)plugin;
    return true;
}

void clap_plugin_stop_processing(const clap_plugin_t *plugin) {
    (void)plugin;
}

void clap_plugin_reset(const clap_plugin_t *plugin) {
    (void)plugin;
}

clap_process_status clap_plugin_process(const clap_plugin_t *plugin, const clap_process_t *process) {
    (void)plugin;
    (void)process;
    return CLAP_PROCESS_CONTINUE;
}

const void *clap_plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &as_plugin(plugin)->params;
    }
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &as_plugin(plugin)->audio_ports;
    }
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &as_plugin(plugin)->note_ports;
    }
    if (std::strcmp(id, CLAP_EXT_VOICE_INFO) == 0) {
        return &as_plugin(plugin)->voice_info;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &as_plugin(plugin)->state;
    }
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &as_plugin(plugin)->gui;
    }
    return nullptr;
}

void clap_plugin_on_main_thread(const clap_plugin_t *plugin) {
    (void)plugin;
}

uint32_t clap_plugin_factory_get_plugin_count(const clap_plugin_factory *factory) {
    (void)factory;
    return 1U;
}

const clap_plugin_descriptor_t *clap_plugin_factory_get_plugin_descriptor(const clap_plugin_factory *factory, uint32_t index) {
    static const clap_plugin_descriptor_t descriptor = {
        CLAP_VERSION_INIT,
        kPluginId,
        kPluginName,
        kVendor,
        kUrl,
        kManualUrl,
        kSupportUrl,
        kVersion,
        kDescription,
        kFeatures,
    };

    (void)factory;
    if (index != 0U) {
        return nullptr;
    }
    return &descriptor;
}

const clap_plugin_t *clap_plugin_factory_create_plugin(const clap_plugin_factory *factory, const clap_host_t *host, const char *plugin_id) {
    (void)factory;
    (void)host;
    if (std::strcmp(plugin_id, kPluginId) != 0) {
        return nullptr;
    }

    auto *plugin = new TectonicPlugin();
    plugin->descriptor = {
        CLAP_VERSION_INIT,
        kPluginId,
        kPluginName,
        kVendor,
        kUrl,
        kManualUrl,
        kSupportUrl,
        kVersion,
        kDescription,
        kFeatures,
    };

    plugin->plugin.desc = &plugin->descriptor;
    plugin->plugin.plugin_data = plugin;
    plugin->plugin.init = clap_plugin_init;
    plugin->plugin.destroy = clap_plugin_destroy;
    plugin->plugin.activate = clap_plugin_activate;
    plugin->plugin.deactivate = clap_plugin_deactivate;
    plugin->plugin.start_processing = clap_plugin_start_processing;
    plugin->plugin.stop_processing = clap_plugin_stop_processing;
    plugin->plugin.reset = clap_plugin_reset;
    plugin->plugin.process = clap_plugin_process;
    plugin->plugin.get_extension = clap_plugin_get_extension;
    plugin->plugin.on_main_thread = clap_plugin_on_main_thread;

    plugin->params.count = clap_plugin_params_count;
    plugin->params.get_info = clap_plugin_params_get_info;
    plugin->params.get_value = clap_plugin_params_get_value;
    plugin->params.value_to_text = clap_plugin_params_value_to_text;
    plugin->params.text_to_value = clap_plugin_params_text_to_value;
    plugin->params.flush = clap_plugin_params_flush;

    plugin->audio_ports.count = clap_plugin_audio_ports_count;
    plugin->audio_ports.get = clap_plugin_audio_ports_get;

    plugin->note_ports.count = clap_plugin_note_ports_count;
    plugin->note_ports.get = clap_plugin_note_ports_get;

    plugin->voice_info.get = clap_plugin_voice_info_get;
    plugin->state.save = clap_plugin_state_save;
    plugin->state.load = clap_plugin_state_load;

    plugin->gui.is_api_supported = clap_plugin_gui_is_api_supported;
    plugin->gui.get_preferred_api = clap_plugin_gui_get_preferred_api;
    plugin->gui.create = clap_plugin_gui_create;
    plugin->gui.destroy = clap_plugin_gui_destroy;
    plugin->gui.set_scale = clap_plugin_gui_set_scale;
    plugin->gui.get_size = clap_plugin_gui_get_size;
    plugin->gui.can_resize = clap_plugin_gui_can_resize;
    plugin->gui.set_size = clap_plugin_gui_set_size;
    plugin->gui.set_parent = clap_plugin_gui_set_parent;
    plugin->gui.set_transient = clap_plugin_gui_set_transient;
    plugin->gui.suggest_title = clap_plugin_gui_suggest_title;
    plugin->gui.show = clap_plugin_gui_show;
    plugin->gui.hide = clap_plugin_gui_hide;

    return &plugin->plugin;
}

static const clap_plugin_factory_t g_plugin_factory = {
    clap_plugin_factory_get_plugin_count,
    clap_plugin_factory_get_plugin_descriptor,
    clap_plugin_factory_create_plugin,
};

bool clap_entry_init(const char *plugin_path) {
    (void)plugin_path;
    return true;
}

void clap_entry_deinit() {
}

const void *clap_entry_get_factory(const char *factory_id) {
    if (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &g_plugin_factory;
    }
    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    clap_entry_init,
    clap_entry_deinit,
    clap_entry_get_factory,
};

