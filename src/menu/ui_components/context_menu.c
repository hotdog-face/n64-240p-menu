/**
 * @file context_menu.c
 * @brief Implementation of the context menu UI component.
 * @ingroup ui_components
 */

#include "../ui_components.h"
#include "../fonts.h"
#include "../sound.h"
#include "constants.h"

/**
 * @brief Get the current submenu from a context menu component.
 *
 * @param cm Pointer to the context menu component.
 * @return Pointer to the current submenu (deepest submenu in the chain).
 */
static component_context_menu_t *get_current_submenu(component_context_menu_t *cm) {
    while (cm->submenu != NULL) {
        cm = cm->submenu;
    }
    return cm;
}

/* Exact distance between two baselines for FNT_DEFAULT, measured from a tiny
   two-line layout (libdragon has no public ascent/line-height getter). Measured
   every call because the font can change at runtime (legacy font setting). */
static int context_menu_line_height(void) {
    int nbytes = 3;
    rdpq_paragraph_t *p = rdpq_paragraph_build(
        &(rdpq_textparms_t){ .line_spacing = TEXT_LINE_SPACING_ADJUST }, FNT_DEFAULT, "A\nA", &nbytes);
    int lh = 0;
    if (p && p->nchars >= 2) {
        lh = p->chars[1].y - p->chars[0].y;
        if (lh < 0) lh = -lh;
    }
    if (p) rdpq_paragraph_free(p);
    return lh > 0 ? lh : 14;
}

/**
 * @brief Initialize the context menu component.
 *
 * @param cm Pointer to the context menu component.
 */
void ui_components_context_menu_init(component_context_menu_t *cm) {
    cm->row_selected = -1;
    cm->row_count = 0;
    cm->hide_pending = false;
    cm->parent = NULL;
    for (int i = 0; (cm->list[i].text) != NULL; i++) {
        cm->row_count += 1;
    }
}

/**
 * @brief Show the context menu component (reset selection and submenu).
 *
 * @param cm Pointer to the context menu component.
 */
void ui_components_context_menu_show(component_context_menu_t *cm) {
    cm->row_selected = 0;
    cm->submenu = NULL;
}

/**
 * @brief Process the context menu actions (navigation, selection, etc).
 *
 * @param menu Pointer to the menu structure.
 * @param cm Pointer to the context menu component.
 * @return true if the context menu is processed, false otherwise.
 */
bool ui_components_context_menu_process(menu_t *menu, component_context_menu_t *cm) {
    if (!cm || (cm->row_selected < 0)) {
        return false;
    }

    component_context_menu_t *top = cm;

    cm = get_current_submenu(cm);

    if (menu->actions.back) {
        if (cm->parent) {
            cm->parent->submenu = NULL;
        } else {
            cm->hide_pending = true;
        }
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.enter) {
        if (cm->list[cm->row_selected].submenu) {
            cm->submenu = cm->list[cm->row_selected].submenu;
            ui_components_context_menu_init(cm->submenu);
            // Use custom default selection if available, otherwise default to 0
            if (cm->submenu->get_default_selection) {
                cm->submenu->row_selected = cm->submenu->get_default_selection(menu);
            } else {
                cm->submenu->row_selected = 0;
            }
            cm->submenu->parent = cm;
        } else if (cm->list[cm->row_selected].action) {
            cm->list[cm->row_selected].action(menu, cm->list[cm->row_selected].arg);
            top->hide_pending = true;
        }
        sound_play_effect(SFX_ENTER);
    } else if (menu->actions.go_up) {
        /* Wrap around the top, skipping blank separator rows ({ .text = "" }). */
        do {
            cm->row_selected -= 1;
            if (cm->row_selected < 0) {
                cm->row_selected = cm->row_count - 1;
            }
        } while (cm->list[cm->row_selected].text && cm->list[cm->row_selected].text[0] == '\0');
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down) {
        /* Wrap around the bottom, skipping blank separator rows ({ .text = "" }). */
        do {
            cm->row_selected += 1;
            if (cm->row_selected >= cm->row_count) {
                cm->row_selected = 0;
            }
        } while (cm->list[cm->row_selected].text && cm->list[cm->row_selected].text[0] == '\0');
        sound_play_effect(SFX_CURSOR);
    }

    return true;
}

/**
 * @brief Draw the context menu UI component.
 *
 * @param cm Pointer to the context menu component.
 */
void ui_components_context_menu_draw(component_context_menu_t *cm) {
    if (!cm || (cm->row_selected < 0)) {
        return;
    }

    component_context_menu_t *top = cm;

    cm = get_current_submenu(cm);

    bool la = top->left_align;

    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t) {
            .width        = la ? MESSAGEBOX_MAX_WIDTH : VISIBLE_AREA_WIDTH,
            .height       = VISIBLE_AREA_HEIGHT,
            .align        = la ? ALIGN_LEFT : ALIGN_CENTER,
            .valign       = VALIGN_CENTER,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        NULL
    );

    for (int i = 0; i < cm->row_count; i++) {
        const char *text = cm->list[i].text;
        rdpq_paragraph_builder_span(text, strlen(text));
        if (cm->list[i + 1].text != NULL) {
            rdpq_paragraph_builder_newline();
        }
    }

    rdpq_paragraph_t *layout = rdpq_paragraph_builder_end();

    int width = layout->bbox.x1 - layout->bbox.x0 + MESSAGEBOX_MARGIN;
    /* Floor the width so a menu doesn't visibly resize as a variable-length label changes
       (e.g. the grid's "Favorite N/M" count or the Favorite/Unfavorite verb). Menus whose
       content is wider than this still size to fit; their labels are fixed, so no jitter. */
    if (width < 240) width = 240;
    int height = layout->bbox.y1 - layout->bbox.y0 + MESSAGEBOX_MARGIN;

    /* The rainbow rim is drawn by ui_components_dialog_draw (shared by all dialogs). */
    ui_components_dialog_draw(width, height);

    int dialog_x0 = DISPLAY_CENTER_X - (width / 2);
    int highlight_x0 = dialog_x0;
    int highlight_x1 = dialog_x0 + width;
    /* Guard against a degenerate (empty) layout: nlines == 0 would divide by zero
       (bbox is float, so this faults as an FPU DIV0 exception). Skip the highlight. */
    if (layout->nlines > 0) {
        /* Step rows by the font's real line height. v25 used bbox height / nlines,
           but blank separator rows aren't counted in nlines, so the bar drifted
           further off the text on every row below a separator. bbox.y0 is the
           top of the first row's line box (baseline - ascent). */
        int highlight_height = context_menu_line_height();
        int highlight_y = VISIBLE_AREA_Y0 + layout->bbox.y0 + ((cm->row_selected) * highlight_height);

        ui_components_box_draw(
            highlight_x0,
            highlight_y,
            highlight_x1,
            highlight_y + highlight_height,
            CONTEXT_MENU_HIGHLIGHT_COLOR
        );
    }

    /* For left-aligned menus render text at the inner-left of the dialog box;
       for centered menus use the standard VISIBLE_AREA origin (text is
       self-centering via ALIGN_CENTER + the paragraph bbox offset). */
    int render_x = la ? (dialog_x0 + MESSAGEBOX_MARGIN / 2) : VISIBLE_AREA_X0;

    rdpq_paragraph_render(layout, render_x, VISIBLE_AREA_Y0);

    rdpq_paragraph_free(layout);

    if (top->hide_pending) {
        top->hide_pending = false;
        top->row_selected = -1;
    }
}
