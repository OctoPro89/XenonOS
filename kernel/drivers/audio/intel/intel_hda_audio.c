#include <drivers/audio/intel/intel_hda_audio.h>
#include <drivers/audio/audio_backend.h>

#include <arch/x86_64/sync/sync.h>
#include <xlibc/string.h>

#define INTEL_HDA_AUDIO_MAX_CONTROLLERS  8u

// approximate attenuation in quarter-dB units for percentages 0..100. 1..100 follow 20*log10(volume/100), capped at -40 dB | zero is handled as hardware mute, where supported
static const u8 g_volume_attenuation_qdb[101] = {
    0,
    160, 136, 122, 112, 104, 98, 92, 88, 84,
    80, 77, 74, 71, 68, 66, 64, 62, 60, 58,
    56, 54, 53, 51, 50, 48, 47, 45, 44, 43,
    42, 41, 40, 39, 37, 36, 35, 35, 34, 33,
    32, 31, 30, 29, 29, 28, 27, 26, 26, 25,
    24, 23, 23, 22, 21, 21, 20, 20, 19, 18,
    18, 17, 17, 16, 16, 15, 14, 14, 13, 13,
    12, 12, 11, 11, 10, 10, 10, 9, 9, 8,
    8, 7, 7, 6, 6, 6, 5, 5, 4, 4,
    4, 3, 3, 3, 2, 2, 1, 1, 1, 0,
    0
};

typedef struct {
    intel_hda_driver_t* driver;
    mutex_t operation_mutex;

    b8 has_selected_output;
    u8 selected_codec_addr;
    u16 selected_path_index;

    u8 volume;
    b8 muted;
} intel_hda_audio_context_t;

static intel_hda_audio_context_t g_contexts[INTEL_HDA_AUDIO_MAX_CONTROLLERS];
static u8 g_context_count;

static b8 intel_hda_path_is_analog(const intel_hda_output_path_t* path) {
    if (!path || !path->valid || path->depth < 2 || path->depth > HDA_MAX_PATH_DEPTH) {
        return false;
    }

    return path->pin_device == HDA_PIN_DEVICE_LINE_OUT || path->pin_device == HDA_PIN_DEVICE_HEADPHONE || path->pin_device == HDA_PIN_DEVICE_SPEAKER;
}

static b8 intel_hda_audio_is_first_endpoint(intel_hda_driver_t* driver, u8 codec_addr, u16 path_index) {
    if (!driver || codec_addr >= HDA_MAX_CODECS || !driver->output_paths[codec_addr] ||
        path_index >= driver->output_path_count[codec_addr]) {
        return false;
    }

    const intel_hda_output_path_t* path = &driver->output_paths[codec_addr][path_index];
    if (!intel_hda_path_is_analog(path)) {
        return false;
    }

    // several widget paths can lead to the same physical pin, expose one endpoint per pin
    for (u16 i = 0; i < path_index; ++i) {
        const intel_hda_output_path_t* previous = &driver->output_paths[codec_addr][i];
        if (intel_hda_path_is_analog(previous) && previous->pin_nid == path->pin_nid) {
            return false;
        }
    }

    return true;
}

static u32 intel_hda_audio_count_outputs(void* opaque) {
    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    if (!context || !context->driver) {
        return 0;
    }

    intel_hda_driver_t* driver = context->driver;
    u32 count = 0;

    for (u8 codec = 0; codec < HDA_MAX_CODECS; ++codec) {
        if (!(driver->codec_mask & (1u << codec)) || !driver->output_paths[codec]) {
            continue;
        }

        for (u16 i = 0; i < driver->output_path_count[codec]; ++i) {
            if (intel_hda_audio_is_first_endpoint(driver, codec, i)) {
                ++count;
            }
        }
    }

    return count;
}

static b8 intel_hda_audio_find_output(intel_hda_audio_context_t* context, u32 local_index, u8* codec_addr, u16* path_index, intel_hda_output_path_t** out_path) {
    if (!context || !context->driver || !codec_addr || !path_index || !out_path) {
        return false;
    }

    intel_hda_driver_t* driver = context->driver;

    for (u8 codec = 0; codec < HDA_MAX_CODECS; ++codec) {
        if (!(driver->codec_mask & (1u << codec)) || !driver->output_paths[codec]) {
            continue;
        }

        for (u16 i = 0; i < driver->output_path_count[codec]; ++i) {
            intel_hda_output_path_t* path = &driver->output_paths[codec][i];
            if (!intel_hda_audio_is_first_endpoint(driver, codec, i)) {
                continue;
            }

            if (local_index == 0) {
                *codec_addr = codec;
                *path_index = i;
                *out_path = path;
                return true;
            }

            --local_index;
        }
    }

    return false;
}

