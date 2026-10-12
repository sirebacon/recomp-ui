#include "recomp_runtime_ui_internal.h"
#include "launcher_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int same_text(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    return strcmp(a, b) == 0;
}

int recomp_runtime_ui_item_enabled(const RecompRuntimeUi *ui,
                                   const RecompRuntimeUiItem *item) {
    if (!ui->config.callbacks.is_enabled) return 1;
    return ui->config.callbacks.is_enabled(ui->config.callbacks.context, item) != 0;
}

size_t recomp_runtime_ui_section_item_count(const RecompRuntimeUi *ui,
                                            size_t section) {
    size_t count = 0;
    if (!ui || section >= ui->section_count) return 0;
    for (size_t i = 0; i < ui->config.item_count; ++i)
        if (same_text(ui->config.items[i].section, ui->sections[section])) ++count;
    return count;
}

const RecompRuntimeUiItem *recomp_runtime_ui_section_item(
    const RecompRuntimeUi *ui, size_t section, size_t row) {
    size_t found = 0;
    if (!ui || section >= ui->section_count) return NULL;
    for (size_t i = 0; i < ui->config.item_count; ++i) {
        if (!same_text(ui->config.items[i].section, ui->sections[section])) continue;
        if (found++ == row) return &ui->config.items[i];
    }
    return NULL;
}

/* ---- nested pages -------------------------------------------------------- */

static const char *item_path(const RecompRuntimeUiItem *item) {
    return item->section && *item->section ? item->section : "Settings";
}

static size_t first_component_len(const char *path) {
    const char *slash = strchr(path, '/');
    return slash ? (size_t)(slash - path) : strlen(path);
}

/* Full path of the open page: "<section>" or "<section>/<page>". */
static void open_page_path(const RecompRuntimeUi *ui, char *out, size_t size) {
    if (!ui->page[0]) snprintf(out, size, "%s", ui->sections[ui->section_index]);
    else snprintf(out, size, "%s/%s", ui->sections[ui->section_index], ui->page);
}

/* 1: path is the page itself. 2: path is below it (*rest = what follows the
 * page and its '/'). 0: unrelated. */
static int path_relation(const char *path, const char *page, const char **rest) {
    const size_t n = strlen(page);
    if (strncmp(path, page, n) != 0) return 0;
    if (path[n] == '\0') return 1;
    if (path[n] == '/' && path[n + 1]) { *rest = path + n + 1; return 2; }
    return 0;
}

/* Walk the open page's rows in order. Returns the row count, or stops at
 * `want` and fills item/folder for it. */
static size_t page_walk(const RecompRuntimeUi *ui, size_t want,
                        const RecompRuntimeUiItem **item_out,
                        const char **folder_out, size_t *folder_len_out) {
    char page[256];
    size_t row = 0;
    if (!ui || ui->section_index >= ui->section_count) return 0;
    open_page_path(ui, page, sizeof(page));
    for (size_t i = 0; i < ui->config.item_count; ++i) {
        const char *rest = NULL;
        const int rel = path_relation(item_path(&ui->config.items[i]), page, &rest);
        if (rel == 0) continue;
        size_t folder_len = 0;
        if (rel == 2) {
            /* One folder row per next component, at its first appearance. */
            folder_len = first_component_len(rest);
            int seen = 0;
            for (size_t j = 0; j < i && !seen; ++j) {
                const char *prev = NULL;
                if (path_relation(item_path(&ui->config.items[j]), page, &prev) == 2 &&
                    first_component_len(prev) == folder_len &&
                    strncmp(prev, rest, folder_len) == 0)
                    seen = 1;
            }
            if (seen) continue;
        }
        if (row == want) {
            if (item_out) *item_out = rel == 1 ? &ui->config.items[i] : NULL;
            if (folder_out) *folder_out = rel == 2 ? rest : NULL;
            if (folder_len_out) *folder_len_out = folder_len;
            return row;
        }
        ++row;
    }
    return row;
}

size_t recomp_runtime_ui_page_entry_count(const RecompRuntimeUi *ui) {
    return page_walk(ui, (size_t)-1, NULL, NULL, NULL);
}

const RecompRuntimeUiItem *recomp_runtime_ui_page_entry(
    const RecompRuntimeUi *ui, size_t row, char *folder_out, size_t folder_size) {
    const RecompRuntimeUiItem *item = NULL;
    const char *folder = NULL;
    size_t folder_len = 0;
    if (folder_out && folder_size) folder_out[0] = '\0';
    if (row >= recomp_runtime_ui_page_entry_count(ui)) return NULL;
    page_walk(ui, row, &item, &folder, &folder_len);
    if (folder && folder_out && folder_size) {
        if (folder_len >= folder_size) folder_len = folder_size - 1;
        memcpy(folder_out, folder, folder_len);
        folder_out[folder_len] = '\0';
    }
    return item;
}

