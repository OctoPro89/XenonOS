#include <drivers/usb/hid/hid_parser.h>
#include <xlibc/xstddef.h>
#include <kernel.h>

// TODO: CHECK THIS WHOLE FILE

#define LONG_ITEM_PREFIX ((u8)0xFE)
#define MAX_EXPLICIT_USAGES ((u8)32)
#define GLOBAL_STACK_DEPTH ((u8)4)
#define INVALID_REPORT_INDEX ((u16)0xFFFFu)

static size_t item_length(const u8* descriptor, size_t offset, size_t length) {
    if (offset >= length) { return 0; }

    u8 prefix = descriptor[offset];
    if (prefix == LONG_ITEM_PREFIX) {
        if (offset + 1 >= length) {
            return 0;
        }

        u8 data_size = descriptor[offset + 1];
        size_t total = 3 + (size_t)data_size;
        if (offset + total > length) {
            return 0;
        }

        return total;
    }

    u8 size = prefix & 0x03u;
    if (size == 3) {
        size = 4;
    }

    if (offset + 1 + size > length) {
        return 0;
    }

    return 1 + size;
}

static usb_hid_report_item_t decode_short_item(const u8* descriptor, size_t offset) {
    u8 prefix = descriptor[offset];
    u8 size = prefix & 0x03u;
    if (size == 3) {
        size = 4;
    }

    u32 data = 0;
    for (u8 i = 0; i < size; i++) {
        data |= (u32)(descriptor[offset + 1 + i]) << (8u * i);
    }

    return (usb_hid_report_item_t){
        .type = (usb_hid_item_type_t)((prefix >> 2) & 0x03u),
        .tag = (u8)((prefix >> 4) & 0x0Fu),
        .size = size,
        .data = data,
    };
}

static i32 sign_extend(u32 value, u8 payload_bytes) {
    if (payload_bytes == 0) {
        return 0;
    }

    u8 bits = (u8)(payload_bytes * 8u);
    if (bits >= 32) {
        return (i32)(value);
    }

    u32 mask = (1u << bits) - 1u;
    value &= mask;
    u32 sign_bit = 1u << (bits - 1u);
    if ((value & sign_bit) != 0) {
        value |= ~mask;
    }
    return (i32)value;
}

typedef struct {
    u16 usage_page;
    u32 report_size;
    u32 report_count;
    u8 report_id;
    i32 logical_minimum;
    i32 logical_maximum;
} global_state_t;

typedef struct {
    u32 usages[MAX_EXPLICIT_USAGES];
    u8 usage_count;
    u32 usage_minimum;
    u32 usage_maximum;
    b8 has_usage_range;
} local_state_t;

static __hint_inline__ void local_state_reset(local_state_t* ls) {
    ls->usage_count = 0;
    ls->usage_minimum = 0;
    ls->usage_maximum = 0;
    ls->has_usage_range = false;
}

typedef struct {
    b8 seen;
    u8 report_id;
    u32 field_count;
    u32 body_bits;
} report_accumulator_t;

static b8 count_items(const u8* descriptor, size_t length, size_t* out_count) {
    *out_count = 0;
    size_t offset = 0;
    while (offset < length) {
        size_t len = item_length(descriptor, offset, length);
        if (len == 0) {
            return false;
        }

        if (descriptor[offset] != LONG_ITEM_PREFIX) {
            ++(*out_count);
        }

        offset += len;
    }

    return true;
}

static b8 parse_items(const u8* descriptor, size_t length, usb_hid_report_item_t* items, size_t max_items, size_t* out_count) {
    *out_count = 0;
    size_t offset = 0;
    while (offset < length) {
        size_t len = item_length(descriptor, offset, length);
        if (len == 0) {
            return false;
        }

        if (descriptor[offset] != LONG_ITEM_PREFIX) {
            if ((*out_count) >= max_items) {
                return false;
            }
            items[(*out_count)++] = decode_short_item(descriptor, offset);
        }
        offset += len;
    }
    return true;
}

