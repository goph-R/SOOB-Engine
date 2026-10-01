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
 *               geometry and could switch to a different tab. With a
 *               closeCallback(), each tab gets an X (hover-highlighted) and
 *               middle-click closes; both fire on release, like a button.
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
#define TABS_MARGIN_X  4      /* row inset before the first tab (minimum) */
#define TABS_GAP_TOP   4      /* row strip visible above the tab block */
#define TABS_CLOSE_GAP 8      /* caption -> close glyph */
#define TABS_CLOSE_SZ  8      /* close glyph (an X), square */
#define TABS_CLOSE_HIT 16     /* its clickable / hover square */

/* Asked to close a tab: the child widget whose X was clicked (or that was
   middle-clicked). The callee decides -- it may prompt, or keep the tab. */
typedef void (*CodeTabsCloseFn)(Fl_Widget *child, void *data);

class CodeTabs : public Fl_Tabs {
public:
    CodeTabs(int X, int Y, int W, int H)
        : Fl_Tabs(X, Y, W, H), mFirstX(0), mCloseFn(0), mCloseData(0),
          mHoverClose(-1), mPressClose(-1) {}

    /* Start the first tab block at a column -- e.g. the edge of the editor's
       line-number gutter, so the tabs sit over the text area. px is measured
       from the widget's left edge, in real (already DPI-scaled) pixels.
       0 = just the default inset. */
    void firstTabAt(int px) { mFirstX = px; redraw(); }

    /* Give every tab a close X (and middle-click to close). */
    void closeCallback(CodeTabsCloseFn fn, void *data)
    {
        mCloseFn = fn;
        mCloseData = data;
        redraw();
    }

    /* Row inset before the first tab block. */
    int inset()
    {
        int m = editDpi(TABS_MARGIN_X);
        return mFirstX > m ? mFirstX : m;
    }

    /* Row height: children start below it. Fallback before the first child. */
    int rowH()
    {
        if (children() > 0) return child(0)->y() - y();
        return 0;
    }

    /* Width a tab needs: padding, caption, and the close X if enabled. */
    int tabWant(int k)
    {
        int w = capW(k) + 2 * editDpi(TABS_PAD_X);
        if (mCloseFn) w += editDpi(TABS_CLOSE_GAP) + editDpi(TABS_CLOSE_SZ);
        return w;
    }

    /* Tab extents for child i, in window coords. If the row overflows, tabs
       are shrunk evenly so every tab stays reachable (captions are clipped). */
    void tabSpan(int i, int *tx, int *tw)
    {
        int n = children();
        int avail = w() - inset() - editDpi(TABS_MARGIN_X);
        int total = 0, k, cx = x() + inset();
        for (k = 0; k < n; k++) total += tabWant(k);
        for (k = 0; k <= i; k++) {
            int cw = tabWant(k);
            if (total > avail && total > 0) cw = cw * avail / total;
            if (k == i) { *tx = cx; *tw = cw; return; }
            cx += cw;
        }
        *tx = cx; *tw = 0;
    }