void recomp_runtime_ui_enter_folder(RecompRuntimeUi *ui, const char *name) {
    if (!ui || !name || !*name || ui->page_depth >= RECOMP_RUNTIME_UI_MAX_DEPTH) return;
    const size_t used = strlen(ui->page);
    const size_t need = strlen(name) + (used ? 1 : 0);
    if (used + need >= sizeof(ui->page)) return;
    ui->row_stack[ui->page_depth++] = ui->row_index;
    if (used) strcat(ui->page, "/");
    strcat(ui->page, name);
    ui->row_index = 0;
}

int recomp_runtime_ui_leave_folder(RecompRuntimeUi *ui) {
    if (!ui || !ui->page[0]) return 0;
    char *slash = strrchr(ui->page, '/');
    if (slash) *slash = '\0';
    else ui->page[0] = '\0';
    ui->row_index = ui->page_depth ? ui->row_stack[--ui->page_depth] : 0;
    return 1;
}

void recomp_runtime_ui_page_title(const RecompRuntimeUi *ui, char *out,
                                  size_t out_size) {
    if (!out || !out_size) return;
    out[0] = '\0';
    if (!ui || ui->section_index >= ui->section_count) return;
    char path[256];
    open_page_path(ui, path, sizeof(path));
    size_t o = 0;
    for (const char *p = path; *p && o + 4 < out_size; ++p) {
        if (*p == '/') { memcpy(out + o, " > ", 3); o += 3; }
        else out[o++] = *p;
    }
    out[o] = '\0';
}

static void reset_page(RecompRuntimeUi *ui) {
    ui->page[0] = '\0';
    ui->page_depth = 0;
}

static void visibility(RecompRuntimeUi *ui, int open) {
    if (!ui || ui->open == open) return;
    ui->open = open;
    if (!open) { ui->in_section = 0; reset_page(ui); }
    if (ui->config.callbacks.visibility_changed)
        ui->config.callbacks.visibility_changed(ui->config.callbacks.context, open);
}