static void register_input_report(report_accumulator_t* reports, u8* order, u16* num_reports, u8 report_id) {
    report_accumulator_t* report = &reports[report_id];
    if (!report->seen) {
        report->seen = true;
        report->report_id = report_id;
        order[(*num_reports)++] = report_id;
    }
}

static u16 resolved_usage_page(u32 usage, u16 default_page) {
    u16 page = (u16)(usage >> 16);
    return page != 0 ? page : default_page;
}

static u16 resolved_usage_id(u32 usage) {
    return (u16)(usage & 0xFFFFu);
}

static u32 resolve_variable_usage(const local_state_t* locals, u32 field_index) {
    if (field_index < locals->usage_count) {
        return locals->usages[field_index];
    }

    if (locals->has_usage_range) {
        u32 range_index = field_index - locals->usage_count;
        u32 candidate = locals->usage_minimum + range_index;
        return candidate <= locals->usage_maximum ? candidate : locals->usage_maximum;
    }

    if (locals->usage_count > 0) {
        return locals->usages[locals->usage_count - 1];
    }

    return 0;
}

static u32 resolve_array_usage(const local_state_t* locals) {
    if (locals->usage_count > 0) {
        return locals->usages[0];
    }

    if (locals->has_usage_range) {
        return locals->usage_minimum;
    }

    return 0;
}

static u16 build_input_flags(u32 raw_input_bits) {
    u16 flags = 0;
    if ((raw_input_bits & 0x01u) != 0) {
        flags |= USB_HID_INPUT_FIELD_CONSTANT;
    }
    if ((raw_input_bits & 0x02u) != 0) {
        flags |= USB_HID_INPUT_FIELD_VARIABLE;
    }
    if ((raw_input_bits & 0x04u) != 0) {
        flags |= USB_HID_INPUT_FIELD_RELATIVE;
    }
    return flags;
}

