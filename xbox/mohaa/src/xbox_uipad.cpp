/*
 * xbox_uipad.cpp -- Xbox controller navigation for the menus.
 *
 * The MOHAA menus are built for a mouse: widgets only react (hover material,
 * hovercommand, click) when the pointer is over them. Here the D-pad / left
 * stick moves a focus between the selectable widgets of the current menu and
 * the (invisible) pointer is put at the centre of the focused one, so the
 * normal mouse logic does the rest.
 *
 *   D-pad / left stick  move the focus (sliders and value lists: left/right
 *                       change the value; lists: up/down move the selection)
 *   A                   click (lists: ENTER, e.g. load the selected game;
 *                       key bindings: bind, then press the button to use)
 *   B                   back (ESC)
 *   X                   key bindings: clear the selected binding
 *   START               ESC (opens the menu in game, skips cinematics)
 *
 * Pulldown menus need the mouse button held while choosing: A opens one and
 * keeps the button down, up/down pick an item, A selects it, B cancels.
 *
 * Hooks: CL_KeyEvent calls UI_Xbox_PadKey first, CL_FillUIDef calls
 * UI_Xbox_PadFrame every frame (both under #ifdef NXDK).
 */
#include "../../../third_party/openmohaa/code/uilib/ui_local.h"
#include "../../../third_party/openmohaa/code/uilib/uimessage.h"

#define PAD_MAX_WIDGETS 128
#define PAD_SAVED_MENUS 16

static SafePtr<UIWidget> pad_focus;
static Menu             *pad_menu;
static UIWidget         *pad_root;
static int               pad_settleFrames;

// pulldown menu currently held open by the controller
static SafePtr<UIWidget> pad_pulldown;
static bool              pad_swallowRelease;
static bool              pad_popupPlaced;

// last focused widget of recently visited menus
static struct {
    Menu             *menu;
    SafePtr<UIWidget> focus;
} pad_saved[PAD_SAVED_MENUS];
static int pad_savedNext;

/*
==============================================================

WIDGETS

==============================================================
*/

static UIPoint2D UI_PadCenter(UIWidget *widget)
{
    const UISize2D size = widget->getSize();
    return widget->getGlobalPoint(UIPoint2D(size.width * 0.5f, size.height * 0.5f));
}

static bool UI_PadIsList(UIWidget *widget)
{
    return widget->isSubclassOf(UIListBase) || widget->isSubclassOf(UILanGameList)
        || widget->isSubclassOf(UIFakkBindList);
}

// widgets whose value changes with left/right
static bool UI_PadIsValue(UIWidget *widget)
{
    return widget->isSubclassOf(UISlider) || widget->isSubclassOf(UIList);
}

static bool UI_PadIsSelectable(UIWidget *widget)
{
    // the cells of the key binding grid are siblings of the grid, which is
    // the single stop for all of them
    if (widget->isSubclassOf(UIBindButton) || widget->isSubclassOf(UIFakkBindListLabel)) {
        return false;
    }
    if (!widget->isSubclassOf(UIButtonBase) && !widget->isSubclassOf(UICheckBox) && !widget->isSubclassOf(UIField)
        && !widget->isSubclassOf(UIPulldownMenu) && !UI_PadIsValue(widget) && !UI_PadIsList(widget)) {
        return false;
    }
    // not CanActivate(): sliders, for one, refuse keyboard activation but
    // work with the mouse, which is what the controller drives
    if (!widget->isEnabled() || widget->IsDying()) {
        return false;
    }
    for (UIWidget *w = widget; w; w = w->getParent()) {
        if (!w->getShow() || w->isInert()) {
            return false;
        }
    }

    const UISize2D size = widget->getSize();
    if (size.width < 4 || size.height < 4) {
        return false;
    }

    // ignore widgets whose centre is off screen
    const UIPoint2D center = UI_PadCenter(widget);
    return center.x >= 0 && center.y >= 0 && center.x < uid.vidWidth && center.y < uid.vidHeight;
}

static void UI_PadCollect(UIWidget *parent, UIWidget **list, int *count)
{
    for (UIWidget *widget = parent->getFirstChild(); widget; widget = parent->getNextChild(widget)) {
        if (!widget->getShow()) {
            continue;
        }
        if (UI_PadIsSelectable(widget)) {
            if (*count < PAD_MAX_WIDGETS) {
                list[(*count)++] = widget;
            }
            // the parts of a selectable widget (list scrollbars etc.) are not separate stops
            continue;
        }
        UI_PadCollect(widget, list, count);
    }
}