static void intel_hda_name_append(char* destination, size_t capacity, const char* text) {
    if (!destination || !text || capacity == 0) {
        return;
    }

    size_t used = 0;
    while (used < capacity && destination[used] != '\0') {
        ++used;
    }

    while (*text && used + 1 < capacity) {
        destination[used++] = *text++;
    }

    destination[used] = '\0';
}

static void intel_hda_name_append_u32(char* destination, size_t capacity, u32 value) {
    char digits[10];
    u32 count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && count < sizeof(digits));

    while (count) {
        char one[2] = { digits[--count], '\0' };
        intel_hda_name_append(destination, capacity, one);
    }
}

static audio_result_t intel_hda_audio_get_output_info(void* opaque, u32 local_index, audio_output_info_t* output) {
    if (!opaque || !output) {
        return AUDIO_INVALID_ARGUMENT;
    }

    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    u8 codec_addr;
    u16 path_index;
    intel_hda_output_path_t* path = NULL;

    if (!intel_hda_audio_find_output(context, local_index, &codec_addr, &path_index, &path)) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    (void)path_index;
    memset(output, 0, sizeof(*output));

    static const char* const positions[16] = {
        "N/A", "Rear", "Front", "Left", "Right", "Top", "Bottom",
        "Special", "Special", "Special", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved"
    };

    static const char* const locations[4] = {
        "External", "Internal", "Separate Chassis", "Other"
    };

    static const char* const colors[16] = {
        "Unknown", "Black", "Grey", "Blue", "Green", "Red", "Orange",
        "Yellow", "Purple", "Pink", "Reserved", "Reserved", "Reserved",
        "Reserved", "White", "Unknown"
    };

    const char* device_name = "Audio Out";
    if (path->pin_device == HDA_PIN_DEVICE_LINE_OUT) {
        device_name = "Line Out";
    } else if (path->pin_device == HDA_PIN_DEVICE_HEADPHONE) {
        device_name = "Headphones";
    } else if (path->pin_device == HDA_PIN_DEVICE_SPEAKER) {
        device_name = "Speakers";
    }

    u8 location = (path->pin_config >> 24) & 0x3F;
    u8 location_group = (location >> 4) & 0x03;
    u8 position = location & 0x0F;
    u8 color = (path->pin_config >> 12) & 0x0F;

    intel_hda_name_append(output->name, sizeof(output->name), "HDA Codec ");
    intel_hda_name_append_u32(output->name, sizeof(output->name), codec_addr);
    intel_hda_name_append(output->name, sizeof(output->name), " ");
    intel_hda_name_append(output->name, sizeof(output->name), locations[location_group]);
    intel_hda_name_append(output->name, sizeof(output->name), " ");
    intel_hda_name_append(output->name, sizeof(output->name), positions[position]);

    if (color != 0) {
        intel_hda_name_append(output->name, sizeof(output->name), " ");
        intel_hda_name_append(output->name, sizeof(output->name), colors[color]);
    }

    intel_hda_name_append(output->name, sizeof(output->name), " ");
    intel_hda_name_append(output->name, sizeof(output->name), device_name);
    intel_hda_name_append(output->name, sizeof(output->name), " (Pin ");
    intel_hda_name_append_u32(output->name, sizeof(output->name), path->pin_nid);
    intel_hda_name_append(output->name, sizeof(output->name), ")");

    return AUDIO_OK;
}

static b8 intel_hda_audio_disable_path_pin(intel_hda_driver_t* driver, u8 codec_addr, const intel_hda_output_path_t* path) {
    if (!driver || !path || codec_addr >= HDA_MAX_CODECS || !driver->widgets[codec_addr]) {
        return false;
    }

    intel_hda_widget_t* pin = &driver->widgets[codec_addr][path->pin_nid];
    if (!pin->valid || pin->type != HDA_WIDGET_TYPE_PIN_COMPLEX) {
        return false;
    }

    u32 old_control = 0;
    if (!intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_GET_PIN_WIDGET_CONTROL, 0, &old_control)) {
        return false;
    }

    u8 new_control = (u8)old_control;
    new_control &= (u8)~(HDA_PINCTL_OUTPUT_ENABLE | HDA_PINCTL_HEADPHONE_ENABLE);

    u32 response = 0;
    return intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_SET_PIN_WIDGET_CONTROL, new_control, &response);
}

