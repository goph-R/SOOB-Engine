#ifndef EDIT_MENUPAD_H
#define EDIT_MENUPAD_H

/*
 * edit_menupad.h -- left "icon column" padding for drop-down menu items.
 *
 * FLTK 1.3 draws a plain menu item's text flush against the menu's left edge;
 * only toggle/radio items get a column (for their check box). Native Windows
 * menus reserve that column on every item, which reads much calmer.
 *
 * editMenuPad() switches every item INSIDE the drop-downs to a custom label
 * type that leaves exactly the toggle items' check-box column empty, so plain
 * and toggle items line up. Menu-bar titles are left alone. Toggle/radio items
 * keep the normal label type -- FLTK already offsets them.
 *
 * Call after the menu is fully built (menu() / add()): items added later keep
 * the default label type.
 */

#include <FL/Fl.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/fl_draw.H>

#define EDIT_MENUPAD_LABEL FL_FREE_LABELTYPE

/* Width of the toggle column, mirroring Fl_Menu_Item::draw(): a W-wide box
   at x+2, text at x+W+3, where d = (h - FL_NORMAL_SIZE + 1) / 2, W = h - 2d. */
static int editMenuPadWidth(int h)
{
    int d = (h - FL_NORMAL_SIZE + 1) / 2;
    return (h - 2 * d) + 3;
}

static void editMenuPadDraw(const Fl_Label *o, int X, int Y, int W, int H,
                            Fl_Align align)
{
    int pad = editMenuPadWidth(H);
    fl_font(o->font, o->size);
    fl_color((Fl_Color)o->color);
    fl_draw(o->value, X + pad, Y, W > pad ? W - pad : 0, H, align, o->image);
}

static void editMenuPadMeasure(const Fl_Label *o, int &W, int &H)
{
    fl_font(o->font, o->size);
    fl_measure(o->value, W, H);
    /* Menus measure with H = text height; the item is drawn LEADING taller,
       which only moves the column by a pixel at most -- close enough here. */
    W += editMenuPadWidth(H);
}

/* Items from `m` to the end of this (sub)menu level, recursing into submenus. */
static void editMenuPadLevel(Fl_Menu_Item *m)
{
    for (; m && m->text; m = m->next()) {
        if (!(m->flags & (FL_MENU_TOGGLE | FL_MENU_RADIO)))
            m->labeltype(EDIT_MENUPAD_LABEL);
        if (m->flags & FL_SUBMENU) editMenuPadLevel(m + 1);
    }
}

static void editMenuPad(Fl_Menu_ *menu)
{
    Fl::set_labeltype(EDIT_MENUPAD_LABEL, editMenuPadDraw, editMenuPadMeasure);
    /* Top level = menu-bar titles: unpadded. Their drop-downs: padded. */
    Fl_Menu_Item *m = (Fl_Menu_Item *)menu->menu();
    for (; m && m->text; m = m->next())
        if (m->flags & FL_SUBMENU) editMenuPadLevel(m + 1);
}

#endif /* EDIT_MENUPAD_H */
