#pragma once

#include <drivers/audio/audio.h>

// internal interface between the generic audio core and hardware backends
typedef struct audio_backend_ops {
    u32 (*get_output_count)(void* context);
    audio_result_t (*get_output_info)(
        void* context,
        u32 local_index,
        audio_output_info_t* output
    );

    audio_result_t (*set_output)(void* context, u32 local_index);
    audio_result_t (*clear_output)(void* context);
    audio_result_t (*set_volume)(void* context, u8 volume);
    audio_result_t (*set_mute)(void* context, b8 muted);
} audio_backend_ops_t;

typedef struct {
    const char* name;
    void* context;
    const audio_backend_ops_t* ops;
} audio_backend_t;

/**
 * @brief Register a backend. Registration is intended for driver initialization (boot-time, before normal concurrent runtime registration is introduced).
 * @bote `backend_slot` may be NULL. Slots remain stable for the lifetime of the OS.
 */
audio_result_t audio_backend_register(const audio_backend_t* backend, u8* backend_slot);
