/*
 * windows.c: Windows front end for my puzzle collection.
 */

#include <windows.h>
#include <commctrl.h>

#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "puzzles.h"

#define IDM_NEW       0x0010
#define IDM_RESTART   0x0020
#define IDM_UNDO      0x0030
#define IDM_REDO      0x0040
#define IDM_QUIT      0x0050
#define IDM_PRESETS   0x0100

#ifdef DEBUG
static FILE *debug_fp = NULL;
static HANDLE debug_hdl = INVALID_HANDLE_VALUE;
static int debug_got_console = 0;

void dputs(char *buf)
{
    DWORD dw;

    if (!debug_got_console) {
	if (AllocConsole()) {
	    debug_got_console = 1;
	    debug_hdl = GetStdHandle(STD_OUTPUT_HANDLE);
	}
    }
    if (!debug_fp) {
	debug_fp = fopen("debug.log", "w");
    }

    if (debug_hdl != INVALID_HANDLE_VALUE) {
	WriteFile(debug_hdl, buf, strlen(buf), &dw, NULL);
    }
    fputs(buf, debug_fp);
    fflush(debug_fp);
}

void debug_printf(char *fmt, ...)
{
    char buf[4096];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    dputs(buf);
    va_end(ap);
}

#define debug(x) (debug_printf x)

#else

#define debug(x)

#endif

struct font {
    HFONT font;
    int type;
    int size;
};

struct frontend {
    midend_data *me;
    HWND hwnd, statusbar;
    HBITMAP bitmap, prevbm;
    HDC hdc_bm;
    COLORREF *colours;
    HBRUSH *brushes;
    HPEN *pens;
    HRGN clip;
    UINT timer;
    int npresets;
    game_params **presets;
    struct font *fonts;
    int nfonts, fontsize;
};

void fatal(char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    MessageBox(NULL, buf, "Fatal error", MB_ICONEXCLAMATION | MB_OK);

    exit(1);
}

void status_bar(frontend *fe, char *text)
{
    SetWindowText(fe->statusbar, text);
}

void frontend_default_colour(frontend *fe, float *output)
{
    DWORD c = GetSysColor(COLOR_MENU); /* ick */

    output[0] = (float)(GetRValue(c) / 255.0);
    output[1] = (float)(GetGValue(c) / 255.0);
    output[2] = (float)(GetBValue(c) / 255.0);
}

void clip(frontend *fe, int x, int y, int w, int h)
{
    if (!fe->clip) {
	fe->clip = CreateRectRgn(0, 0, 1, 1);
	GetClipRgn(fe->hdc_bm, fe->clip);
    }

    IntersectClipRect(fe->hdc_bm, x, y, x+w, y+h);
}

void unclip(frontend *fe)
{
    assert(fe->clip);
    SelectClipRgn(fe->hdc_bm, fe->clip);
}

void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text)
{
    int i;

    /*
     * Find or create the font.
     */
    for (i = 0; i < fe->nfonts; i++)
        if (fe->fonts[i].type == fonttype && fe->fonts[i].size == fontsize)
            break;

    if (i == fe->nfonts) {
        if (fe->fontsize <= fe->nfonts) {
            fe->fontsize = fe->nfonts + 10;
            fe->fonts = sresize(fe->fonts, fe->fontsize, struct font);
        }

        fe->nfonts++;

        fe->fonts[i].type = fonttype;
        fe->fonts[i].size = fontsize;

        /*
         * FIXME: Really I should make at least _some_ effort to
         * pick the correct font.
         */
        fe->fonts[i].font = CreateFont(-fontsize, 0, 0, 0, 0,
				       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				       DEFAULT_QUALITY,
				       (fonttype == FONT_FIXED ?
					FIXED_PITCH | FF_DONTCARE :
					VARIABLE_PITCH | FF_SWISS),
				       NULL);
    }

    /*
     * Position and draw the text.
     */
    {
	HFONT oldfont;
	TEXTMETRIC tm;
	SIZE size;

	oldfont = SelectObject(fe->hdc_bm, fe->fonts[i].font);
	if (GetTextMetrics(fe->hdc_bm, &tm)) {
	    if (align & ALIGN_VCENTRE)
		y -= (tm.tmAscent+tm.tmDescent)/2;
	    else
		y -= tm.tmAscent;
	}
	if (GetTextExtentPoint32(fe->hdc_bm, text, strlen(text), &size)) {
	    if (align & ALIGN_HCENTRE)
		x -= size.cx / 2;
	    else if (align & ALIGN_HRIGHT)
		x -= size.cx;
	}
	SetBkMode(fe->hdc_bm, TRANSPARENT);
	TextOut(fe->hdc_bm, x, y, text, strlen(text));
	SelectObject(fe->hdc_bm, oldfont);
    }
}