static b8 analyze_input_reports(const usb_hid_report_item_t* items, size_t num_items,
                                     report_accumulator_t* reports,
                                     u8* order,
                                     u16* out_num_reports,
                                     u32* out_num_fields,
                                     b8* out_uses_report_ids,
                                     u32* out_max_input_report_bytes) {
    global_state_t globals;
    memset(&globals, 0, sizeof(global_state_t));
    global_state_t global_stack[GLOBAL_STACK_DEPTH];
    memset(&global_stack, 0, sizeof(global_state_t) * GLOBAL_STACK_DEPTH);
    u8 stack_depth = 0;
    local_state_t locals;
    memset(&locals, 0, sizeof(local_state_t));

    *out_num_reports = 0;
    *out_num_fields = 0;
    *out_uses_report_ids = false;
    *out_max_input_report_bytes = 0;

    for (size_t i = 0; i < num_items; i++) {
        const usb_hid_report_item_t* item = &items[i];
        switch (item->type) {
            case USB_HID_ITEM_TYPE_GLOBAL:
                switch ((usb_hid_global_item_tag_t)(item->tag)) {
                    case USB_HID_GLOBAL_ITEM_TAG_USAGE_PAGE:
                        globals.usage_page = (u16)(item->data & 0xFFFFu);
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
                        globals.report_size = item->data;
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
                        globals.report_count = item->data;
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_REPORT_ID:
                        if (item->size == 0 || item->data == 0 || item->data > 0xFFu) {
                            return false;
                        }
                        if (reports[0].seen) {
                            return false;
                        }
                        globals.report_id = (u8)(item->data);
                        *out_uses_report_ids = true;
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:
                        globals.logical_minimum = sign_extend(item->data, item->size);
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:
                        globals.logical_maximum = sign_extend(item->data, item->size);
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_PUSH:
                        if (stack_depth >= GLOBAL_STACK_DEPTH) {
                            return false;
                        }
                        global_stack[stack_depth++] = globals;
                        break;
                    case USB_HID_GLOBAL_ITEM_TAG_POP:
                        if (stack_depth == 0) {
                            return false;
                        }
                        globals = global_stack[--stack_depth];
                        break;
                    default:
                        break;
                }
                break;

            case USB_HID_ITEM_TYPE_LOCAL:
                switch ((usb_hid_local_item_tag_t)(item->tag)) {
                case USB_HID_LOCAL_ITEM_TAG_USAGE:
                    if (locals.usage_count < MAX_EXPLICIT_USAGES) {
                        locals.usages[locals.usage_count++] = item->data;
                    }
                    break;
                case USB_HID_LOCAL_ITEM_TAG_USAGE_MINIMUM:
                    locals.usage_minimum = item->data;
                    locals.has_usage_range = true;
                    break;
                case USB_HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM:
                    locals.usage_maximum = item->data;
                    locals.has_usage_range = true;
                    break;
                default:
                    break;
                }
                break;

            case USB_HID_ITEM_TYPE_MAIN:
                switch ((usb_hid_main_item_tag_t)(item->tag)) {
                case USB_HID_MAIN_ITEM_TAG_INPUT: {
                    if (globals.report_size == 0 || globals.report_count == 0) {
                        return false;
                    }

                    if ((*out_uses_report_ids) && globals.report_id == 0) {
                        return false;
                    }

                    u8 report_id = globals.report_id;
                    register_input_report(reports, order, out_num_reports, report_id);

                    u64 item_bits = (u64)(globals.report_size) * (u64)(globals.report_count);
                    if (item_bits > 0xFFFFFFFFull) {
                        return false;
                    }

                    report_accumulator_t* report = &reports[report_id];
                    if (report->body_bits > 0xFFFFFFFFu - (u32)(item_bits)) {
                        return false;
                    }
                    report->body_bits += (u32)(item_bits);

                    if ((item->data & 0x01u) == 0) {
                        if (report->field_count > 0xFFFFFFFFu - globals.report_count) {
                            return false;
                        }
                        report->field_count += globals.report_count;
                        if ((*out_num_fields) > 0xFFFFFFFFu - globals.report_count) {
                            return false;
                        }
                        (*out_num_fields) += globals.report_count;
                    }

                    local_state_reset(&locals);
                    break;
                }
                case USB_HID_MAIN_ITEM_TAG_COLLECTION:
                case USB_HID_MAIN_ITEM_TAG_END_COLLECTION:
                case USB_HID_MAIN_ITEM_TAG_OUTPUT:
                case USB_HID_MAIN_ITEM_TAG_FEATURE:
                    local_state_reset(&locals);
                    break;
                default:
                    break;
                }
            break;

        default:
            break;
        }
    }

    if (stack_depth != 0 || (*out_num_reports) == 0) {
        return false;
    }

    for (u16 i = 0; i < out_num_reports; i++) {
        const report_accumulator_t* report = &reports[order[i]];
        u32 body_bytes = (report->body_bits + 7u) / 8u;
        u32 wire_bytes = body_bytes + (out_uses_report_ids ? 1u : 0u);
        if (wire_bytes > (*out_max_input_report_bytes)) {
            *out_max_input_report_bytes = wire_bytes;
        }
    }

    return true;
}