// a visible dialog box takes over the navigation, otherwise the current menu
static UIWidget *UI_PadRoot(void)
{
    for (UIWidget *widget = uWinMan.getFirstChild(); widget; widget = uWinMan.getNextChild(widget)) {
        if (widget->getShow() && (widget->isSubclassOf(UIDialog) || widget->isSubclassOf(UIMessageDialog))) {
            return widget;
        }
    }

    Menu *menu = menuManager.CurrentMenu();
    return menu ? menu->GetContainerWidget() : NULL;
}

static int UI_PadGather(UIWidget **list)
{
    UIWidget *root  = UI_PadRoot();
    int       count = 0;

    if (root) {
        UI_PadCollect(root, list, &count);
    }
    return count;
}

static bool UI_PadInList(UIWidget *widget, UIWidget **list, int count)
{
    for (int i = 0; i < count; i++) {
        if (list[i] == widget) {
            return true;
        }
    }
    return false;
}

// the widget nearest the top left of the screen (reading order) gets the
// focus when a menu opens
static UIWidget *UI_PadFirst(UIWidget **list, int count)
{
    UIWidget *best      = NULL;
    float     bestScore = 0;

    for (int i = 0; i < count; i++) {
        const UIPoint2D center = UI_PadCenter(list[i]);
        const float     score  = center.y + center.x * 0.5f;

        if (!best || score < bestScore) {
            best      = list[i];
            bestScore = score;
        }
    }
    return best;
}

static void UI_PadPointAt(const UIPoint2D& point)
{
    cl.mousex = (int)point.x;
    cl.mousey = (int)point.y;
}

static void UI_PadFocus(UIWidget *widget)
{
    pad_focus = widget;
    UI_PadPointAt(UI_PadCenter(widget));
}

/*
====================
UI_PadNavigate

Moves the focus to the closest widget in direction (dx, dy). Distance along
the direction counts once, sideways distance twice, so the focus prefers
widgets that are in line. When nothing lies in that direction it wraps
around to the far side of the screen.
====================
*/
static void UI_PadNavigate(int dx, int dy)
{
    UIWidget *list[PAD_MAX_WIDGETS];
    const int count = UI_PadGather(list);

    if (!count) {
        return;
    }

    UIWidget *current = pad_focus;
    if (!current || !UI_PadInList(current, list, count)) {
        UI_PadFocus(UI_PadFirst(list, count));
        return;
    }

    const UIPoint2D from      = UI_PadCenter(current);
    const float     span      = dx ? uid.vidWidth : uid.vidHeight;
    UIWidget       *best      = NULL;
    float           bestScore = 0;

    for (int i = 0; i < count; i++) {
        if (list[i] == current) {
            continue;
        }

        const UIPoint2D to      = UI_PadCenter(list[i]);
        float           along   = (to.x - from.x) * dx + (to.y - from.y) * dy;
        const float     sideway = fabs((to.x - from.x) * dy) + fabs((to.y - from.y) * dx);

        if (along < 1) {
            // behind or level with the current widget: only reachable by wrapping
            along += span;
        }

        const float score = along + sideway * 2;
        if (!best || score < bestScore) {
            best      = list[i];
            bestScore = score;
        }
    }

    if (best) {
        UI_PadFocus(best);
    }
}

/*
====================
UI_PadDirection

D-pad / stick handling. Value widgets take left/right, lists take up/down
until their selection stops moving (top or bottom item), then the focus
leaves them.
====================
*/
static void UI_PadDirection(int dx, int dy)
{
    UIWidget *current = pad_focus;

    if (current) {
        if (dx && UI_PadIsValue(current)) {
            current->KeyEvent(dx > 0 ? K_RIGHTARROW : K_LEFTARROW, cls.realtime);
            return;
        }
        if (current->isSubclassOf(UIFakkBindList)) {
            // key binding grid: rows and the primary/alternate columns
            UIFakkBindList *grid = (UIFakkBindList *)current;
            const int       row  = grid->PadRow();
            const int       col  = grid->PadColumn();

            if (dx) {
                current->KeyEvent(dx > 0 ? K_RIGHTARROW : K_LEFTARROW, cls.realtime);
            } else {
                current->KeyEvent(dy > 0 ? K_DOWNARROW : K_UPARROW, cls.realtime);
            }
            if (grid->PadRow() != row || grid->PadColumn() != col) {
                return;
            }
        } else if (dy && UI_PadIsList(current) && current->isSubclassOf(UIListBase)) {
            UIListBase *list   = (UIListBase *)current;
            const int   before = list->getCurrentItem();

            current->KeyEvent(dy > 0 ? K_DOWNARROW : K_UPARROW, cls.realtime);
            if (list->getCurrentItem() != before) {
                return;
            }
        }
    }

    UI_PadNavigate(dx, dy);
}

