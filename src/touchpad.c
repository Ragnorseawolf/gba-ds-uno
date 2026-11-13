/* On-screen touch gamepad [motog patch 0002].
 *
 * Upstream GBA DS has no on-screen game controls at all: on a phone the
 * only ways in are a physical gamepad or the stylus-style menus. This file
 * adds a translucent gamepad overlay for the game screen — D-pad (with
 * diagonals), A/B/X/Y diamond, L/R shoulders, Start and Select — drawn
 * after the game frame and fed into the same btnNow[] every other input
 * source uses, so hotkeys, the pause-menu combo (Start+Select) and the
 * emulators' own key handling all see touch presses exactly like pad
 * presses.
 *
 * Fingers are polled through SDL's touch state (per finger id, so the
 * D-pad and A can be held together), converted to screen-local pixels the
 * same way ui.c's queue_pointer() maps finger events. A pointer filter
 * (ui_set_pointer_filter) claims the matching queued pointer events so a
 * tap on the pad doesn't also open the pause menu.
 *
 * Mode comes from gba-ds.cfg's touch_controls = auto|on|off (default
 * auto): in auto the pad shows until a physical gamepad button is
 * pressed, hides 2.5 s later, and comes back the moment a finger touches
 * the screen again. Inert without touch hardware, so desktop and
 * handheld builds are unaffected in practice.
 */
#include "touchpad.h"

#include <SDL.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------ layout */

enum { MODE_AUTO, MODE_ON, MODE_OFF };

static int tpMode = MODE_AUTO;
static bool tpArmed;             /* in game, menu closed, mode != off */
static bool tpVisible;           /* the pad is on screen this frame */
static bool tpBtn[B_COUNT];      /* what the fingers are pressing now */
static int tpScreen = -1;        /* screen the layout belongs to */
static Uint32 tpLastPoll;        /* last touchpad_poll tick (staleness) */
static Uint32 tpLastPhys;        /* last tick a physical button was down */
static Uint32 tpLastTouch;       /* last tick any finger was down */

/* Control geometry in screen-local pixels, from the screen's size. */
static struct {
	int w, h;        /* screen size */
	float u;         /* ui_px unit */
	int m;           /* edge margin */
	/* D-pad: a plus centred at (dcx, dcy), half-size dr */
	int dcx, dcy, dr;
	/* face diamond centred at (fcx, fcy): X top, A right, B bottom, Y left */
	int fcx, fcy, fr, fs; /* button radius, centre spacing */
	/* shoulder bars and the Start/Select pills */
	SDL_Rect lRect, rRect, selRect, startRect;
} tpLay;

static void tp_layout(int screen) {
	const SDL_Rect* r = &screens[screen].rect;
	tpLay.w = r->w;
	tpLay.h = r->h;
	tpLay.u = ui_px(screen);
	float u = tpLay.u;
	int m = (int) (3 * u);
	tpLay.m = m;
	int dr = (int) (24 * u);
	tpLay.dcx = m + dr;
	tpLay.dcy = tpLay.h - m - dr;
	tpLay.dr = dr;
	int fr = (int) (13 * u);
	int fs = (int) (2 * fr + 5 * u);
	tpLay.fr = fr;
	tpLay.fs = fs;
	tpLay.fcx = tpLay.w - m - fr - fs;
	tpLay.fcy = tpLay.h - m - fr - fs;
	int barW = (int) (24 * u), barH = (int) (8 * u);
	tpLay.lRect = (SDL_Rect) { m, m, barW, barH };
	tpLay.rRect = (SDL_Rect) { tpLay.w - m - barW, m, barW, barH };
	int pillW = (int) (17 * u), pillH = (int) (6 * u);
	int pillY = tpLay.h - m - pillH;
	tpLay.selRect = (SDL_Rect) { tpLay.w / 2 - pillW - (int) (2 * u), pillY, pillW, pillH };
	tpLay.startRect = (SDL_Rect) { tpLay.w / 2 + (int) (2 * u), pillY, pillW, pillH };
	tpScreen = screen;
}

/* Which logical button a screen-local point presses, or -1. The D-pad
 * reports one direction per axis; diagonals come out as two buttons when
 * the poll folds the point in (see tp_press_point). */
static bool tp_in_circle(int x, int y, int cx, int cy, int r) {
	int dx = x - cx, dy = y - cy;
	return dx * dx + dy * dy <= r * r;
}