static b8 build_input_layout(const usb_hid_report_item_t* items, size_t num_items,
                                  const report_accumulator_t* reports,
                                  const u8* order,
                                  u16 num_reports,
                                  b8 uses_report_ids,
                                  usb_hid_report_layout_t* out) {
    u16 report_index_by_id[256];
    u16 next_field_cursor_by_id[256];
    u32 current_bit_offset_by_id[256];
    memset(current_bit_offset_by_id, 0, sizeof(u32) * 256);

    for (u16 i = 0; i < 256; i++) {
        report_index_by_id[i] = INVALID_REPORT_INDEX;
        next_field_cursor_by_id[i] = 0;
    }

    u32 next_field_begin = 0;
    for (u16 i = 0; i < num_reports; i++) {
        const report_accumulator_t* acc = &reports[order[i]];
        if (acc->field_count > 0xFFFFu || next_field_begin > 0xFFFFu ||
            next_field_begin + acc->field_count > 0xFFFFu) {
            return false;
        }

        usb_hid_input_report_info_t* report = &out->input_reports[i];
        report->report_id = acc->report_id;
        report->byte_length = (acc->body_bits + 7u) / 8u;
        report->field_begin = (u16)(next_field_begin);
        report->field_count = (u16)(acc->field_count);
        report_index_by_id[acc->report_id] = i;
        next_field_cursor_by_id[acc->report_id] = report->field_begin;
        next_field_begin += acc->field_count;
    }

    global_state_t globals;
    memset(&globals, 0, sizeof(global_state_t));
    global_state_t global_stack[GLOBAL_STACK_DEPTH];
    memset(&global_stack, 0, sizeof(global_state_t) * GLOBAL_STACK_DEPTH);
    u8 stack_depth = 0;
    local_state_t locals;
    memset(&locals, 0, sizeof(local_state_t));

    for (size_t i = 0; i < num_items; i++) {
        const usb_hid_report_item_t* item = &items[i];
        switch (item->type) {
        case USB_HID_ITEM_TYPE_GLOBAL:
            switch ((usb_hid_global_item_tag_t)(item->tag)) {
            case USB_HID_GLOBAL_ITEM_TAG_USAGE_PAGE:
                globals.usage_page = (u16)(item->data & 0xFFFFu);
                break;
            case USB_HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
                globals.report_size = item->data;
                break;
            case USB_HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
                globals.report_count = item->data;
                break;
            case USB_HID_GLOBAL_ITEM_TAG_REPORT_ID:
                if (item->size == 0 || item->data == 0 || item->data > 0xFFu) {
                    return false;
                }
                globals.report_id = (u8)(item->data);
                break;
            case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:
                globals.logical_minimum = sign_extend(item->data, item->size);
                break;
            case USB_HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:
                globals.logical_maximum = sign_extend(item->data, item->size);
                break;
            case USB_HID_GLOBAL_ITEM_TAG_PUSH:
                if (stack_depth >= GLOBAL_STACK_DEPTH) {
                    return false;
                }
                global_stack[stack_depth++] = globals;
                break;
            case USB_HID_GLOBAL_ITEM_TAG_POP:
                if (stack_depth == 0) {
                    return false;
                }
                globals = global_stack[--stack_depth];
                break;
            default:
                break;
            }
            break;

        case USB_HID_ITEM_TYPE_LOCAL:
            switch ((usb_hid_local_item_tag_t)(item->tag)) {
            case USB_HID_LOCAL_ITEM_TAG_USAGE:
                if (locals.usage_count < MAX_EXPLICIT_USAGES) {
                    locals.usages[locals.usage_count++] = item->data;
                }
                break;
            case USB_HID_LOCAL_ITEM_TAG_USAGE_MINIMUM:
                locals.usage_minimum = item->data;
                locals.has_usage_range = true;
                break;
            case USB_HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM:
                locals.usage_maximum = item->data;
                locals.has_usage_range = true;
                break;
            default:
                break;
            }
            break;

        case USB_HID_ITEM_TYPE_MAIN:
            switch ((usb_hid_main_item_tag_t)(item->tag)) {
            case USB_HID_MAIN_ITEM_TAG_INPUT: {
                if (globals.report_size == 0 || globals.report_count == 0 ||
                    globals.report_size > 0xFFFFu) {
                    return false;
                }

                if (uses_report_ids && globals.report_id == 0) {
                    return false;
                }

                u8 report_id = globals.report_id;
                u16 report_index = report_index_by_id[report_id];
                if (report_index == INVALID_REPORT_INDEX) {
                    return false;
                }

                u32 item_bits = globals.report_size * globals.report_count;
                u16 input_flags = build_input_flags(item->data);
                b8 is_constant = (input_flags & USB_HID_INPUT_FIELD_CONSTANT) != 0;
                u32 current_bit_offset = current_bit_offset_by_id[report_id];

                if (!is_constant) {
                    usb_hid_input_report_info_t* report = &out->input_reports[report_index];
                    u16 cursor = next_field_cursor_by_id[report_id];
                    u32 usage_template = (input_flags & USB_HID_INPUT_FIELD_VARIABLE) ? 0 : resolve_array_usage(&locals);

                    for (u32 j = 0; j < globals.report_count; j++) {
                        if (!out->fields ||
                            cursor >= (u16)(report->field_begin + report->field_count)) {
                            return false;
                        }

                        u32 usage_value = (input_flags & USB_HID_INPUT_FIELD_VARIABLE) ? resolve_variable_usage(&locals, j) : usage_template;

                        out->fields[cursor++] = (usb_hid_field_info_t){
                            .bit_offset = current_bit_offset,
                            .bit_size = (u16)(globals.report_size),
                            .report_id = report_id,
                            .usage_page = resolved_usage_page(usage_value, globals.usage_page),
                            .usage = resolved_usage_id(usage_value),
                            .input_flags = input_flags,
                            .logical_minimum = globals.logical_minimum,
                            .logical_maximum = globals.logical_maximum,
                        };
                        current_bit_offset += globals.report_size;
                    }

                    next_field_cursor_by_id[report_id] = cursor;
                } else {
                    current_bit_offset += item_bits;
                }

                current_bit_offset_by_id[report_id] = current_bit_offset;
                local_state_reset(&locals);
                break;
            }
            case USB_HID_MAIN_ITEM_TAG_COLLECTION:
            case USB_HID_MAIN_ITEM_TAG_END_COLLECTION:
            case USB_HID_MAIN_ITEM_TAG_OUTPUT:
            case USB_HID_MAIN_ITEM_TAG_FEATURE:
                local_state_reset(&locals);
                break;
            default:
                break;
            }
            break;

        default:
            break;
        }
    }

    if (stack_depth != 0) {
        return false;
    }

    for (u16 i = 0; i < num_reports; i++) {
        u8 report_id = order[i];
        if (current_bit_offset_by_id[report_id] != reports[report_id].body_bits) {
            return false;
        }
    }

    return true;
}