    /* The close X's hover / hit square for tab i (glyph centred inside). */
    void closeBox(int i, int *bx, int *by, int *bs)
    {
        int tx, tw, top = editDpi(TABS_GAP_TOP), H = rowH();
        int sz = editDpi(TABS_CLOSE_SZ), hit = editDpi(TABS_CLOSE_HIT);
        tabSpan(i, &tx, &tw);
        *bs = hit;
        *bx = tx + tw - editDpi(TABS_PAD_X) - sz - (hit - sz) / 2;
        *by = y() + top + (H - top - hit) / 2;
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

    int closeAt(int ex, int ey)
    {
        int i, bx, by, bs;
        if (!mCloseFn) return -1;
        i = tabAt(ex);
        if (i < 0) return -1;
        closeBox(i, &bx, &by, &bs);
        return (ex >= bx && ex < bx + bs && ey >= by && ey < by + bs) ? i : -1;
    }

    int handle(int e)
    {
        int H = rowH();
        int inRow = H > 0 && Fl::event_y() >= y() && Fl::event_y() < y() + H &&
                    Fl::event_x() >= x() && Fl::event_x() < x() + w();

        /* hover highlight on the close X */
        if (e == FL_ENTER || e == FL_MOVE || e == FL_LEAVE) {
            int hv = (e != FL_LEAVE && inRow) ? closeAt(Fl::event_x(), Fl::event_y()) : -1;
            if (hv != mHoverClose) { mHoverClose = hv; redraw_tabs(); }
            return Fl_Tabs::handle(e);
        }

        /* a close in progress: pressed on an X (or middle-pressed a tab).
           It fires on release over the same target, like a button. */
        if (mPressClose >= 0 && (e == FL_DRAG || e == FL_RELEASE)) {
            if (e == FL_RELEASE) {
                int i = mPressClose;
                int same = inRow && (Fl::event_button() == FL_MIDDLE_MOUSE
                                     ? tabAt(Fl::event_x())
                                     : closeAt(Fl::event_x(), Fl::event_y())) == i;
                mPressClose = -1;
                if (same && i < children()) {
                    mHoverClose = -1;
                    mCloseFn(child(i), mCloseData);   /* may delete the child: touch nothing after */
                }
            }
            return 1;
        }

        if ((e == FL_PUSH || e == FL_DRAG || e == FL_RELEASE) && inRow) {
            if (e == FL_PUSH) {
                int i = tabAt(Fl::event_x());
                if (mCloseFn && i >= 0 &&
                    (Fl::event_button() == FL_MIDDLE_MOUSE ||
                     closeAt(Fl::event_x(), Fl::event_y()) == i)) {
                    mPressClose = i;
                    return 1;
                }
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
            int closeW = mCloseFn ? editDpi(TABS_CLOSE_GAP) + editDpi(TABS_CLOSE_SZ) : 0;
            fl_rectf(x(), y(), w(), H, color());
            for (i = 0; i < children(); i++) {
                Fl_Widget *c = child(i);
                int sel = (c == v), capRoom;
                Fl_Color bg = sel ? selection_color() : color();
                Fl_Color fg = sel ? labelcolor() : c->labelcolor();
                tabSpan(i, &tx, &tw);
                if (tw <= 0) continue;
                if (sel) fl_rectf(tx, y() + top, tw, H - top, bg);

                capRoom = tw - 2 * padX - closeW;
                if (capRoom > 0) {
                    fl_push_clip(tx + padX, y(), capRoom, H);
                    fl_font(c->labelfont(), c->labelsize());
                    fl_color(fg);
                    fl_draw(c->label() ? c->label() : "", tx + padX, y() + top,
                            capRoom, H - top, FL_ALIGN_LEFT | FL_ALIGN_CLIP);
                    fl_pop_clip();
                }
                if (mCloseFn) drawClose(i, bg, fg);
            }
        }
    }

private:
    int mFirstX;
    CodeTabsCloseFn mCloseFn;
    void *mCloseData;
    int mHoverClose;              /* tab whose X is under the mouse, or -1 */
    int mPressClose;              /* tab whose close is being clicked, or -1 */

    void drawClose(int i, Fl_Color bg, Fl_Color fg)
    {
        int bx, by, bs, sz = editDpi(TABS_CLOSE_SZ), gx, gy, t;
        closeBox(i, &bx, &by, &bs);
        if (i == mHoverClose)
            fl_rectf(bx, by, bs, bs, fl_color_average(fg, bg, 0.25f));
        gx = bx + (bs - sz) / 2;
        gy = by + (bs - sz) / 2;
        t = sz / 6;                    /* stroke: 1 px at 100%, grows with DPI */
        if (t < 1) t = 1;
        fl_color(fg);
        fl_line_style(FL_SOLID, t);
        fl_line(gx, gy, gx + sz - 1, gy + sz - 1);
        fl_line(gx, gy + sz - 1, gx + sz - 1, gy);
        fl_line_style(0);
    }

    int capW(int i)
    {
        Fl_Widget *c = child(i);
        fl_font(c->labelfont(), c->labelsize());
        return (int)fl_width(c->label() ? c->label() : "");
    }
};

#endif /* EDIT_TABS_H */