static bool tp_hit(int x, int y) {
	int hitR = (int) (tpLay.fr * 1.25f);
	if (tp_in_circle(x, y, tpLay.fcx, tpLay.fcy - tpLay.fs, hitR)) return true; /* X */
	if (tp_in_circle(x, y, tpLay.fcx + tpLay.fs, tpLay.fcy, hitR)) return true; /* A */
	if (tp_in_circle(x, y, tpLay.fcx, tpLay.fcy + tpLay.fs, hitR)) return true; /* B */
	if (tp_in_circle(x, y, tpLay.fcx - tpLay.fs, tpLay.fcy, hitR)) return true; /* Y */
	SDL_Point pt = { x, y };
	if (SDL_PointInRect(&pt, &tpLay.lRect) || SDL_PointInRect(&pt, &tpLay.rRect)) return true;
	if (SDL_PointInRect(&pt, &tpLay.selRect) || SDL_PointInRect(&pt, &tpLay.startRect)) return true;
	int dx = x - tpLay.dcx, dy = y - tpLay.dcy;
	return dx >= -tpLay.dr && dx <= tpLay.dr && dy >= -tpLay.dr && dy <= tpLay.dr;
}

static void tp_press_point(bool out[B_COUNT], int x, int y) {
	int hitR = (int) (tpLay.fr * 1.25f);
	if (tp_in_circle(x, y, tpLay.fcx, tpLay.fcy - tpLay.fs, hitR)) out[B_X] = true;
	if (tp_in_circle(x, y, tpLay.fcx + tpLay.fs, tpLay.fcy, hitR)) out[B_A] = true;
	if (tp_in_circle(x, y, tpLay.fcx, tpLay.fcy + tpLay.fs, hitR)) out[B_B] = true;
	if (tp_in_circle(x, y, tpLay.fcx - tpLay.fs, tpLay.fcy, hitR)) out[B_Y] = true;
	SDL_Point pt = { x, y };
	if (SDL_PointInRect(&pt, &tpLay.lRect)) out[B_L] = true;
	if (SDL_PointInRect(&pt, &tpLay.rRect)) out[B_R] = true;
	if (SDL_PointInRect(&pt, &tpLay.selRect)) out[B_SELECT] = true;
	if (SDL_PointInRect(&pt, &tpLay.startRect)) out[B_START] = true;
	/* D-pad: past the dead zone, each axis presses its direction (both
	 * axes together = a diagonal) */
	int dx = x - tpLay.dcx, dy = y - tpLay.dcy;
	if (dx >= -tpLay.dr && dx <= tpLay.dr && dy >= -tpLay.dr && dy <= tpLay.dr) {
		int dead = (int) (4 * tpLay.u);
		if (dx < -dead) out[B_LEFT] = true;
		if (dx > dead) out[B_RIGHT] = true;
		if (dy < -dead) out[B_UP] = true;
		if (dy > dead) out[B_DOWN] = true;
	}
}

/* ------------------------------------------------------------ polling */

/* One finger (normalized SDL touch coordinates) folded into `out`, in the
 * game screen's local pixels; returns false if it isn't on that screen.
 * Mirrors ui.c queue_pointer()'s two touch_map modes. */
static bool tp_finger_point(const Config* cfg, int screen, float fx, float fy, int* ox, int* oy) {
	const SDL_Rect* r = &screens[screen].rect;
	if (!strcmp(cfg->touchMap, "screen")) {
		*ox = (int) (fx * r->w);
		*oy = (int) (fy * r->h);
		return true;
	}
	int vw = 0, vh = 0;
	SDL_GetRendererOutputSize(ui_ren(screen), &vw, &vh);
	if (!vw || !vh) return false;
	int x = (int) (fx * vw) - r->x, y = (int) (fy * vh) - r->y;
	if (x < 0 || y < 0 || x >= r->w || y >= r->h) return false;
	*ox = x, *oy = y;
	return true;
}

void touchpad_poll(const Config* cfg, bool menuOpen, int screen) {
	tpLastPoll = SDL_GetTicks();
	tpMode = !strcasecmp(cfg->touchControls, "on") ? MODE_ON :
	         !strcasecmp(cfg->touchControls, "off") ? MODE_OFF : MODE_AUTO;
	memset(tpBtn, 0, sizeof(tpBtn));
	tpArmed = !menuOpen && tpMode != MODE_OFF && screen >= 0 && screen < nScreens;
	if (!tpArmed) {
		tpVisible = false;
		return;
	}
	tp_layout(screen);
	Uint32 now = tpLastPoll;
	/* btnNow at this point is physical input only (ui_read_buttons has
	 * just run; nothing else has been folded in yet) */
	for (int b = 0; b < B_COUNT; ++b)
		if (btnNow[b]) tpLastPhys = now;
	bool anyFinger = false;
	int nDev = SDL_GetNumTouchDevices();
	for (int d = 0; d < nDev; ++d) {
		SDL_TouchID dev = SDL_GetTouchDevice(d);
		int nf = SDL_GetNumTouchFingers(dev);
		for (int i = 0; i < nf; ++i) {
			const SDL_Finger* f = SDL_GetTouchFinger(dev, i);
			if (!f) continue;
			int x, y;
			if (!tp_finger_point(cfg, screen, f->x, f->y, &x, &y)) continue;
			anyFinger = true;
			tp_press_point(tpBtn, x, y);
		}
	}
	if (anyFinger) tpLastTouch = now;
	tpVisible = tpMode == MODE_ON || anyFinger || now - tpLastPhys > 2500 ||
	            (tpLastTouch && now - tpLastTouch < 2500);
	for (int b = 0; b < B_COUNT; ++b)
		if (tpBtn[b]) btnNow[b] = true;
}