void usb_hid_report_layout_destroy(usb_hid_report_layout_t* layout) {
    if (layout->fields) {
        kfree(layout->fields);
        layout->fields = NULL;
    }

    if (layout->input_reports) {
        kfree(layout->input_reports);
        layout->input_reports = NULL;
    }

    layout->num_fields = 0;
    layout->num_input_reports = 0;
    layout->uses_report_ids = false;
    layout->max_input_report_bytes = 0;
}

const usb_hid_input_report_info_t* usb_hid_find_input_report(const usb_hid_report_layout_t* layout, u8 report_id) {
    for (u16 i = 0; i < layout->num_input_reports; ++i) {
        if (layout->input_reports[i].report_id == report_id) {
            return &layout->input_reports[i];
        }
    }

    return NULL;
}

const usb_hid_field_info_t* usb_hid_report_fields(const usb_hid_report_layout_t* layout, const usb_hid_input_report_info_t* report) {
    if (!layout->fields || report->field_begin >= layout->num_fields) {
        return NULL;
    }

    return layout->fields + report->field_begin;
}

b8 usb_hid_parse_report_descriptor(const u8* descriptor, size_t length, usb_hid_report_layout_t* out) {
    usb_hid_report_layout_destroy(out);

    if (!descriptor || length == 0) {
        return false;
    }

    size_t item_count = 0;
    if ((!count_items(descriptor, length, &item_count)) || item_count == 0) {
        return false;
    }

    usb_hid_report_item_t* items = (usb_hid_report_item_t*)kmalloc(item_count * sizeof(usb_hid_report_item_t));
    if (!items) {
        xassert(false, "");
        return false;
    }

    size_t parsed_count = 0;
    if (!parse_items(descriptor, length, items, item_count, &parsed_count) || parsed_count != item_count) {
        xassert(false, "");
        kfree(items);
        return false;
    }

    report_accumulator_t reports[256];
    memset(&reports, 0, sizeof(report_accumulator_t) * 256);
    u8 report_order[256];
    memset(&report_order, 0, sizeof(u8) * 256);
    u16 num_reports = 0;
    u32 num_fields = 0;
    b8 uses_report_ids = false;
    u32 max_input_report_bytes = 0;

    b8 rc = analyze_input_reports(items, parsed_count, reports, report_order, &num_reports, &num_fields, &uses_report_ids, &max_input_report_bytes);

    if (!rc || num_reports == 0 || num_fields > 0xFFFFu) {
        kfree(items);
        return false;
    }

    out->input_reports = (usb_hid_input_report_info_t*)kalloc(num_reports * sizeof(usb_hid_input_report_info_t));
    if (!out->input_reports) {
        kfree(items);
        return false;
    }

    if (num_fields > 0) {
        out->fields = (usb_hid_field_info_t*)kmalloc(num_fields * sizeof(usb_hid_field_info_t));
        if (!out->fields) {
            kfree(items);
            usb_hid_report_layout_destroy(out);
            return false;
        }
    }

    out->num_fields = (u16)num_fields;
    out->num_input_reports = num_reports;
    out->uses_report_ids = uses_report_ids;
    out->max_input_report_bytes = max_input_report_bytes;

    rc = build_input_layout(items, parsed_count, reports, report_order, num_reports, uses_report_ids, out);
    kfree(items);
    if (rc != 0) {
        usb_hid_report_layout_destroy(out);
        return false;
    }

    return true;
}