void draw_rect(frontend *fe, int x, int y, int w, int h, int colour)
{
    if (w == 1 && h == 1) {
	/*
	 * Rectangle() appears to get uppity if asked to draw a 1x1
	 * rectangle, presumably on the grounds that that's beneath
	 * its dignity and you ought to be using SetPixel instead.
	 * So I will.
	 */
	SetPixel(fe->hdc_bm, x, y, fe->colours[colour]);
    } else {
	HBRUSH oldbrush = SelectObject(fe->hdc_bm, fe->brushes[colour]);
	HPEN oldpen = SelectObject(fe->hdc_bm, fe->pens[colour]);
	Rectangle(fe->hdc_bm, x, y, x+w, y+h);
	SelectObject(fe->hdc_bm, oldbrush);
	SelectObject(fe->hdc_bm, oldpen);
    }
}

void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour)
{
    HPEN oldpen = SelectObject(fe->hdc_bm, fe->pens[colour]);
    MoveToEx(fe->hdc_bm, x1, y1, NULL);
    LineTo(fe->hdc_bm, x2, y2);
    SetPixel(fe->hdc_bm, x2, y2, fe->colours[colour]);
    SelectObject(fe->hdc_bm, oldpen);
}

void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour)
{
    POINT *pts = snewn(npoints+1, POINT);
    int i;

    for (i = 0; i <= npoints; i++) {
	int j = (i < npoints ? i : 0);
	pts[i].x = coords[j*2];
	pts[i].y = coords[j*2+1];
    }

    if (fill) {
	HBRUSH oldbrush = SelectObject(fe->hdc_bm, fe->brushes[colour]);
	HPEN oldpen = SelectObject(fe->hdc_bm, fe->pens[colour]);
	Polygon(fe->hdc_bm, pts, npoints);
	SelectObject(fe->hdc_bm, oldbrush);
	SelectObject(fe->hdc_bm, oldpen);
    } else {
	HPEN oldpen = SelectObject(fe->hdc_bm, fe->pens[colour]);
	Polyline(fe->hdc_bm, pts, npoints+1);
	SelectObject(fe->hdc_bm, oldpen);
    }

    sfree(pts);
}

void start_draw(frontend *fe)
{
    HDC hdc_win;
    hdc_win = GetDC(fe->hwnd);
    fe->hdc_bm = CreateCompatibleDC(hdc_win);
    fe->prevbm = SelectObject(fe->hdc_bm, fe->bitmap);
    ReleaseDC(fe->hwnd, hdc_win);
    fe->clip = NULL;
}

void draw_update(frontend *fe, int x, int y, int w, int h)
{
    RECT r;

    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;

    InvalidateRect(fe->hwnd, &r, FALSE);
}

void end_draw(frontend *fe)
{
    SelectObject(fe->hdc_bm, fe->prevbm);
    DeleteDC(fe->hdc_bm);
    if (fe->clip) {
	DeleteObject(fe->clip);
	fe->clip = NULL;
    }
}

void deactivate_timer(frontend *fe)
{
    KillTimer(fe->hwnd, fe->timer);
    fe->timer = 0;
}

void activate_timer(frontend *fe)
{
    fe->timer = SetTimer(fe->hwnd, fe->timer, 20, NULL);
}