bool touchpad_pointer_filter(const Pointer* p) {
	if (!tpArmed || !tpVisible || !p) return false;
	if (SDL_GetTicks() - tpLastPoll > 150) return false; /* not in the game loop */
	if (p->screen != tpScreen || p->kind == 0) return false;
	return tp_hit(p->x, p->y);
}

/* ------------------------------------------------------------ drawing */

static const SDL_Color TP_BASE = { 15, 17, 28, 105 };    /* ~40% opaque */
static const SDL_Color TP_PRESS = { 120, 175, 255, 180 };
static const SDL_Color TP_LABEL = { 255, 255, 255, 230 };
static const SDL_Color TP_LABEL_PRESS = { 10, 12, 20, 255 };

static void tp_disc(int screen, int cx, int cy, int r, bool pressed, const char* label) {
	SDL_Rect q = { cx - r, cy - r, 2 * r, 2 * r };
	ui_fill_round(screen, q, pressed ? TP_PRESS : TP_BASE, r);
	if (label) ui_label(screen, cx, cy, (int) (r * 1.05f), label, pressed ? TP_LABEL_PRESS : TP_LABEL);
}

static void tp_pill(int screen, SDL_Rect q, bool pressed, const char* label, int capPx) {
	ui_fill_round(screen, q, pressed ? TP_PRESS : TP_BASE, q.h / 2);
	if (label) ui_label(screen, q.x + q.w / 2, q.y + q.h / 2, capPx, label, pressed ? TP_LABEL_PRESS : TP_LABEL);
}

void touchpad_draw(int screen) {
	if (!tpArmed || !tpVisible || screen != tpScreen) return;
	float u = tpLay.u;
	/* shoulders */
	tp_pill(screen, tpLay.lRect, tpBtn[B_L], "L", (int) (tpLay.lRect.h * 0.8f));
	tp_pill(screen, tpLay.rRect, tpBtn[B_R], "R", (int) (tpLay.rRect.h * 0.8f));
	/* D-pad: a plus, the pressed arm lit */
	int arm = (int) (4.5f * u), dr = tpLay.dr;
	SDL_Rect vBar = { tpLay.dcx - arm, tpLay.dcy - dr, 2 * arm, 2 * dr };
	SDL_Rect hBar = { tpLay.dcx - dr, tpLay.dcy - arm, 2 * dr, 2 * arm };
	ui_fill_round(screen, vBar, TP_BASE, arm / 2);
	ui_fill_round(screen, hBar, TP_BASE, arm / 2);
	if (tpBtn[B_UP]) ui_fill_round(screen, (SDL_Rect) { tpLay.dcx - arm, tpLay.dcy - dr, 2 * arm, dr }, TP_PRESS, arm / 2);
	if (tpBtn[B_DOWN]) ui_fill_round(screen, (SDL_Rect) { tpLay.dcx - arm, tpLay.dcy, 2 * arm, dr }, TP_PRESS, arm / 2);
	if (tpBtn[B_LEFT]) ui_fill_round(screen, (SDL_Rect) { tpLay.dcx - dr, tpLay.dcy - arm, dr, 2 * arm }, TP_PRESS, arm / 2);
	if (tpBtn[B_RIGHT]) ui_fill_round(screen, (SDL_Rect) { tpLay.dcx, tpLay.dcy - arm, dr, 2 * arm }, TP_PRESS, arm / 2);
	int cr = (int) (3.5f * u);
	ui_fill_round(screen, (SDL_Rect) { tpLay.dcx - cr, tpLay.dcy - cr, 2 * cr, 2 * cr }, TP_BASE, cr);
	/* face diamond: X top, A right, B bottom, Y left (by position) */
	tp_disc(screen, tpLay.fcx, tpLay.fcy - tpLay.fs, tpLay.fr, tpBtn[B_X], "X");
	tp_disc(screen, tpLay.fcx + tpLay.fs, tpLay.fcy, tpLay.fr, tpBtn[B_A], "A");
	tp_disc(screen, tpLay.fcx, tpLay.fcy + tpLay.fs, tpLay.fr, tpBtn[B_B], "B");
	tp_disc(screen, tpLay.fcx - tpLay.fs, tpLay.fcy, tpLay.fr, tpBtn[B_Y], "Y");
	/* Start / Select */
	int cap = (int) (3.2f * u);
	tp_pill(screen, tpLay.selRect, tpBtn[B_SELECT], "SELECT", cap);
	tp_pill(screen, tpLay.startRect, tpBtn[B_START], "START", cap);
}