u32 usb_hid_read_field_unsigned(const u8* data, u32 length, u32 bit_offset, u16 bit_size) {
    if (!data || bit_size == 0) {
        return 0;
    }

    u32 byte_offset = bit_offset / 8u;
    u32 bit_shift = bit_offset % 8u;
    u32 bytes_needed = (bit_shift + bit_size + 7u) / 8u;
    if (byte_offset >= length) {
        return 0;
    }

    if (byte_offset + bytes_needed > length) {
        bytes_needed = length - byte_offset;
    }

    u32 raw = 0;
    memcpy(&raw, data + byte_offset, bytes_needed > 4 ? 4 : bytes_needed);
    raw >>= bit_shift;
    if (bit_size < 32) {
        raw &= (1u << bit_size) - 1u;
    }
    return raw;
}

i32 usb_hid_read_field_signed(const u8* data, u32 length, u32 bit_offset, u16 bit_size) {
    if (!data || bit_size == 0) {
        return 0;
    }

    u32 byte_offset = bit_offset / 8u;
    u32 bit_shift = bit_offset % 8u;
    u32 bytes_needed = (bit_shift + bit_size + 7u) / 8u;
    if (byte_offset >= length) {
        return 0;
    }

    if (byte_offset + bytes_needed > length) {
        bytes_needed = length - byte_offset;
    }

    u32 raw = 0;
    memcpy(&raw, data + byte_offset, bytes_needed > 4 ? 4 : bytes_needed);
    raw >>= bit_shift;
    if (bit_size >= 32) {
        return (i32)(raw);
    }

    raw &= (1u << bit_size) - 1u;
    i32 shift = 32 - bit_size;
    return (i32)(raw << shift) >> shift;
}