static audio_result_t intel_hda_audio_set_output(void* opaque, u32 local_index) {
    if (!opaque) {
        return AUDIO_INVALID_ARGUMENT;
    }

    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    intel_hda_driver_t* driver = context->driver;

    u8 new_codec;
    u16 new_path_index;
    intel_hda_output_path_t* new_path = NULL;

    if (!intel_hda_audio_find_output(context, local_index, &new_codec, &new_path_index, &new_path)) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    mutex_lock(&context->operation_mutex);

    // route changes cannot safely reuse a prepared/running stream yet
    if (driver->playback_prepared || driver->playback_running) {
        mutex_unlock(&context->operation_mutex);
        return AUDIO_BUSY;
    }

    intel_hda_output_path_t* old_path = NULL;
    u8 old_codec = context->selected_codec_addr;

    if (context->has_selected_output && old_codec < HDA_MAX_CODECS &&
        driver->output_paths[old_codec] &&
        context->selected_path_index < driver->output_path_count[old_codec]) {
        old_path = &driver->output_paths[old_codec][context->selected_path_index];
    }

    if (old_path && (old_codec != new_codec || old_path->pin_nid != new_path->pin_nid)) {
        if (!intel_hda_audio_disable_path_pin(driver, old_codec, old_path)) {
            mutex_unlock(&context->operation_mutex);
            return AUDIO_ERROR;
        }
    }

    if (!intel_hda_driver_configure_output_path(driver, new_codec, new_path, true)) {
        // attempt shitty cleanup if configuration failed after enabling the new pin
        // TODO:
        if (!old_path || old_codec != new_codec || old_path->pin_nid != new_path->pin_nid) {
            (void)intel_hda_audio_disable_path_pin(driver, new_codec, new_path);
        }

        // shitty restore of the previous selectors and pin configuration
        if (old_path) {
            (void)intel_hda_driver_configure_output_path(driver, old_codec, old_path, true);
        }
        mutex_unlock(&context->operation_mutex);
        return AUDIO_ERROR;
    }

    context->selected_codec_addr = new_codec;
    context->selected_path_index = new_path_index;
    context->has_selected_output = true;

    mutex_unlock(&context->operation_mutex);
    return AUDIO_OK;
}

static audio_result_t intel_hda_audio_clear_output(void* opaque) {
    if (!opaque) {
        return AUDIO_INVALID_ARGUMENT;
    }

    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    mutex_lock(&context->operation_mutex);

    if (!context->has_selected_output) {
        mutex_unlock(&context->operation_mutex);
        return AUDIO_OK;
    }

    intel_hda_driver_t* driver = context->driver;
    if (driver->playback_prepared || driver->playback_running) {
        mutex_unlock(&context->operation_mutex);
        return AUDIO_BUSY;
    }

    u8 codec = context->selected_codec_addr;
    if (codec >= HDA_MAX_CODECS || !driver->output_paths[codec] || context->selected_path_index >= driver->output_path_count[codec]) {
        mutex_unlock(&context->operation_mutex);
        return AUDIO_DEVICE_NOT_FOUND;
    }

    intel_hda_output_path_t* path =
        &driver->output_paths[codec][context->selected_path_index];

    if (!intel_hda_audio_disable_path_pin(driver, codec, path)) {
        mutex_unlock(&context->operation_mutex);
        return AUDIO_ERROR;
    }

    context->has_selected_output = false;
    mutex_unlock(&context->operation_mutex);
    return AUDIO_OK;
}