RecompRuntimeUi *recomp_runtime_ui_create(const RecompRuntimeUiConfig *config) {
    if (!config || !config->items || config->item_count == 0) return NULL;
    RecompRuntimeUi *ui = (RecompRuntimeUi *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->config = *config;
    ui->sections = (const char **)calloc(config->item_count, sizeof(*ui->sections));
    if (!ui->sections) {
        free(ui);
        return NULL;
    }
    /* Top-level sections are the first path components, in first-seen order. */
    for (size_t i = 0; i < config->item_count; ++i) {
        const char *path = item_path(&config->items[i]);
        const size_t len = first_component_len(path);
        int known = 0;
        for (size_t j = 0; j < ui->section_count && !known; ++j)
            if (strlen(ui->sections[j]) == len && strncmp(ui->sections[j], path, len) == 0)
                known = 1;
        if (known) continue;
        char *name = (char *)malloc(len + 1);
        if (!name) continue;
        memcpy(name, path, len);
        name[len] = '\0';
        ui->sections[ui->section_count++] = name;
    }
    return ui;
}

void recomp_runtime_ui_destroy(RecompRuntimeUi *ui) {
    if (!ui) return;
    if (ui->open && ui->config.callbacks.visibility_changed)
        ui->config.callbacks.visibility_changed(ui->config.callbacks.context, 0);
    for (size_t i = 0; i < ui->section_count; ++i) free((void *)ui->sections[i]);
    free(ui->sections);
    free(ui->owned_items);
    free(ui->owned_view_choices);
    free(ui->owned_view_values);
    free(ui);
}

int recomp_runtime_ui_is_open(const RecompRuntimeUi *ui) {
    return ui && ui->open;
}

void recomp_runtime_ui_open(RecompRuntimeUi *ui) {
    if (!ui) return;
    ui->in_section = 0;
    ui->row_index = 0;
    reset_page(ui);
    visibility(ui, 1);
}

void recomp_runtime_ui_close(RecompRuntimeUi *ui) {
    /* Abandon any in-progress edit, so reopening never resumes a stale buffer
     * and wants_text_input cannot stay stuck on with the menu shut. */
    if (ui) ui->editing_text = 0;
    visibility(ui, 0);
}

static size_t wrap_index(size_t current, int delta, size_t count) {
    if (count == 0) return 0;
    if (delta < 0) return current == 0 ? count - 1 : current - 1;
    return current + 1 >= count ? 0 : current + 1;
}

int recomp_runtime_ui_current_value(RecompRuntimeUi *ui,
                                    const RecompRuntimeUiItem *item,
                                    int *value) {
    if (!ui->config.callbacks.get_value || !value) return 0;
    return ui->config.callbacks.get_value(ui->config.callbacks.context,
                                          item, value) != 0;
}

static void accepted_change(RecompRuntimeUi *ui, const char *status) {
    snprintf(ui->status, sizeof(ui->status), "%s", status ? status : "Applied");
    ui->status_frames = 90;
    if (ui->config.callbacks.save)
        ui->config.callbacks.save(ui->config.callbacks.context);
}

static void accepted_action(RecompRuntimeUi *ui) {
    snprintf(ui->status, sizeof(ui->status), "%s", "Done");
    ui->status_frames = 90;
}

void recomp_runtime_ui_adjust_current(RecompRuntimeUi *ui, int direction,
                                      int activate, int repeat) {
    char folder[96];
    const RecompRuntimeUiItem *item = recomp_runtime_ui_page_entry(
        ui, ui->row_index, folder, sizeof(folder));
    if (!item) {
        /* A folder row opens on accept; left/right do nothing. */
        if (folder[0] && activate && !repeat) recomp_runtime_ui_enter_folder(ui, folder);
        return;
    }
    if (!recomp_runtime_ui_item_enabled(ui, item)) return;
    if (item->type == RECOMP_RUNTIME_UI_GRID) {
        /* Accept acts on the cursor tile; left/right move the cursor. */
        if (!activate) {
            recomp_runtime_ui_grid_move(ui, item, direction < 0 ? -1 : 1, 0);
            return;
        }
        if (repeat || !ui->config.callbacks.run_action) return;
        if (recomp_runtime_ui_grid_cell_count(ui, item) > 0 &&
            ui->config.callbacks.run_action(ui->config.callbacks.context, item))
            accepted_action(ui);
        return;
    }
    if (item->type == RECOMP_RUNTIME_UI_ACTION) {
        if (!activate || repeat || !ui->config.callbacks.run_action) return;
        if (ui->config.callbacks.run_action(ui->config.callbacks.context, item))
            accepted_action(ui);
        return;
    }
    if (item->type == RECOMP_RUNTIME_UI_TEXT) {
        /* Activation opens the editor; the backend drives it from there. */
        if (!activate || repeat) return;
        recomp_runtime_ui_current_text(ui, item, ui->edit_buffer,
                                       sizeof(ui->edit_buffer));
        ui->editing_text = 1;
        return;
    }
    if (!ui->config.callbacks.set_value) return;
    int value = 0;
    if (!recomp_runtime_ui_current_value(ui, item, &value)) return;
    int next = value;
    if (item->type == RECOMP_RUNTIME_UI_BOOL) {
        if (repeat) return;
        next = !value;
    } else if (item->type == RECOMP_RUNTIME_UI_CHOICE) {
        int count = item->choice_count ? (int)item->choice_count
                                       : item->maximum - item->minimum + 1;
        if (count <= 0) return;
        int base = value - item->minimum;
        if (item->choice_values) {
            base = 0;
            for (int i = 0; i < count; ++i)
                if (item->choice_values[i] == value) { base = i; break; }
        }
        int delta = direction < 0 ? -1 : 1;
        base = (base + delta + count) % count;
        next = item->choice_values ? item->choice_values[base]
                                   : item->minimum + base;
    } else {
        int step = item->step > 0 ? item->step : 1;
        next = value + (direction < 0 ? -step : step);
        if (next < item->minimum) next = item->minimum;
        if (next > item->maximum) next = item->maximum;
    }
    if (next != value && ui->config.callbacks.set_value(
            ui->config.callbacks.context, item, next))
        accepted_change(ui, "Saved");
}

void recomp_runtime_ui_current_text(RecompRuntimeUi *ui,
                                    const RecompRuntimeUiItem *item, char *buf,
                                    size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    if (!ui || !item || !ui->config.callbacks.get_text) return;
    if (!ui->config.callbacks.get_text(ui->config.callbacks.context, item, buf,
                                       buf_size))
        buf[0] = '\0';
    buf[buf_size - 1] = '\0';
}

int recomp_runtime_ui_commit_text(RecompRuntimeUi *ui,
                                  const RecompRuntimeUiItem *item,
                                  const char *value) {
    if (!ui || !item || !value || !ui->config.callbacks.set_text) return 0;
    if (!ui->config.callbacks.set_text(ui->config.callbacks.context, item,
                                       value))
        return 0;
    accepted_change(ui, "Saved");
    return 1;
}

int recomp_runtime_ui_grid_columns(const RecompRuntimeUiItem *item) {
    return item && item->step > 0 ? item->step : 1;
}

int recomp_runtime_ui_grid_cell_count(RecompRuntimeUi *ui,
                                      const RecompRuntimeUiItem *item) {
    if (!ui || !item || !ui->config.callbacks.get_cell_count) return 0;
    int n = ui->config.callbacks.get_cell_count(ui->config.callbacks.context, item);
    if (n < 0) n = 0;
    if (n > item->maximum + 1) n = item->maximum + 1;
    return n;
}

int recomp_runtime_ui_grid_cell(RecompRuntimeUi *ui,
                                const RecompRuntimeUiItem *item, int index,
                                char *label, size_t label_size,
                                unsigned *flags) {
    if (label && label_size) label[0] = '\0';
    if (flags) *flags = 0;
    if (!ui || !item || !label || !label_size || !ui->config.callbacks.get_cell)
        return 0;
    unsigned f = 0;
    if (!ui->config.callbacks.get_cell(ui->config.callbacks.context, item, index,
                                       label, label_size, &f)) {
        label[0] = '\0';
        return 0;
    }
    label[label_size - 1] = '\0';
    if (flags) *flags = f;
    return 1;
}

int recomp_runtime_ui_grid_move(RecompRuntimeUi *ui,
                                const RecompRuntimeUiItem *item, int dx,
                                int dy) {
    const int count = recomp_runtime_ui_grid_cell_count(ui, item);
    const int cols = recomp_runtime_ui_grid_columns(item);
    int cursor = 0;
    if (count <= 0) return dy == 0;
    if (!recomp_runtime_ui_current_value(ui, item, &cursor)) cursor = 0;
    if (cursor < 0 || cursor >= count) cursor = 0;
    int next = cursor;
    if (dy) {
        next = cursor + dy * cols;
        if (next < 0 || next >= count) {
            /* Down from a short last row's neighbour still lands on the last
             * tile; only a move past the grid's edge leaves it. */
            const int row = cursor / cols, last_row = (count - 1) / cols;
            if (dy > 0 && row < last_row) next = count - 1;
            else return 0;
        }
    } else {
        next = cursor + dx;
        if (next < 0) next = 0;
        if (next >= count) next = count - 1;
    }
    if (next != cursor && ui->config.callbacks.set_value)
        ui->config.callbacks.set_value(ui->config.callbacks.context, item, next);
    return 1;
}

int recomp_runtime_ui_wants_text_input(const RecompRuntimeUi *ui) {
    return (ui && ui->open && ui->editing_text) ? 1 : 0;
}

void recomp_runtime_ui_enter_section(RecompRuntimeUi *ui, size_t section) {
    if (!ui || section >= ui->section_count) return;
    ui->section_index = section;
    ui->row_index = 0;
    ui->in_section = 1;
    reset_page(ui);
}

void recomp_runtime_ui_leave_section(RecompRuntimeUi *ui) {
    if (!ui) return;
    ui->in_section = 0;
    ui->row_index = 0;
    reset_page(ui);
}

int recomp_runtime_ui_handle_input(RecompRuntimeUi *ui,
                                   RecompRuntimeUiInput input,
                                   int pressed, int repeat) {
    if (!ui) return 0;
    if (!ui->open) {
        if (pressed && !repeat && input == RECOMP_RUNTIME_UI_INPUT_TOGGLE) {
            recomp_runtime_ui_open(ui);
            return 1;
        }
        return 0;
    }
    if (!pressed) return 1;
    if (input == RECOMP_RUNTIME_UI_INPUT_TOGGLE) {
        if (!repeat) recomp_runtime_ui_close(ui);
        return 1;
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_BACK) {
        if (!repeat) {
            if (ui->in_section) {
                if (!recomp_runtime_ui_leave_folder(ui))
                    recomp_runtime_ui_leave_section(ui);
            } else {
                recomp_runtime_ui_close(ui);
            }
        }
        return 1;
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_UP ||
        input == RECOMP_RUNTIME_UI_INPUT_DOWN) {
        int delta = input == RECOMP_RUNTIME_UI_INPUT_UP ? -1 : 1;
        if (ui->in_section) {
            const RecompRuntimeUiItem *item =
                recomp_runtime_ui_page_entry(ui, ui->row_index, NULL, 0);
            if (item && item->type == RECOMP_RUNTIME_UI_GRID &&
                recomp_runtime_ui_item_enabled(ui, item) &&
                recomp_runtime_ui_grid_move(ui, item, 0, delta))
                return 1;             /* moved inside the grid */
        }
        size_t count = ui->in_section ? recomp_runtime_ui_page_entry_count(ui)
                                      : ui->section_count;
        size_t *selection = ui->in_section ? &ui->row_index : &ui->section_index;
        *selection = wrap_index(*selection, delta, count);
        return 1;
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_ACCEPT) {
        if (!ui->in_section) {
            if (!repeat) {
                recomp_runtime_ui_enter_section(ui, ui->section_index);
            }
        } else {
            recomp_runtime_ui_adjust_current(ui, 1, 1, repeat);
        }
        return 1;
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_LEFT ||
        input == RECOMP_RUNTIME_UI_INPUT_RIGHT) {
        if (ui->in_section)
            recomp_runtime_ui_adjust_current(ui,
                           input == RECOMP_RUNTIME_UI_INPUT_LEFT ? -1 : 1,
                           0, repeat);
        return 1;
    }
    return 1;
}

#define GLYPH(a,b,c,d,e,f,g) \
    ((uint64_t)(a) | ((uint64_t)(b) << 5) | ((uint64_t)(c) << 10) | \
     ((uint64_t)(d) << 15) | ((uint64_t)(e) << 20) | \
     ((uint64_t)(f) << 25) | ((uint64_t)(g) << 30))

static uint64_t glyph_bits(unsigned char c) {
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    switch (c) {
    case 'A': return GLYPH(14,17,17,31,17,17,17);
    case 'B': return GLYPH(30,17,17,30,17,17,30);
    case 'C': return GLYPH(14,17,16,16,16,17,14);
    case 'D': return GLYPH(30,17,17,17,17,17,30);
    case 'E': return GLYPH(31,16,16,30,16,16,31);
    case 'F': return GLYPH(31,16,16,30,16,16,16);
    case 'G': return GLYPH(14,17,16,23,17,17,15);
    case 'H': return GLYPH(17,17,17,31,17,17,17);
    case 'I': return GLYPH(14,4,4,4,4,4,14);
    case 'J': return GLYPH(7,2,2,2,18,18,12);
    case 'K': return GLYPH(17,18,20,24,20,18,17);
    case 'L': return GLYPH(16,16,16,16,16,16,31);
    case 'M': return GLYPH(17,27,21,21,17,17,17);
    case 'N': return GLYPH(17,25,21,19,17,17,17);
    case 'O': return GLYPH(14,17,17,17,17,17,14);
    case 'P': return GLYPH(30,17,17,30,16,16,16);
    case 'Q': return GLYPH(14,17,17,17,21,18,13);
    case 'R': return GLYPH(30,17,17,30,20,18,17);
    case 'S': return GLYPH(15,16,16,14,1,1,30);
    case 'T': return GLYPH(31,4,4,4,4,4,4);
    case 'U': return GLYPH(17,17,17,17,17,17,14);
    case 'V': return GLYPH(17,17,17,17,17,10,4);
    case 'W': return GLYPH(17,17,17,21,21,21,10);
    case 'X': return GLYPH(17,17,10,4,10,17,17);
    case 'Y': return GLYPH(17,17,10,4,4,4,4);
    case 'Z': return GLYPH(31,1,2,4,8,16,31);
    case '0': return GLYPH(14,17,19,21,25,17,14);
    case '1': return GLYPH(4,12,4,4,4,4,14);
    case '2': return GLYPH(14,17,1,2,4,8,31);
    case '3': return GLYPH(30,1,1,14,1,1,30);
    case '4': return GLYPH(2,6,10,18,31,2,2);
    case '5': return GLYPH(31,16,16,30,1,1,30);
    case '6': return GLYPH(14,16,16,30,17,17,14);
    case '7': return GLYPH(31,1,2,4,8,8,8);
    case '8': return GLYPH(14,17,17,14,17,17,14);
    case '9': return GLYPH(14,17,17,15,1,1,14);
    case '-': return GLYPH(0,0,0,31,0,0,0);
    case '+': return GLYPH(0,4,4,31,4,4,0);
    case '/': return GLYPH(1,2,2,4,8,8,16);
    case ':': return GLYPH(0,4,4,0,4,4,0);
    case '.': return GLYPH(0,0,0,0,0,6,6);
    case ',': return GLYPH(0,0,0,0,6,6,4);
    case '(': return GLYPH(2,4,8,8,8,4,2);
    case ')': return GLYPH(8,4,2,2,2,4,8);
    case '[': return GLYPH(14,8,8,8,8,8,14);
    case ']': return GLYPH(14,2,2,2,2,2,14);
    case '%': return GLYPH(17,2,4,4,8,16,17);
    case '<': return GLYPH(2,4,8,16,8,4,2);
    case '>': return GLYPH(8,4,2,1,2,4,8);
    case '=': return GLYPH(0,0,31,0,31,0,0);
    case '_': return GLYPH(0,0,0,0,0,0,31);
    case '!': return GLYPH(4,4,4,4,4,0,4);
    case '?': return GLYPH(14,17,1,2,4,0,4);
    case '#': return GLYPH(10,31,10,10,31,10,0);
    case '\'': return GLYPH(4,4,2,0,0,0,0);
    case ' ': return 0;
    default: return GLYPH(14,17,2,4,4,0,4);
    }
}

static uint32_t blend_pixel(uint32_t dst, uint32_t src, unsigned alpha) {
    unsigned dr = (dst >> 16) & 255, dg = (dst >> 8) & 255, db = dst & 255;
    unsigned sr = (src >> 16) & 255, sg = (src >> 8) & 255, sb = src & 255;
    unsigned inv = 255 - alpha;
    return 0xff000000u |
           (((sr * alpha + dr * inv) / 255) << 16) |
           (((sg * alpha + dg * inv) / 255) << 8) |
           ((sb * alpha + db * inv) / 255);
}

static void rect(void *pixels, int width, int height, int pitch, int x, int y,
                 int w, int h, uint32_t color, unsigned alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy) {
        uint32_t *row = (uint32_t *)((unsigned char *)pixels + yy * pitch);
        for (int xx = x; xx < x + w; ++xx)
            row[xx] = alpha == 255 ? (0xff000000u | color)
                                   : blend_pixel(row[xx], color, alpha);
    }
}

