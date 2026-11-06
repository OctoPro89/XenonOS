#include <drivers/audio/wav_parser.h>
#include <xlibc/string.h>

typedef struct __packed__ {
    char chunk_id[4];       // "RIFF"
    u32 chunk_size;         // The size of the entire file minus 8 bytes
    char format[4];         // "WAVE"
} wav_riff_header_t;

typedef struct __packed__ {
    char chunk_id[4];       // "fmt "
    u32 chunk_size;         // Size of this chunk
    u16 audio_format;       // 1 = PCM (uncompressed)
    u16 num_channels;       // 1 = Mono, 2 = Stereo, etc
    u32 sample_rate;        // 44100, 48000
    u32 byte_rate;          // sample_rate * num_channels * (bits_per_sample / 8)
    u16 block_align;        // num_channels * (bits_per_sample / 8)
    u16 bits_per_sample;    // 8, 16, 24, or 32
} wav_fmt_chunk_t;

typedef struct __packed__ {
    char chunk_id[4];       // "data"
    u32 chunk_size;         // length of raw audio data payload
} wav_data_chunk_t;

b8 wav_parser_parse_from_memory(u8* buffer, size_t file_size, wav_parsed_t* out_wav) {
    if (file_size < sizeof(wav_riff_header_t)) { return false; } // file too small

    wav_riff_header_t* riff = (wav_riff_header_t*)buffer;
    if (memcmp(riff->chunk_id, "RIFF", 4) != 0 || memcmp(riff->format, "WAVE", 4) != 0) {
        return false; // not a valid WAVE file
    }

    size_t offset = sizeof(wav_riff_header_t);
    b8 found_fmt = false, found_data = false;

    // walk chunks
    while (offset + 8 <= file_size) {
        char chunk_id[4];
        memcpy(chunk_id, buffer + offset, 4);
        u32 chunk_size = *(u32*)(buffer + offset + 4);

        offset += 8; // move past ID and size fields

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            if (offset + sizeof(wav_fmt_chunk_t) - 8 > file_size) { return false; }

            wav_fmt_chunk_t* fmt = (wav_fmt_chunk_t*)(buffer + offset - 8);

            if (fmt->audio_format != 1) { return false; } // only support uncompressed PCM

            out_wav->channels = fmt->num_channels;
            out_wav->sample_rate = fmt->sample_rate;
            out_wav->bits_per_sample = fmt->bits_per_sample;
            found_fmt = true;
        }
        else if (memcmp(chunk_id, "data", 4) == 0) {
            out_wav->audio_data = buffer + offset;
            out_wav->data_size = chunk_size;
            found_data = 1;
            break; // found payload
        }

        // skip unknown metadata chunks (e.g. LIST, JUNK)
        offset += chunk_size;

        // handle pad byte for odd chunk sizes (RIFF constraint)
        if (chunk_size % 2 != 0) {
            ++offset;
        }
    }

    return (found_fmt && found_data) ? true : false;
}