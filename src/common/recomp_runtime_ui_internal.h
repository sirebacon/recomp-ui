#ifndef RECOMP_RUNTIME_UI_INTERNAL_H
#define RECOMP_RUNTIME_UI_INTERNAL_H

#include "recomp_runtime_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nested pages. An item's `section` may be a path ("Mods/My Mod/Editor"):
 * the first component is the top-level section, the rest names sub-pages.
 * Inside a page, items whose path continues deeper appear as one folder row
 * per next component (in first-appearance order); opening it descends, and
 * Back climbs one level. Flat section names behave exactly as before.
 */
#define RECOMP_RUNTIME_UI_MAX_DEPTH 8

struct RecompRuntimeUi {
    RecompRuntimeUiConfig config;
    const char **sections;      /* first path components, owned copies */
    size_t section_count;
    size_t section_index;
    size_t row_index;
    int open;
    int in_section;
    char page[192];             /* path below the section; "" = section root */
    size_t page_depth;
    size_t row_stack[RECOMP_RUNTIME_UI_MAX_DEPTH]; /* row to restore on Back */
    unsigned status_frames;
    char status[48];
    RecompRuntimeUiItem *owned_items;
    const char **owned_view_choices;
    int *owned_view_values;
    /*
     * Text-editing state. Owned by the presentation backend (the ImGui one
     * drives an InputText), because the edit lifecycle -- caret, selection,
     * commit-on-Enter -- is the widget's business, not the model's. The core
     * keeps it here only so the host can query wants_text_input.
     */
    int editing_text;
    char edit_buffer[128];
};

int recomp_runtime_ui_item_enabled(const RecompRuntimeUi *ui,
                                   const RecompRuntimeUiItem *item);
size_t recomp_runtime_ui_section_item_count(const RecompRuntimeUi *ui,
                                            size_t section);
const RecompRuntimeUiItem *recomp_runtime_ui_section_item(
    const RecompRuntimeUi *ui, size_t section, size_t row);
int recomp_runtime_ui_current_value(RecompRuntimeUi *ui,
                                    const RecompRuntimeUiItem *item,
                                    int *value);
void recomp_runtime_ui_adjust_current(RecompRuntimeUi *ui, int direction,
                                      int activate, int repeat);
void recomp_runtime_ui_enter_section(RecompRuntimeUi *ui, size_t section);
void recomp_runtime_ui_leave_section(RecompRuntimeUi *ui);
/* Rows of the page currently open inside a section (items and folders). */
size_t recomp_runtime_ui_page_entry_count(const RecompRuntimeUi *ui);
/* The item at `row`, or NULL for a folder row (its name is written to
 * folder_out when given) or an out-of-range row. */
const RecompRuntimeUiItem *recomp_runtime_ui_page_entry(
    const RecompRuntimeUi *ui, size_t row, char *folder_out, size_t folder_size);
void recomp_runtime_ui_enter_folder(RecompRuntimeUi *ui, const char *name);
/* Climbs one page; returns 0 (and does nothing) at the section root. */
int recomp_runtime_ui_leave_folder(RecompRuntimeUi *ui);
/* "Section > Page > Sub-page" for the open page. */
void recomp_runtime_ui_page_title(const RecompRuntimeUi *ui, char *out,
                                  size_t out_size);
/* Reads a TEXT item's current value; writes "" when unavailable. */
void recomp_runtime_ui_current_text(RecompRuntimeUi *ui,
                                    const RecompRuntimeUiItem *item, char *buf,
                                    size_t buf_size);
/* Commits an edited TEXT value; returns non-zero when accepted. */
int recomp_runtime_ui_commit_text(RecompRuntimeUi *ui,
                                  const RecompRuntimeUiItem *item,
                                  const char *value);

#ifdef __cplusplus
}
#endif

#endif