static void outline(void *pixels, int width, int height, int pitch, int x, int y,
                    int w, int h, int thickness, uint32_t color) {
    rect(pixels,width,height,pitch,x,y,w,thickness,color,255);
    rect(pixels,width,height,pitch,x,y+h-thickness,w,thickness,color,255);
    rect(pixels,width,height,pitch,x,y,thickness,h,color,255);
    rect(pixels,width,height,pitch,x+w-thickness,y,thickness,h,color,255);
}

static void draw_text(void *pixels, int width, int height, int pitch, int x,
                      int y, int scale, uint32_t color, const char *text,
                      int max_x) {
    if (!text) return;
    int cursor = x;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p >= 0x80) continue;
        if (cursor + 5 * scale > max_x) break;
        uint64_t bits = glyph_bits(*p);
        for (int gy = 0; gy < 7; ++gy) {
            unsigned row = (unsigned)((bits >> (gy * 5)) & 31);
            for (int gx = 0; gx < 5; ++gx)
                if (row & (1u << (4 - gx)))
                    rect(pixels,width,height,pitch,cursor+gx*scale,y+gy*scale,
                         scale,scale,color,255);
        }
        cursor += 6 * scale;
    }
}

static int text_width(const char *text, int scale) {
    int count = 0;
    if (!text) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p < 0x80) ++count;
    return count * 6 * scale;
}

