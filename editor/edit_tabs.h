#ifndef EDIT_TABS_H
#define EDIT_TABS_H

/*
 * edit_tabs.h -- CodeTabs: a flat, borderless Fl_Tabs for the code editor.
 *
 * Fl_Tabs 1.3 always draws bevelled tab outlines, and its tab widths (label +
 * a fixed 10 px) live in private members, so neither borders nor padding can
 * be changed from outside. CodeTabs takes over the TAB ROW only:
 *
 *   draw()   -- row filled with color(); the selected tab is a flat block in
 *               selection_color() (= the editor background, so it merges into
 *               the page below); other tabs are bare captions on the row.
 *               Captions: labelcolor() when selected, the child's own
 *               labelcolor() otherwise (same convention as Fl_Tabs).
 *   handle() -- clicks in the row hit-test against OUR widths. They must be
 *               swallowed whole (push, drag and release): Fl_Tabs would
 *               otherwise re-resolve the release against its own, narrower
 *               geometry and could switch to a different tab.
 *
 * Everything below the row -- child management, value(), resizing, keyboard
 * -- stays stock Fl_Tabs. Row height comes from the children's y offset, as
 * with Fl_Tabs; the tab block leaves a gap above it (TABS_GAP_TOP) so the row
 * reads as a strip. Paddings are 96-DPI pixels, scaled through editDpi().
 */

#include <FL/Fl.H>
#include <FL/Fl_Tabs.H>
#include <FL/fl_draw.H>
#include "edit_dpi.h"

#define TABS_PAD_X    12      /* caption padding, left and right */
#define TABS_MARGIN_X  4      /* row inset before the first tab */
#define TABS_GAP_TOP   4      /* row strip visible above the tab block */

class CodeTabs : public Fl_Tabs {
public:
    CodeTabs(int X, int Y, int W, int H) : Fl_Tabs(X, Y, W, H) {}

    /* Row height: children start below it. Fallback before the first child. */
    int rowH()
    {
        if (children() > 0) return child(0)->y() - y();
        return 0;
    }

    /* Tab extents for child i, in window coords. Widths are caption + padding;
       if the row overflows they are shrunk evenly so every tab stays reachable
       (captions are clipped to their tab then). */
    void tabSpan(int i, int *tx, int *tw)
    {
        int n = children(), padX = editDpi(TABS_PAD_X);
        int avail = w() - 2 * editDpi(TABS_MARGIN_X);
        int total = 0, k, cx = x() + editDpi(TABS_MARGIN_X);
        for (k = 0; k < n; k++) total += capW(k) + 2 * padX;
        for (k = 0; k <= i; k++) {
            int cw = capW(k) + 2 * padX;
            if (total > avail && total > 0) cw = cw * avail / total;
            if (k == i) { *tx = cx; *tw = cw; return; }
            cx += cw;
        }
        *tx = cx; *tw = 0;
    }

    int tabAt(int ex)
    {
        int i, tx, tw;
        for (i = 0; i < children(); i++) {
            tabSpan(i, &tx, &tw);
            if (ex >= tx && ex < tx + tw) return i;
        }
        return -1;
    }

    int handle(int e)
    {
        int H = rowH();
        if ((e == FL_PUSH || e == FL_DRAG || e == FL_RELEASE) && H > 0 &&
            Fl::event_y() >= y() && Fl::event_y() < y() + H) {
            if (e == FL_PUSH) {
                int i = tabAt(Fl::event_x());
                if (i >= 0 && child(i) != value()) {
                    value(child(i));
                    set_changed();
                    do_callback();
                    redraw();
                }
            }
            return 1;
        }
        return Fl_Tabs::handle(e);
    }

protected:
    void draw()
    {
        Fl_Widget *v = value();
        int H = rowH();

        if (damage() & FL_DAMAGE_ALL) {
            if (v) draw_child(*v);
            else   fl_rectf(x(), y(), w(), h(), color());
        } else if (v) {
            update_child(*v);
        }
        if (H > 0 && (damage() & (FL_DAMAGE_SCROLL | FL_DAMAGE_ALL))) {
            int i, tx, tw, top = editDpi(TABS_GAP_TOP);
            int padX = editDpi(TABS_PAD_X);
            fl_rectf(x(), y(), w(), H, color());
            for (i = 0; i < children(); i++) {
                Fl_Widget *c = child(i);
                int sel = (c == v);
                tabSpan(i, &tx, &tw);
                if (tw <= 0) continue;
                if (sel) fl_rectf(tx, y() + top, tw, H - top, selection_color());
                fl_push_clip(tx, y(), tw, H);
                fl_font(c->labelfont(), c->labelsize());
                fl_color(sel ? labelcolor() : c->labelcolor());
                fl_draw(c->label() ? c->label() : "", tx + padX, y() + top,
                        tw - padX, H - top, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
                fl_pop_clip();
            }
        }
    }

private:
    int capW(int i)
    {
        Fl_Widget *c = child(i);
        fl_font(c->labelfont(), c->labelsize());
        return (int)fl_width(c->label() ? c->label() : "");
    }
};

#endif /* EDIT_TABS_H */
