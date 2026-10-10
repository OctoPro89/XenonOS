#include <windowing/window/window.h>
#include <windowing/window/events.h>
#include <windowing/window/internal.h>
#include <arch/x86_64/sync/sync.h>
#include <xlibc/string.h>
#include <xlibc/stdlib.h>
#include <kernel.h>

// TODO: check synchronization

static void notify_compositor();

static struct window window_slots[WINDOW_MAX_COUNT];
static window_t* z_order[WINDOW_MAX_COUNT];

static u32 active_count;
static window_id_t next_id = 1;

static spinlock_t window_server_lock;

static window_invalidate_callback_t invalidate_callback;

static void copy_title(char* dst, const char* src) {
    u32 i = 0;

    if (!src) src = "";

    while (i + 1 < WINDOW_TITLE_MAX && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static __hint_inline__ b8 valid_window(const window_t* w) {
    return w && w->alive;
}

static __hint_inline__ void queue_event(window_t* w, const window_event_t* event) {
    if (!valid_window(w) || !event) { return; }

    ring_buffer_push(w->events, event);
}

void window_server_init() {
    spin_lock_init(&window_server_lock);

    // TODO: not sure if this needs to be locked, should be fine as long as only window server is using it
    memset(&window_slots, 0, sizeof(struct window) * WINDOW_MAX_COUNT);
    memset(&z_order, 0, sizeof(struct window*) * WINDOW_MAX_COUNT);
    invalidate_callback = NULL;

    active_count = 0;
    next_id = 1;
}

window_t* window_create(const window_config_t* config) {
    if (!config) { return NULL; }

    if (active_count >= WINDOW_MAX_COUNT) {
        return NULL;
    }

    struct window* w = NULL;

    // TODO: I believe this needs a lock
    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);

    for (u32 i = 0; i < WINDOW_MAX_COUNT; ++i) {
        if (!window_slots[i].alive) {
            w = &window_slots[i];
            break;
        }
    }

    if (!w) {
        spin_unlock_irqrestore(&window_server_lock, flags);
        return NULL;
    }

    // do modifying statics here
    memset(w, 0, sizeof(struct window));
    w->id = next_id++;
    if (next_id == 0) { next_id = 1; }

    w->rect = config->rect;
    w->type = config->type;
    w->flags = config->flags;
    w->state = WINDOW_STATE_NORMAL;
    w->alive = true;

    copy_title(w->title, config->title);

    w->events = ring_buffer_create(WINDOW_EVENT_QUEUE_SIZE, sizeof(window_event_t));

    z_order[active_count++] = w;

    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_REDRAW; // initial paint
    queue_event(w, &event); // TODO: shouldn't need a lock here since ringbuffer_t has it's own but be safe ig?
    
    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();

    return w;
}

void window_destroy(window_t* window) {
    if (!valid_window(window)) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);

    for (u32 i = 0; i < active_count; ++i) {
        if (z_order[i] != window) { continue; }

        for (u32 j = i; j + 1 < active_count; ++j) {
            z_order[j] = z_order[j + 1];
        }

        z_order[--active_count] = NULL;
        break;
    }

    window->alive = false;
    window->focused = false;
    window->surface = NULL;
    ring_buffer_destroy(window->events); // TODO: ensure readers and writeers can't retain references while destruction occurs
    window->events = NULL;

    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();
}

void window_set_title(window_t* window, const char* title) {
    if (!valid_window(window)) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);
    copy_title(window->title, title);
    spin_unlock_irqrestore(&window_server_lock, flags);
    /*
    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_REDRAW; // TODO: Maybe this should cause a redraw idk
    queue_event(window, &event);
    */

    notify_compositor();
}

void window_set_position(window_t* window, i32 x, i32 y) {
    if (!valid_window(window)) { return; }

    if (window->rect.x == x && window->rect.y == y) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);
    window->rect.x = x;
    window->rect.y = y;

    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_MOVED;
    queue_event(window, &event); // TODO: shouldn't need a lock here since ringbuffer_t has it's own but be safe ig?
    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();
}

void window_set_size(window_t* window, i32 width, i32 height) {
    if (!valid_window(window)) { return; }
    if (width == 0 || height == 0) { return; }

    if (window->rect.width == width && window->rect.height == height) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);
    window->damaged = true;
    window->damage = (window_rect_t){ .x = 0, .y = 0, .width = width, .height = height };

    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_RESIZED;
    queue_event(window, &event);
    event.type = WINDOW_EVENT_REDRAW;
    queue_event(window, &event);
    spin_unlock_irqrestore(&window_server_lock, flags); // TODO: shouldn't need a lock here since ringbuffer_t has it's own but be safe ig?
    
    notify_compositor();
}

// TODO: window_set_state