static audio_result_t intel_hda_audio_apply_gain(intel_hda_audio_context_t* context, u8 volume, b8 muted) {
    if (!context || !context->driver || !context->has_selected_output) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    intel_hda_driver_t* driver = context->driver;
    u8 codec_addr = context->selected_codec_addr;

    if (codec_addr >= HDA_MAX_CODECS || !driver->output_paths[codec_addr] ||
        context->selected_path_index >= driver->output_path_count[codec_addr]) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    intel_hda_output_path_t* path = &driver->output_paths[codec_addr][context->selected_path_index];

    // prefer the output amp closest to the physical pin
    intel_hda_widget_t* amp_widget = NULL;
    for (u8 reverse = path->depth; reverse > 0; --reverse) {
        u8 nid = path->nodes[reverse - 1];
        intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];
        if (widget->valid && widget->has_output_amp_caps) {
            amp_widget = widget;
            break;
        }
    }

    b8 effective_mute = muted || volume == 0;

    if (!amp_widget) {
        return (!effective_mute && volume == 100) ? AUDIO_OK : AUDIO_NOT_SUPPORTED;
    }

    u32 caps = amp_widget->output_amp_caps;
    u8 zero_db_code = caps & 0x7F;
    u8 max_gain_code = (caps >> 8) & 0x7F;
    u8 step_qdb = (u8)(((caps >> 16) & 0x7F) + 1);
    b8 mute_supported = (caps & (1u << 31)) != 0;

    if (zero_db_code > max_gain_code) {
        zero_db_code = max_gain_code;
    }

    if (effective_mute && !mute_supported) {
        return AUDIO_NOT_SUPPORTED;
    }

    if (!effective_mute && volume < 100 && zero_db_code == 0) {
        return AUDIO_NOT_SUPPORTED;
    }

    u8 attenuation_qdb = g_volume_attenuation_qdb[volume];
    u8 attenuation_steps = (u8)((attenuation_qdb + (step_qdb / 2)) / step_qdb);
    if (attenuation_steps > zero_db_code) {
        attenuation_steps = zero_db_code;
    }

    u8 gain_code = zero_db_code - attenuation_steps;

    // set output amplifier, both stereo channels, with the selected gain/mute
    u16 payload = (1u << 15) | (1u << 13) | (1u << 12) | gain_code;
    if (effective_mute) {
        payload |= (1u << 7);
    }

    u32 response = 0;
    if (!intel_hda_driver_send_verb4(driver, codec_addr, amp_widget->nid, HDA_VERB_SET_AMP_GAIN_MUTE, payload, &response)) {
        return AUDIO_ERROR;
    }

    return AUDIO_OK;
}

static audio_result_t intel_hda_audio_set_volume(void* opaque, u8 volume) {
    if (!opaque || volume > 100) {
        return AUDIO_INVALID_ARGUMENT;
    }

    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    mutex_lock(&context->operation_mutex);

    audio_result_t result = AUDIO_OK;
    if (context->has_selected_output) {
        result = intel_hda_audio_apply_gain(context, volume, context->muted);
    }

    if (result == AUDIO_OK) {
        context->volume = volume;
    }

    mutex_unlock(&context->operation_mutex);
    return result;
}

static audio_result_t intel_hda_audio_set_mute(void* opaque, b8 muted) {
    if (!opaque) {
        return AUDIO_INVALID_ARGUMENT;
    }

    intel_hda_audio_context_t* context = (intel_hda_audio_context_t*)opaque;
    mutex_lock(&context->operation_mutex);

    audio_result_t result = AUDIO_OK;
    if (context->has_selected_output) {
        result = intel_hda_audio_apply_gain(context, context->volume, muted);
    }

    if (result == AUDIO_OK) {
        context->muted = muted;
    }

    mutex_unlock(&context->operation_mutex);
    return result;
}

static const audio_backend_ops_t g_intel_hda_audio_ops = {
    .get_output_count = intel_hda_audio_count_outputs,
    .get_output_info = intel_hda_audio_get_output_info,
    .set_output = intel_hda_audio_set_output,
    .clear_output = intel_hda_audio_clear_output,
    .set_volume = intel_hda_audio_set_volume,
    .set_mute = intel_hda_audio_set_mute
};

b8 intel_hda_audio_register(intel_hda_driver_t* driver) {
    if (!driver) {
        return false;
    }

    for (u8 i = 0; i < g_context_count; ++i) {
        if (g_contexts[i].driver == driver) {
            return true;
        }
    }

    if (g_context_count >= INTEL_HDA_AUDIO_MAX_CONTROLLERS) {
        return false;
    }

    intel_hda_audio_context_t* context = &g_contexts[g_context_count];
    memset(context, 0, sizeof(*context));

    context->driver = driver;
    context->volume = 100;
    context->muted = false;
    mutex_init(&context->operation_mutex);

    audio_backend_t backend = {
        .name = "Intel High Definition Audio",
        .context = context,
        .ops = &g_intel_hda_audio_ops
    };

    if (audio_backend_register(&backend, NULL) != AUDIO_OK) {
        memset(context, 0, sizeof(*context));
        return false;
    }

    ++g_context_count;
    return true;
}
