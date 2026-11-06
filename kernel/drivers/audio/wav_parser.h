#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xstddef.h>
#include <kernel.h>

typedef struct {
    u16 channels;
    u32 sample_rate;
    u16 bits_per_sample;
    u8* audio_data;         // NOTE: wav_parsed_t DOES NOT own this memory, it is a pointer into `buffer`
    u32 data_size;
} wav_parsed_t;

b8 wav_parser_parse_from_memory(u8* buffer, size_t file_size, wav_parsed_t* out_wav);