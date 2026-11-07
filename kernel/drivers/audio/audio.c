#include <drivers/audio/audio.h>
#include <drivers/audio/audio_backend.h>

#include <arch/x86_64/sync/sync.h>
#include <xlibc/string.h>

#define AUDIO_MAX_BACKENDS          16u
#define AUDIO_OUTPUT_BACKEND_SHIFT  24u
#define AUDIO_OUTPUT_LOCAL_MASK     0x00FFFFFFu

static spinlock_t g_backend_lock;
static audio_backend_t g_backends[AUDIO_MAX_BACKENDS];
static u8 g_backend_count;

static mutex_t g_audio_mutex;
static b8 g_audio_initialized;

static audio_output_id_t g_selected_output;
static b8 g_has_selected_output;
static u8 g_volume = 100;
static b8 g_muted;

static b8 audio_backend_snapshot(u8 slot, audio_backend_t* out) {
    if (!out) {
        return false;
    }

    u64 flags;
    spin_lock_irqsave(&g_backend_lock, &flags);

    if (slot >= g_backend_count) {
        spin_unlock_irqrestore(&g_backend_lock, flags);
        return false;
    }

    *out = g_backends[slot];
    spin_unlock_irqrestore(&g_backend_lock, flags);
    return true;
}

static audio_result_t audio_decode_output_id(audio_output_id_t id, u8* backend_slot, u32* local_index, audio_backend_t* backend) {
    u32 tag = id >> AUDIO_OUTPUT_BACKEND_SHIFT;
    u32 local = id & AUDIO_OUTPUT_LOCAL_MASK;

    if (tag == 0 || tag > AUDIO_MAX_BACKENDS || !backend_slot || !local_index || !backend) {
        return AUDIO_INVALID_ARGUMENT;
    }

    u8 slot = (u8)(tag - 1u);
    if (!audio_backend_snapshot(slot, backend)) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    if (!backend->ops || !backend->ops->get_output_count ||
        local >= backend->ops->get_output_count(backend->context)) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    *backend_slot = slot;
    *local_index = local;
    return AUDIO_OK;
}

audio_result_t audio_backend_register(const audio_backend_t* backend, u8* backend_slot) {
    if (!backend || !backend->name || !backend->context || !backend->ops ||
        !backend->ops->get_output_count || !backend->ops->get_output_info ||
        !backend->ops->set_output || !backend->ops->clear_output ||
        !backend->ops->set_volume || !backend->ops->set_mute) {
        return AUDIO_INVALID_ARGUMENT;
    }

    u64 flags;
    spin_lock_irqsave(&g_backend_lock, &flags);

    if (g_backend_count >= AUDIO_MAX_BACKENDS) {
        spin_unlock_irqrestore(&g_backend_lock, flags);
        return AUDIO_NO_MEMORY;
    }

    u8 slot = g_backend_count;
    g_backends[slot] = *backend;
    g_backend_count++;

    spin_unlock_irqrestore(&g_backend_lock, flags);

    if (backend_slot) {
        *backend_slot = slot;
    }

    return AUDIO_OK;
}

audio_result_t audio_init(void) {
    if (g_audio_initialized) {
        return AUDIO_OK;
    }

    mutex_init(&g_audio_mutex);
    g_selected_output = 0;
    g_has_selected_output = false;
    g_volume = 100;
    g_muted = false;
    g_audio_initialized = true;

    return AUDIO_OK;
}

u32 audio_get_output_count(void) {
    u32 total = 0;
    u64 flags;

    spin_lock_irqsave(&g_backend_lock, &flags);
    for (u8 i = 0; i < g_backend_count; ++i) {
        const audio_backend_ops_t* ops = g_backends[i].ops;
        if (ops && ops->get_output_count) {
            total += ops->get_output_count(g_backends[i].context);
        }
    }
    spin_unlock_irqrestore(&g_backend_lock, flags);

    return total;
}

audio_result_t audio_get_output_info(u32 index, audio_output_info_t* output) {
    if (!output) {
        return AUDIO_INVALID_ARGUMENT;
    }

    audio_backend_t selected_backend;
    u32 local_index = index;
    u8 selected_slot = 0xFF;
    b8 found = false;

    u64 flags;
    spin_lock_irqsave(&g_backend_lock, &flags);

    for (u8 i = 0; i < g_backend_count; ++i) {
        u32 count = g_backends[i].ops->get_output_count(g_backends[i].context);
        if (local_index < count) {
            selected_backend = g_backends[i];
            selected_slot = i;
            found = true;
            break;
        }
        local_index -= count;
    }

    spin_unlock_irqrestore(&g_backend_lock, flags);

    if (!found || selected_slot == 0xFF) {
        return AUDIO_DEVICE_NOT_FOUND;
    }

    memset(output, 0, sizeof(*output));

    audio_result_t result = selected_backend.ops->get_output_info(selected_backend.context, local_index, output);

    if (result != AUDIO_OK) {
        return result;
    }

    output->id = (((u32)selected_slot + 1u) << AUDIO_OUTPUT_BACKEND_SHIFT) | (local_index & AUDIO_OUTPUT_LOCAL_MASK);

    return AUDIO_OK;
}