static void item_value(RecompRuntimeUi *ui, const RecompRuntimeUiItem *item,
                       char *out, size_t out_size) {
    if (item->type == RECOMP_RUNTIME_UI_ACTION) {
        snprintf(out, out_size, ">");
        return;
    }
    int value = 0;
    if (!recomp_runtime_ui_current_value(ui, item, &value)) {
        snprintf(out, out_size, "?");
    } else if (item->type == RECOMP_RUNTIME_UI_BOOL) {
        snprintf(out, out_size, "%s", value ? "On" : "Off");
    } else if (item->type == RECOMP_RUNTIME_UI_CHOICE && item->choices) {
        size_t index = item->choice_count;
        if (item->choice_values) {
            for (size_t i = 0; i < item->choice_count; ++i)
                if (item->choice_values[i] == value) { index = i; break; }
        } else if (value >= item->minimum &&
                   (size_t)(value - item->minimum) < item->choice_count) {
            index = (size_t)(value - item->minimum);
        }
        if (index < item->choice_count)
            snprintf(out, out_size, "%s", item->choices[index]);
        else
            snprintf(out, out_size, "?");
    } else {
        snprintf(out, out_size, "%d", value);
    }
}

/* Height of list row `index` on the open page (a section name on the top
 * level is one line). */