/*
==============================================================

PULLDOWN MENUS

==============================================================
*/

static UIPopupMenu *UI_PadOpenPopup(void)
{
    UIWidget *widget = pad_pulldown;

    if (!widget || !widget->isSubclassOf(UIPulldownMenu)) {
        return NULL;
    }
    return ((UIPulldownMenu *)widget)->PadOpenPopup();
}

static void UI_PadPopupStep(int dir)
{
    UIPopupMenu *popup = UI_PadOpenPopup();
    UIPoint2D    center;

    if (popup && popup->PadStep(dir, &center)) {
        UI_PadPointAt(center);
    }
}

/*
==============================================================

ENTRY POINTS

==============================================================
*/

// For test ISOs (xbox_padkey): what the controller is on
const char *UI_Xbox_PadFocusName(void)
{
    UIWidget *focus = pad_focus;
    Menu     *menu  = menuManager.CurrentMenu();

    return va("menu %s focus %s catcher %d mouse %d,%d", menu ? menu->m_name.c_str() : "-", focus ? focus->getName() : "-",
              Key_GetCatcher(), cl.mousex, cl.mousey);
}

/*
====================
UI_Xbox_PadFrame

Called every frame before the UI reads the mouse. Gives a widget the focus
when a menu opens (restoring the one it had when the player left it), and
keeps the pointer on the focus (widgets can move, e.g. menu motion).
====================
*/
void UI_Xbox_PadFrame(void)
{
    Menu     *menu = menuManager.CurrentMenu();
    UIWidget *root = UI_PadRoot();

    if (menu != pad_menu || root != pad_root) {
        UIWidget *restore = NULL;
        int       i;

        // remember the focus of the menu being left...
        for (i = 0; i < PAD_SAVED_MENUS; i++) {
            if (pad_saved[i].menu == pad_menu) {
                break;
            }
        }
        if (i == PAD_SAVED_MENUS) {
            i             = pad_savedNext;
            pad_savedNext = (pad_savedNext + 1) % PAD_SAVED_MENUS;
        }
        pad_saved[i].menu  = pad_menu;
        pad_saved[i].focus = pad_focus;

        // ...and restore the one of the menu coming up
        for (i = 0; i < PAD_SAVED_MENUS; i++) {
            if (menu && pad_saved[i].menu == menu) {
                restore = pad_saved[i].focus;
            }
        }

        pad_menu         = menu;
        pad_root         = root;
        pad_settleFrames = 0;
        pad_focus        = restore;
        pad_pulldown     = NULL;
    }

    if (!root || !(Key_GetCatcher() & KEYCATCH_UI)) {
        return;
    }

    if (pad_pulldown) {
        if (UI_PadOpenPopup()) {
            // the pointer belongs to the popup while it is open; start on its first item
            if (!pad_popupPlaced) {
                pad_popupPlaced = true;
                UI_PadPopupStep(0);
            }
            return;
        }
        if (cl.mouseButtons & 1) {
            // pressed this frame, the popup opens when the UI sees the click
            return;
        }
        pad_pulldown = NULL;
    }

    // wait for the widgets to get their screen position
    if (pad_settleFrames < 2) {
        pad_settleFrames++;
        return;
    }

    UIWidget *list[PAD_MAX_WIDGETS];
    const int count = UI_PadGather(list);

    if (!count) {
        return;
    }

    UIWidget *current = pad_focus;
    UI_PadFocus(current && UI_PadInList(current, list, count) ? current : UI_PadFirst(list, count));
}