static frontend *new_window(HINSTANCE inst)
{
    frontend *fe;
    int x, y;
    RECT r, sr;
    HDC hdc;

    fe = snew(frontend);
    fe->me = midend_new(fe);
    midend_new_game(fe->me, NULL);
    midend_size(fe->me, &x, &y);

    fe->timer = 0;

    {
	int i, ncolours;
        float *colours;

        colours = midend_colours(fe->me, &ncolours);

	fe->colours = snewn(ncolours, COLORREF);
	fe->brushes = snewn(ncolours, HBRUSH);
	fe->pens = snewn(ncolours, HPEN);

	for (i = 0; i < ncolours; i++) {
	    fe->colours[i] = RGB(255 * colours[i*3+0],
				 255 * colours[i*3+1],
				 255 * colours[i*3+2]);
	    fe->brushes[i] = CreateSolidBrush(fe->colours[i]);
	    if (!fe->brushes[i])
		MessageBox(fe->hwnd, "ooh", "eck", MB_OK);
	    fe->pens[i] = CreatePen(PS_SOLID, 1, fe->colours[i]);
	}
    }

    r.left = r.top = 0;
    r.right = x;
    r.bottom = y;
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW &~
		       (WS_THICKFRAME | WS_MAXIMIZEBOX | WS_OVERLAPPED),
		       TRUE, 0);

    fe->hwnd = CreateWindowEx(0, game_name, game_name,
			      WS_OVERLAPPEDWINDOW &~
			      (WS_THICKFRAME | WS_MAXIMIZEBOX),
			      CW_USEDEFAULT, CW_USEDEFAULT,
			      r.right - r.left, r.bottom - r.top,
			      NULL, NULL, inst, NULL);

    {
	HMENU bar = CreateMenu();
	HMENU menu = CreateMenu();

	AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT)menu, "Game");
	AppendMenu(menu, MF_ENABLED, IDM_NEW, "New");
	AppendMenu(menu, MF_ENABLED, IDM_RESTART, "Restart");

	if ((fe->npresets = midend_num_presets(fe->me)) > 0) {
	    HMENU sub = CreateMenu();
	    int i;

	    AppendMenu(menu, MF_ENABLED|MF_POPUP, (UINT)sub, "Type");

	    fe->presets = snewn(fe->npresets, game_params *);

	    for (i = 0; i < fe->npresets; i++) {
		char *name;

		midend_fetch_preset(fe->me, i, &name, &fe->presets[i]);

		/*
		 * FIXME: we ought to go through and do something
		 * with ampersands here.
		 */

		AppendMenu(sub, MF_ENABLED, IDM_PRESETS + 0x10 * i, name);
	    }
	}

	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_UNDO, "Undo");
	AppendMenu(menu, MF_ENABLED, IDM_REDO, "Redo");
	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_QUIT, "Exit");
	SetMenu(fe->hwnd, bar);
    }

    if (midend_wants_statusbar(fe->me)) {
	fe->statusbar = CreateWindowEx(0, STATUSCLASSNAME, "ooh",
				       WS_CHILD | WS_VISIBLE,
				       0, 0, 0, 0, /* status bar does these */
				       fe->hwnd, NULL, inst, NULL);
	GetWindowRect(fe->statusbar, &sr);
	SetWindowPos(fe->hwnd, NULL, 0, 0,
		     r.right - r.left, r.bottom - r.top + sr.bottom - sr.top,
		     SWP_NOMOVE | SWP_NOZORDER);
	SetWindowPos(fe->statusbar, NULL, 0, y, x, sr.bottom - sr.top,
		     SWP_NOZORDER);
    } else {
	fe->statusbar = NULL;
    }

    hdc = GetDC(fe->hwnd);
    fe->bitmap = CreateCompatibleBitmap(hdc, x, y);
    ReleaseDC(fe->hwnd, hdc);

    SetWindowLong(fe->hwnd, GWL_USERDATA, (LONG)fe);

    ShowWindow(fe->hwnd, SW_NORMAL);
    SetForegroundWindow(fe->hwnd);

    midend_redraw(fe->me);

    return fe;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (message) {
      case WM_CLOSE:
	DestroyWindow(hwnd);
	return 0;
      case WM_COMMAND:
	switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
	  case IDM_NEW:
	    if (!midend_process_key(fe->me, 0, 0, 'n'))
		PostQuitMessage(0);
	    break;
	  case IDM_RESTART:
	    if (!midend_process_key(fe->me, 0, 0, 'r'))
		PostQuitMessage(0);
	    break;
	  case IDM_UNDO:
	    if (!midend_process_key(fe->me, 0, 0, 'u'))
		PostQuitMessage(0);
	    break;
	  case IDM_REDO:
	    if (!midend_process_key(fe->me, 0, 0, '\x12'))
		PostQuitMessage(0);
	    break;
	  case IDM_QUIT:
	    if (!midend_process_key(fe->me, 0, 0, 'q'))
		PostQuitMessage(0);
	    break;
	  default:
	    {
		int p = ((wParam &~ 0xF) - IDM_PRESETS) / 0x10;

		if (p >= 0 && p < fe->npresets) {
		    RECT r, sr;
		    HDC hdc;
		    int x, y;

		    midend_set_params(fe->me, fe->presets[p]);
		    midend_new_game(fe->me, NULL);
		    midend_size(fe->me, &x, &y);

		    r.left = r.top = 0;
		    r.right = x;
		    r.bottom = y;
		    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW &~
				       (WS_THICKFRAME | WS_MAXIMIZEBOX |
					WS_OVERLAPPED),
				       TRUE, 0);

		    if (fe->statusbar != NULL) {
			GetWindowRect(fe->statusbar, &sr);
		    } else {
			sr.left = sr.right = sr.top = sr.bottom = 0;
		    }
		    SetWindowPos(fe->hwnd, NULL, 0, 0,
				 r.right - r.left,
				 r.bottom - r.top + sr.bottom - sr.top,
				 SWP_NOMOVE | SWP_NOZORDER);
		    if (fe->statusbar != NULL)
			SetWindowPos(fe->statusbar, NULL, 0, y, x,
				     sr.bottom - sr.top, SWP_NOZORDER);

		    DeleteObject(fe->bitmap);

		    hdc = GetDC(fe->hwnd);
		    fe->bitmap = CreateCompatibleBitmap(hdc, x, y);
		    ReleaseDC(fe->hwnd, hdc);

		    midend_redraw(fe->me);
		}
	    }
	    break;
	}
	break;
      case WM_DESTROY:
	PostQuitMessage(0);
	return 0;
      case WM_PAINT:
	{
	    PAINTSTRUCT p;
	    HDC hdc, hdc2;
	    HBITMAP prevbm;

	    hdc = BeginPaint(hwnd, &p);
	    hdc2 = CreateCompatibleDC(hdc);
	    prevbm = SelectObject(hdc2, fe->bitmap);
	    BitBlt(hdc,
		   p.rcPaint.left, p.rcPaint.top,
		   p.rcPaint.right - p.rcPaint.left,
		   p.rcPaint.bottom - p.rcPaint.top,
		   hdc2,
		   p.rcPaint.left, p.rcPaint.top,
		   SRCCOPY);
	    SelectObject(hdc2, prevbm);
	    DeleteDC(hdc2);
	    EndPaint(hwnd, &p);
	}
	return 0;
      case WM_KEYDOWN:
	{
	    int key = -1;

	    switch (wParam) {
	      case VK_LEFT: key = CURSOR_LEFT; break;
	      case VK_RIGHT: key = CURSOR_RIGHT; break;
	      case VK_UP: key = CURSOR_UP; break;
	      case VK_DOWN: key = CURSOR_DOWN; break;
		/*
		 * Diagonal keys on the numeric keypad.
		 */
	      case VK_PRIOR:
		if (!(lParam & 0x01000000)) key = CURSOR_UP_RIGHT;
		break;
	      case VK_NEXT:
		if (!(lParam & 0x01000000)) key = CURSOR_DOWN_RIGHT;
		break;
	      case VK_HOME:
		if (!(lParam & 0x01000000)) key = CURSOR_UP_LEFT;
		break;
	      case VK_END:
		if (!(lParam & 0x01000000)) key = CURSOR_DOWN_LEFT;
		break;
		/*
		 * Numeric keypad keys with Num Lock on.
		 */
	      case VK_NUMPAD4: key = CURSOR_LEFT; break;
	      case VK_NUMPAD6: key = CURSOR_RIGHT; break;
	      case VK_NUMPAD8: key = CURSOR_UP; break;
	      case VK_NUMPAD2: key = CURSOR_DOWN; break;
	      case VK_NUMPAD9: key = CURSOR_UP_RIGHT; break;
	      case VK_NUMPAD3: key = CURSOR_DOWN_RIGHT; break;
	      case VK_NUMPAD7: key = CURSOR_UP_LEFT; break;
	      case VK_NUMPAD1: key = CURSOR_DOWN_LEFT; break;
	    }

	    if (key != -1) {
		if (!midend_process_key(fe->me, 0, 0, key))
		    PostQuitMessage(0);
	    } else {
		MSG m;
		m.hwnd = hwnd;
		m.message = WM_KEYDOWN;
		m.wParam = wParam;
		m.lParam = lParam & 0xdfff;
		TranslateMessage(&m);
	    }
	}
	break;
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
	{
	    int button;

	    /*
	     * Shift-clicks count as middle-clicks, since otherwise
	     * two-button Windows users won't have any kind of
	     * middle click to use.
	     */
	    if (message == WM_MBUTTONDOWN || (wParam & MK_SHIFT))
		button = MIDDLE_BUTTON;
	    else if (message == WM_LBUTTONDOWN)
		button = LEFT_BUTTON;
	    else
		button = RIGHT_BUTTON;
		
	    if (!midend_process_key(fe->me, LOWORD(lParam),
				    HIWORD(lParam), button))
		PostQuitMessage(0);
	}
	break;
      case WM_CHAR:
	if (!midend_process_key(fe->me, 0, 0, (unsigned char)wParam))
	    PostQuitMessage(0);
	return 0;
      case WM_TIMER:
	if (fe->timer)
	    midend_timer(fe->me, (float)0.02);
	return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;

    srand(time(NULL));

    InitCommonControls();

    if (!prev) {
	WNDCLASS wndclass;

	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	wndclass.hIcon = LoadIcon(inst, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = game_name;

	RegisterClass(&wndclass);
    }

    new_window(inst);

    while (GetMessage(&msg, NULL, 0, 0)) {
	DispatchMessage(&msg);
    }

    return msg.wParam;
}