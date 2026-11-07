#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>

/*
 * XenonOS kernel audio API
 *
 * Hardware operations and the volume/mute getters (which take the audio
 * control mutex) must be called from normal task context, with a current task
 * and interrupts enabled. They may sleep while the backend talks to hardware.
 */

typedef u32 audio_output_id_t;

typedef enum {
    AUDIO_OK = 0,
    AUDIO_ERROR,
    AUDIO_INVALID_ARGUMENT,
    AUDIO_DEVICE_NOT_FOUND,
    AUDIO_NOT_SUPPORTED,
    AUDIO_NO_MEMORY,
    AUDIO_BUSY
} audio_result_t;

typedef struct {
    u32 sample_rate;
    u16 channels;
    u16 bits_per_sample;
} audio_format_t;

typedef struct {
    audio_output_id_t id;
    char name[64];
} audio_output_info_t;

// initializes the generic audio core, safe to call more than once
audio_result_t audio_init(void);

// number of output endpoints currently registered by hardware backends
u32 audio_get_output_count(void);

audio_result_t audio_get_output_info(u32 index, audio_output_info_t* output);

audio_result_t audio_set_output(audio_output_id_t output_id);

audio_result_t audio_set_volume(u8 volume);
audio_result_t audio_get_volume(u8* volume);

audio_result_t audio_set_mute(b8 muted);
audio_result_t audio_get_mute(b8* muted);