void window_raise(window_t* window) {
    if (!valid_window(window)) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);

    u32 index = active_count;

    for (u32 i = 0; i < active_count; ++i) {
        if (z_order[i] == window) {
            index = i;
            break;
        }
    }

    if (index == active_count) { return; }

    for (u32 i = index; i + 1 < active_count; ++i) {
        z_order[i] = z_order[i + 1];
    }

    z_order[active_count - 1] = window;

    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();
}

window_rect_t window_get_rect(const window_t* window) {
    return valid_window(window) ? window->rect : (window_rect_t){ 0, 0, 0, 0 };
}

b8 window_set_surface(window_t* window, window_surface_t* surface) {
    if (!valid_window(window)) return false;

    if (surface) {
        if (!surface->pixels || !surface->width || !surface->height || surface->stride < surface->width * sizeof(u32)) {
            return false;
        }
    }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);
    window->surface = surface;
    window->damaged = true;
    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_REDRAW;
    queue_event(window, &event); // TODO: shouldn't need a lock here since ringbuffer_t has it's own but be safe ig?
    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();

    return true;
}

window_surface_t* window_get_surface(window_t* window) {
    return valid_window(window) ? window->surface : NULL;
}

void window_request_close(window_t* window) {
    if (!window) {
        return;
    }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);

    if (!valid_window(window)) {
        spin_unlock_irqrestore(&window_server_lock, flags);
        return;
    }

    window_event_t event;
    event.buttons = 0;
    event.keycode = 0;
    event.type = WINDOW_EVENT_CLOSE_REQUEST;

    queue_event(window, &event);

    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();
}

void window_damage(window_t* window, i32 x, i32 y, i32 width, i32 height) {
    if (!valid_window(window)) { return; }
    if (width == 0 || height == 0) { return; }
    
    window_surface_t* s = window->surface;
    if (!s) { return; }

    if (x >= (i32)s->width || y >= (i32)s->height) { return; }

    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);

    // clip to bounds
    if (width > ((i32)(s->width) - x)) { width = (((i32)(s->width) - x)); }
    if (height > ((i32)(s->height) - y)) { height = (((i32)(s->height) - y)); }

    if (width == 0 || height == 0) {
        spin_unlock_irqrestore(&window_server_lock, flags);
        return;
    }

    if (!window->damaged) {
        window->damage = (window_rect_t){ .x = x, .y = y, .width = width, .height = height };
        window->damaged = true;
        spin_unlock_irqrestore(&window_server_lock, flags);
        return;
    }

    // union with existing damaged rect
    i32 x1 = window->damage.x < x ? window->damage.x : x;
    i32 y1 = window->damage.y < y ? window->damage.y : y;

    i32 old_x2 = window->damage.x + window->damage.width;
    i32 old_y2 = window->damage.y + window->damage.height;
    i32 new_x2 = x + width;
    i32 new_y2 = y + height;

    i32 x2 = old_x2 > new_x2 ? old_x2 : new_x2;
    i32 y2 = old_y2 > new_y2 ? old_y2 : new_y2;

    window->damage = (window_rect_t){ .x = x1, .y = y1, .width = x2 - x1, .height = y2 - y1 };
    spin_unlock_irqrestore(&window_server_lock, flags);

    notify_compositor();
}


void window_server_set_invalidate_callback(window_invalidate_callback_t callback) {
    u64 flags = 0;
    spin_lock_irqsave(&window_server_lock, &flags);
    invalidate_callback = callback;
    spin_unlock_irqrestore(&window_server_lock, flags);
}

/*
 * call only after releasing window_server_lock
 * copying the function pointer under the lock prevents a data race
 * with window_server_set_invalidate_callback()
 */
static void notify_compositor() {
    window_invalidate_callback_t callback;
    u64 flags = 0;

    spin_lock_irqsave(&window_server_lock, &flags);
    callback = invalidate_callback;
    spin_unlock_irqrestore(&window_server_lock, flags);

    if (callback) {
        callback();
    }
}

u32 window_server_snapshot(window_snapshot_t* out, u32 capacity) {
    if (!out || capacity == 0) {
        return 0;
    }

    u32 count = 0;
    u64 flags = 0;

    spin_lock_irqsave(&window_server_lock, &flags);

    // z_order is maintained back-to-front
    u32 limit = active_count < capacity ? active_count : capacity;

    for (u32 i = 0; i < limit; ++i) {
        window_t* w = z_order[i];

        if (!w || !w->alive) {
            continue;
        }

        window_snapshot_t* dst = &out[count++];

        dst->handle = w;
        dst->id = w->id;
        dst->rect = w->rect;
        dst->type = w->type;
        dst->state = w->state;
        dst->flags = w->flags;
        dst->focused = w->focused;
        dst->surface = w->surface;

        copy_title(dst->title, w->title);
    }

    spin_unlock_irqrestore(&window_server_lock, flags);

    return count;
}