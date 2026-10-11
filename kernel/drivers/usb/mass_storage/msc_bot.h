#pragma once

#include <xlibc/xstdint.h>
#include <xlibc/xassert.h>
#include <kernel.h>

#define MSC_BOT_CBW_SIGNATURE 0x43425355u
#define MSC_BOT_CSW_SIGNATURE 0x53425355u

#define MSC_BOT_CBW_SIZE 31
#define MSC_BOT_CSW_SIZE 13

#define MSC_BOT_CBW_FLAG_IN  0x80
#define MSC_BOT_CBW_FLAG_OUT 0x00

typedef struct __packed__ {
    u32 signature;
    u32 tag;
    u32 transfer_length;
    u8 flags;
    u8 lun;
    u8 command_length;
    u8 command[16];
} msc_bot_cbw_t;

STATIC_ASSERT(sizeof(msc_bot_cbw_t) == MSC_BOT_CBW_SIZE);

typedef struct __packed__ {
    u32 signature;
    u32 tag;
    u32 residue;
    u8 status;
} msc_bot_csw_t;

STATIC_ASSERT(sizeof(msc_bot_csw_t) == MSC_BOT_CSW_SIZE);

typedef struct {
    u8 bulk_in_endpoint;
    u8 bulk_out_endpoint;

    u32 next_tag;
    u8 lun;
} msc_bot_t;