static int row_height(RecompRuntimeUi *ui, size_t index, int row_h, int cell_h,
                      int cell_gap) {
    if (!ui->in_section) return row_h;
    const RecompRuntimeUiItem *item = recomp_runtime_ui_page_entry(ui, index, NULL, 0);
    if (!item || item->type != RECOMP_RUNTIME_UI_GRID) return row_h;
    const int cols = recomp_runtime_ui_grid_columns(item);
    const int rows = (recomp_runtime_ui_grid_cell_count(ui, item) + cols - 1) / cols;
    return row_h + rows * (cell_h + cell_gap) + cell_gap;
}

void recomp_runtime_ui_render_argb8888(RecompRuntimeUi *ui, void *pixels,
                                       int width, int height, int pitch) {
    if (!ui || !ui->open || !pixels || width <= 0 || height <= 0 ||
        pitch < width * 4) return;
    LauncherTheme theme = launcher_theme_by_name(ui->config.theme);
    int scale = height / 224;
    if (scale < 1) scale = 1;
    int margin = 6 * scale;
    int panel_w = 244 * scale;
    if (panel_w > width - margin * 2) panel_w = width - margin * 2;
    int panel_h = height - margin * 2;
    int panel_x = (width - panel_w) / 2;
    int panel_y = margin;
    int pad = 8 * scale;
    #define RGB8(c) (((uint32_t)((c).r * 255.0f) << 16) | \
                     ((uint32_t)((c).g * 255.0f) << 8) | \
                     (uint32_t)((c).b * 255.0f))
    const uint32_t white = RGB8(theme.text), muted = RGB8(theme.text_muted);
    const uint32_t accent = RGB8(theme.accent2), panel = RGB8(theme.panel);
    const uint32_t selected = RGB8(theme.control_hovered);
    const uint32_t disabled = RGB8(theme.text_muted);
    const uint32_t border = RGB8(theme.border);
    /* Grid tiles: usable ones in the accent, greyed and faint ones darker. */
    const uint32_t tile = RGB8(theme.accent_dim);
    const uint32_t tile_dim = RGB8(theme.panel_hovered);
    const uint32_t tile_empty = panel;
    const uint32_t gold = 0xE0B84Au, mark = 0x5DCAA5u;

    rect(pixels,width,height,pitch,0,0,width,height,0x000000,130);
    rect(pixels,width,height,pitch,panel_x,panel_y,panel_w,panel_h,panel,238);
    outline(pixels,width,height,pitch,panel_x,panel_y,panel_w,panel_h,
            scale,accent);
    const char *title = ui->config.title ? ui->config.title : "Settings";
    const char *subtitle = ui->config.subtitle;
    char breadcrumb[256];
    if (ui->in_section && ui->page[0]) {
        /* Inside a sub-page the breadcrumb is more useful than the subtitle. */
        recomp_runtime_ui_page_title(ui, breadcrumb, sizeof(breadcrumb));
        subtitle = breadcrumb;
    }
    const int title_w = text_width(title, scale);
    const int subtitle_w = text_width(subtitle, scale);
    const int header_inner_w = panel_w - pad * 2;
    const int stacked_header = subtitle && subtitle[0] &&
        title_w + subtitle_w + 4 * scale > header_inner_w;
    draw_text(pixels,width,height,pitch,panel_x+pad,panel_y+pad,scale,accent,
              title,panel_x+panel_w-pad);
    if (subtitle && subtitle[0]) {
        const int subtitle_y = panel_y + pad +
            (stacked_header ? 10 * scale : 0);
        int subtitle_x = panel_x + panel_w - pad - subtitle_w;
        if (subtitle_x < panel_x + pad) subtitle_x = panel_x + pad;
        draw_text(pixels,width,height,pitch,subtitle_x,
                  subtitle_y,scale,muted,subtitle,panel_x+panel_w-pad);
    }
    const int header_extra = stacked_header ? 10 * scale : 0;
    rect(pixels,width,height,pitch,panel_x+pad,
         panel_y+24*scale+header_extra,
         panel_w-pad*2,scale,border,255);

    int list_y = panel_y + 32 * scale + header_extra;
    int footer_y = panel_y + panel_h - 28 * scale;
    int row_h = 15 * scale;
    const int cell_h = 18 * scale, cell_gap = 3 * scale;
    size_t count = ui->in_section ? recomp_runtime_ui_page_entry_count(ui)
                                  : ui->section_count;
    size_t selection = ui->in_section ? ui->row_index : ui->section_index;
    /* Rows are one line tall, except a grid: its label line plus its tiles.
     * Scroll so the selected row fits, keeping as many rows above it as can. */
    #define ROW_HEIGHT(index) row_height(ui, (index), row_h, cell_h, cell_gap)
    const int avail = footer_y - list_y;
    size_t first = selection < count ? selection : 0;
    {
        int used = count ? ROW_HEIGHT(first) : 0;
        while (first > 0 && used + ROW_HEIGHT(first - 1) <= avail)
            used += ROW_HEIGHT(--first);
    }
    int y = list_y;
    for (size_t index = first; index < count; ++index) {
        const int h = ROW_HEIGHT(index);
        if (index != first && y + h > footer_y) break;
        if (index == selection)
            rect(pixels,width,height,pitch,panel_x+pad/2,y-3*scale,
                 panel_w-pad,row_h,selected,255);
        char folder[96] = "";
        const RecompRuntimeUiItem *item = ui->in_section
            ? recomp_runtime_ui_page_entry(ui, index, folder, sizeof(folder))
            : NULL;
        if (item && item->type == RECOMP_RUNTIME_UI_GRID) {
            const int enabled = recomp_runtime_ui_item_enabled(ui, item);
            const int sel = index == selection && enabled;
            char header[32];
            recomp_runtime_ui_grid_cell(ui, item, -1, header, sizeof(header), NULL);
            const int header_w = text_width(header, scale);
            draw_text(pixels,width,height,pitch,panel_x+pad,y,scale,
                      sel ? white : muted,item->label,
                      panel_x+panel_w-pad-header_w-8*scale);
            draw_text(pixels,width,height,pitch,panel_x+panel_w-pad-header_w,y,
                      scale,sel ? accent : muted,header,panel_x+panel_w-pad);
            const int cells = recomp_runtime_ui_grid_cell_count(ui, item);
            const int cols = recomp_runtime_ui_grid_columns(item);
            int cursor = 0;
            if (!recomp_runtime_ui_current_value(ui, item, &cursor)) cursor = -1;
            const int grid_x = panel_x + pad, grid_w = panel_w - pad * 2;
            const int cell_w = (grid_w - cell_gap * (cols - 1)) / cols;
            for (int c = 0; c < cells; ++c) {
                const int cx = grid_x + (c % cols) * (cell_w + cell_gap);
                const int cy = y + row_h + (c / cols) * (cell_h + cell_gap);
                char label[32];
                unsigned flags = 0;
                recomp_runtime_ui_grid_cell(ui, item, c, label, sizeof(label), &flags);
                uint32_t fill = tile, text = white;
                if (flags & RECOMP_RUNTIME_UI_CELL_EMPTY) { fill = tile_empty; text = disabled; }
                else if (flags & RECOMP_RUNTIME_UI_CELL_DIM) { fill = tile_dim; text = muted; }
                rect(pixels,width,height,pitch,cx,cy,cell_w,cell_h,fill,255);
                outline(pixels,width,height,pitch,cx,cy,cell_w,cell_h,scale,
                        (flags & RECOMP_RUNTIME_UI_CELL_HIGHLIGHT) ? gold : border);
                if (flags & RECOMP_RUNTIME_UI_CELL_MARK)
                    rect(pixels,width,height,pitch,cx+cell_w-5*scale,cy+2*scale,
                         3*scale,3*scale,mark,255);
                if (c == cursor) {
                    const int t = sel ? 2 * scale : scale;
                    outline(pixels,width,height,pitch,cx-t,cy-t,cell_w+2*t,
                            cell_h+2*t,t,sel ? white : muted);
                }
                int label_w = text_width(label, scale);
                if (label_w > cell_w - 4 * scale) label_w = cell_w - 4 * scale;
                draw_text(pixels,width,height,pitch,cx+(cell_w-label_w)/2,
                          cy+(cell_h-7*scale)/2,scale,text,label,cx+cell_w-2*scale);
            }
            y += h;
            continue;
        }
        if (!ui->in_section || !item) {
            /* Section on the top level, or a folder row inside a page. */
            const char *name = ui->in_section ? folder : ui->sections[index];
            draw_text(pixels,width,height,pitch,panel_x+pad,y,scale,
                      index == selection ? white : muted,name,
                      panel_x+panel_w-pad-12*scale);
            draw_text(pixels,width,height,pitch,panel_x+panel_w-pad-5*scale,y,
                      scale,index == selection ? accent : muted,">",
                      panel_x+panel_w-pad);
        } else {
            int enabled = recomp_runtime_ui_item_enabled(ui, item);
            uint32_t color = enabled ? (index == selection ? white : muted)
                                     : disabled;
            char value[96];
            item_value(ui, item, value, sizeof(value));
            int value_w = text_width(value, scale);
            draw_text(pixels,width,height,pitch,panel_x+pad,y,scale,color,
                      item->label,panel_x+panel_w-pad-value_w-8*scale);
            draw_text(pixels,width,height,pitch,panel_x+panel_w-pad-value_w,y,
                      scale,index == selection && enabled ? accent : color,
                      value,panel_x+panel_w-pad);
        }
        y += h;
    }
    #undef ROW_HEIGHT

    const char *help = "UP/DOWN SELECT  LEFT/RIGHT CHANGE  ENTER ACCEPT";
    if (ui->in_section) {
        char folder[96];
        const RecompRuntimeUiItem *item = recomp_runtime_ui_page_entry(
            ui, ui->row_index, folder, sizeof(folder));
        if (item && item->description && *item->description) help = item->description;
        else if (!item && folder[0]) help = "ENTER OPEN  ESC BACK";
    }
    rect(pixels,width,height,pitch,panel_x+pad,footer_y-4*scale,
         panel_w-pad*2,scale,border,255);
    draw_text(pixels,width,height,pitch,panel_x+pad,footer_y+3*scale,scale,
              muted,help,panel_x+panel_w-pad);
    if (ui->status_frames) {
        int status_w = text_width(ui->status, scale);
        draw_text(pixels,width,height,pitch,panel_x+panel_w-pad-status_w,
                  panel_y+panel_h-12*scale,scale,accent,ui->status,
                  panel_x+panel_w-pad);
        --ui->status_frames;
    }
    #undef RGB8
}