/*
====================
UI_Xbox_PadKey

Called first thing by CL_KeyEvent. Returns qtrue when the key was used up
here; otherwise the key (and its state) may have been replaced by the
mouse/keyboard equivalent and is processed normally.
====================
*/
qboolean UI_Xbox_PadKey(int *key, qboolean *down)
{
    // START always behaves like ESC: opens the menu in game, skips cinematics
    if (*key == K_PAD0_START) {
        *key = K_ESCAPE;
        return qfalse;
    }

    if (clc.state == CA_CINEMATIC) {
        if (*key == K_PAD0_A || *key == K_PAD0_B) {
            *key = K_ESCAPE;
        }
        return qfalse;
    }

    if (!(Key_GetCatcher() & KEYCATCH_UI) || UI_BindActive()) {
        return qfalse;
    }

    if (*key == K_PAD0_A && !*down && pad_swallowRelease) {
        // the release of the A press that opened a pulldown menu: keep the
        // mouse button held so the menu stays open
        pad_swallowRelease = false;
        return qtrue;
    }

    // a pulldown menu is open: the controller picks one of its items
    if (pad_pulldown && UI_PadOpenPopup()) {
        switch (*key) {
        case K_PAD0_A:
            if (*down) {
                return qtrue;
            }
            // releasing the mouse button over the item selects it
            *key         = K_MOUSE1;
            pad_pulldown = NULL;
            return qfalse;
        case K_PAD0_B:
            if (!*down) {
                return qtrue;
            }
            // release away from the popup: nothing is selected
            if (pad_focus) {
                UI_PadFocus(pad_focus);
            }
            *key         = K_MOUSE1;
            *down        = qfalse;
            pad_pulldown = NULL;
            return qfalse;
        case K_PAD0_DPAD_UP:
        case K_PAD0_LEFTSTICK_UP:
            if (*down) {
                UI_PadPopupStep(-1);
            }
            return qtrue;
        case K_PAD0_DPAD_DOWN:
        case K_PAD0_LEFTSTICK_DOWN:
            if (*down) {
                UI_PadPopupStep(1);
            }
            return qtrue;
        default:
            return qtrue;
        }
    }

    switch (*key) {
    case K_PAD0_A:
        if (*down && pad_focus && UI_PadIsList(pad_focus)) {
            UIWidget *list = pad_focus;
            list->KeyEvent(K_ENTER, cls.realtime);
            return qtrue;
        }
        if (!*down && pad_focus && UI_PadIsList(pad_focus)) {
            return qtrue;
        }
        if (*down && pad_focus && pad_focus->isSubclassOf(UIPulldownMenu)) {
            // press the mouse button on the pulldown and keep it down
            pad_pulldown       = pad_focus;
            pad_swallowRelease = true;
            pad_popupPlaced    = false;
            // other widgets (the container, labels) can cover the pulldown;
            // make sure the click reaches it
            uWinMan.setFirstResponder(pad_pulldown);
        }
        *key = K_MOUSE1;
        return qfalse;
    case K_PAD0_B:
        *key = K_ESCAPE;
        return qfalse;
    case K_PAD0_X:
        // key binding grid: clear the selected binding
        if (pad_focus && pad_focus->isSubclassOf(UIFakkBindList)) {
            if (*down) {
                UIWidget *grid = pad_focus;
                grid->KeyEvent(K_DEL, cls.realtime);
            }
            return qtrue;
        }
        return qfalse;
    case K_PAD0_DPAD_UP:
    case K_PAD0_LEFTSTICK_UP:
        if (*down) {
            UI_PadDirection(0, -1);
        }
        return qtrue;
    case K_PAD0_DPAD_DOWN:
    case K_PAD0_LEFTSTICK_DOWN:
        if (*down) {
            UI_PadDirection(0, 1);
        }
        return qtrue;
    case K_PAD0_DPAD_LEFT:
    case K_PAD0_LEFTSTICK_LEFT:
        if (*down) {
            UI_PadDirection(-1, 0);
        }
        return qtrue;
    case K_PAD0_DPAD_RIGHT:
    case K_PAD0_LEFTSTICK_RIGHT:
        if (*down) {
            UI_PadDirection(1, 0);
        }
        return qtrue;
    default:
        return qfalse;
    }
}

/*
====================
UI_Xbox_PadDraw

Called after the UI has been drawn. Buttons show the focus themselves (hover
material), the other widgets (sliders, pulldowns, check boxes, lists, text
fields) get a pulsing frame around them.
====================
*/
void UI_Xbox_PadDraw(void)
{
    UIWidget *focus = pad_focus;

    if (!focus || !(Key_GetCatcher() & KEYCATCH_UI) || UI_PadOpenPopup() || focus->isSubclassOf(UIButtonBase)
        || !focus->getShow()) {
        return;
    }

    const UISize2D  size   = focus->getSize();
    const UIPoint2D origin = focus->getGlobalPoint(UIPoint2D(0, 0));
    const float     pulse  = 0.65f + 0.35f * sin(cls.realtime * 0.006f);
    const float     color[4] = {1.0f, 0.8f, 0.25f, pulse};
    const float     t      = 2.0f;
    const float     x      = origin.x - t - 1;
    const float     y      = origin.y - t - 1;
    const float     w      = size.width + (t + 1) * 2;
    const float     h      = size.height + (t + 1) * 2;

    re.Set2DWindow(0, 0, uid.vidWidth, uid.vidHeight, 0, uid.vidWidth, uid.vidHeight, 0, -1, 1);
    re.Scissor(0, 0, uid.vidWidth, uid.vidHeight);
    re.SetColor(color);
    re.DrawBox(x, y, w, t);
    re.DrawBox(x, y + h - t, w, t);
    re.DrawBox(x, y + t, t, h - t * 2);
    re.DrawBox(x + w - t, y + t, t, h - t * 2);
    re.SetColor(NULL);
}