audio_result_t audio_set_output(audio_output_id_t output_id) {
    if (!g_audio_initialized) {
        return AUDIO_ERROR;
    }

    mutex_lock(&g_audio_mutex);

    u8 new_slot;
    u32 new_local;
    audio_backend_t new_backend;

    audio_result_t result = audio_decode_output_id(output_id, &new_slot, &new_local, &new_backend);

    if (result != AUDIO_OK) {
        mutex_unlock(&g_audio_mutex);
        return result;
    }

    if (g_has_selected_output && g_selected_output == output_id) {
        mutex_unlock(&g_audio_mutex);
        return AUDIO_OK;
    }

    u8 old_slot = 0xFF;
    u32 old_local = 0;
    audio_backend_t old_backend;
    b8 had_old_output = false;

    if (g_has_selected_output) {
        if (audio_decode_output_id(g_selected_output, &old_slot, &old_local, &old_backend) == AUDIO_OK) {
            had_old_output = true;
        }
    }

    result = new_backend.ops->set_output(new_backend.context, new_local);

    if (result == AUDIO_OK) {
        result = new_backend.ops->set_volume(new_backend.context, g_volume);
    }

    if (result == AUDIO_OK) {
        result = new_backend.ops->set_mute(new_backend.context, g_muted);
    }

    if (result == AUDIO_OK && had_old_output && old_slot != new_slot) {
        result = old_backend.ops->clear_output(old_backend.context);
    }

    if (result != AUDIO_OK) {
        // shitty rollback if endpoint activation or state application failed
        if (had_old_output) {
            if (old_slot != new_slot) {
                (void)new_backend.ops->clear_output(new_backend.context);
            }
            (void)old_backend.ops->set_output(old_backend.context, old_local);
            (void)old_backend.ops->set_volume(old_backend.context, g_volume);
            (void)old_backend.ops->set_mute(old_backend.context, g_muted);
        } else {
            (void)new_backend.ops->clear_output(new_backend.context);
        }

        mutex_unlock(&g_audio_mutex);
        return result;
    }

    g_selected_output = output_id;
    g_has_selected_output = true;

    mutex_unlock(&g_audio_mutex);
    return AUDIO_OK;
}

audio_result_t audio_set_volume(u8 volume) {
    if (volume > 100) {
        return AUDIO_INVALID_ARGUMENT;
    }
    if (!g_audio_initialized) {
        return AUDIO_ERROR;
    }

    mutex_lock(&g_audio_mutex);

    if (g_has_selected_output) {
        u8 slot;
        u32 local;
        audio_backend_t backend;
        audio_result_t result = audio_decode_output_id(g_selected_output, &slot, &local, &backend);
        if (result != AUDIO_OK) {
            mutex_unlock(&g_audio_mutex);
            return result;
        }

        result = backend.ops->set_volume(backend.context, volume);
        if (result != AUDIO_OK) {
            mutex_unlock(&g_audio_mutex);
            return result;
        }
    }

    g_volume = volume;
    mutex_unlock(&g_audio_mutex);
    return AUDIO_OK;
}

audio_result_t audio_get_volume(u8* volume) {
    if (!volume) {
        return AUDIO_INVALID_ARGUMENT;
    }
    if (!g_audio_initialized) {
        return AUDIO_ERROR;
    }

    mutex_lock(&g_audio_mutex);
    *volume = g_volume;
    mutex_unlock(&g_audio_mutex);
    return AUDIO_OK;
}

audio_result_t audio_set_mute(b8 muted) {
    if (!g_audio_initialized) {
        return AUDIO_ERROR;
    }

    mutex_lock(&g_audio_mutex);

    if (g_has_selected_output) {
        u8 slot;
        u32 local;
        audio_backend_t backend;
        audio_result_t result = audio_decode_output_id(g_selected_output, &slot, &local, &backend);
        if (result != AUDIO_OK) {
            mutex_unlock(&g_audio_mutex);
            return result;
        }

        result = backend.ops->set_mute(backend.context, muted);
        if (result != AUDIO_OK) {
            mutex_unlock(&g_audio_mutex);
            return result;
        }
    }

    g_muted = muted;
    mutex_unlock(&g_audio_mutex);
    return AUDIO_OK;
}

audio_result_t audio_get_mute(b8* muted) {
    if (!muted) {
        return AUDIO_INVALID_ARGUMENT;
    }
    if (!g_audio_initialized) {
        return AUDIO_ERROR;
    }

    mutex_lock(&g_audio_mutex);
    *muted = g_muted;
    mutex_unlock(&g_audio_mutex);
    return AUDIO_OK;
}
