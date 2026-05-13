/*
 * AutoClicker - WinAPI, без сторонних библиотек
 * Компиляция: g++ AutoClicker.cpp -o AutoClicker.exe -lgdi32 -luser32 -mwindows
 * Или MSVC: cl AutoClicker.cpp /link user32.lib gdi32.lib
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

// ─── ID элементов ─────────────────────────────────────────────────────────────
#define IDC_START_BTN       100
#define IDC_STOP_BTN        101
#define IDC_CPS_EDIT        102
#define IDC_CPS_SLIDER      103
#define IDC_BTN_LEFT        104
#define IDC_BTN_RIGHT       105
#define IDC_BTN_MIDDLE      106
#define IDC_STATUS_LABEL    107
#define IDC_CPS_LABEL       108
#define IDC_CLICKS_LABEL    109
#define IDC_HOTKEY_LABEL    110
#define IDC_TOGGLE_HOTKEY   111

// ─── Глобальные переменные ────────────────────────────────────────────────────
static HWND   g_hWnd        = nullptr;
static HWND   g_hStartBtn   = nullptr;
static HWND   g_hStopBtn    = nullptr;
static HWND   g_hCpsEdit    = nullptr;
static HWND   g_hSlider     = nullptr;
static HWND   g_hBtnLeft    = nullptr;
static HWND   g_hBtnRight   = nullptr;
static HWND   g_hBtnMiddle  = nullptr;
static HWND   g_hStatusLbl  = nullptr;
static HWND   g_hCpsLbl     = nullptr;
static HWND   g_hClicksLbl  = nullptr;

static std::atomic<bool>     g_running{false};
static std::atomic<bool>     g_stopFlag{false};
static std::atomic<long long> g_clickCount{0};
static std::thread           g_clickThread;

static int    g_selectedButton = 0;   // 0=Left, 1=Right, 2=Middle
static int    g_cps            = 10;  // clicks per second
static bool   g_useHotkey      = true;

// Цвета (тёмная тема)
static const COLORREF CLR_BG        = RGB(18,  18,  24);
static const COLORREF CLR_SURFACE   = RGB(28,  28,  38);
static const COLORREF CLR_ACCENT    = RGB(99,  179, 237);
static const COLORREF CLR_ACCENT2   = RGB(237,  99, 120);
static const COLORREF CLR_GREEN     = RGB( 72, 199, 142);
static const COLORREF CLR_TEXT      = RGB(220, 220, 230);
static const COLORREF CLR_SUBTEXT   = RGB(120, 120, 150);
static const COLORREF CLR_BORDER    = RGB( 45,  45,  65);

static HBRUSH g_hBgBrush     = nullptr;
static HBRUSH g_hSurfBrush   = nullptr;
static HFONT  g_hFontBig     = nullptr;
static HFONT  g_hFontMed     = nullptr;
static HFONT  g_hFontSmall   = nullptr;
static HFONT  g_hFontMono    = nullptr;

// ─── Поток кликов ─────────────────────────────────────────────────────────────
void clickerThread()
{
    while (!g_stopFlag.load())
    {
        int cps = g_cps;
        if (cps < 1)  cps = 1;
        if (cps > 100) cps = 100;

        long long intervalUs = 1000000LL / cps;

        auto start = std::chrono::high_resolution_clock::now();

        INPUT inp[2] = {};
        int btn = g_selectedButton;

        inp[0].type = INPUT_MOUSE;
        inp[1].type = INPUT_MOUSE;

        if (btn == 0) {         // Left
            inp[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inp[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        } else if (btn == 1) {  // Right
            inp[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            inp[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        } else {                // Middle
            inp[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            inp[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        }

        SendInput(2, inp, sizeof(INPUT));
        g_clickCount.fetch_add(1);

        // Обновляем счётчик в GUI (через PostMessage, безопасно из потока)
        PostMessage(g_hWnd, WM_USER + 1, 0, 0);

        auto end   = std::chrono::high_resolution_clock::now();
        long long elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        long long sleep   = intervalUs - elapsed;
        if (sleep > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(sleep));
    }
}

// ─── Управление кликером ──────────────────────────────────────────────────────
void startClicking()
{
    if (g_running.load()) return;
    g_stopFlag.store(false);
    g_clickCount.store(0);
    g_running.store(true);
    g_clickThread = std::thread(clickerThread);

    SetWindowText(g_hStatusLbl, L"● АКТИВЕН");
    EnableWindow(g_hStartBtn, FALSE);
    EnableWindow(g_hStopBtn,  TRUE);
}

void stopClicking()
{
    if (!g_running.load()) return;
    g_stopFlag.store(true);
    if (g_clickThread.joinable())
        g_clickThread.join();
    g_running.store(false);

    SetWindowText(g_hStatusLbl, L"○ ОСТАНОВЛЕН");
    EnableWindow(g_hStartBtn, TRUE);
    EnableWindow(g_hStopBtn,  FALSE);
}

// ─── Рисование кастомных кнопок ───────────────────────────────────────────────
void DrawRoundRect(HDC hdc, RECT rc, int r, COLORREF fill, COLORREF border)
{
    HBRUSH hFill   = CreateSolidBrush(fill);
    HPEN   hPen    = CreatePen(PS_SOLID, 1, border);
    HBRUSH hOldB   = (HBRUSH)SelectObject(hdc, hFill);
    HPEN   hOldP   = (HPEN)SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(hdc, hOldB);
    SelectObject(hdc, hOldP);
    DeleteObject(hFill);
    DeleteObject(hPen);
}

// ─── Обработчик OwnerDraw кнопок ─────────────────────────────────────────────
void OnDrawItem(HWND hWnd, DRAWITEMSTRUCT* dis)
{
    HWND hCtrl = dis->hwndItem;
    RECT rc    = dis->rcItem;
    HDC  hdc   = dis->hDC;

    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool isStart  = (hCtrl == g_hStartBtn);
    bool isStop   = (hCtrl == g_hStopBtn);
    bool isLeft   = (hCtrl == g_hBtnLeft);
    bool isRight  = (hCtrl == g_hBtnRight);
    bool isMiddle = (hCtrl == g_hBtnMiddle);

    COLORREF fillClr   = CLR_SURFACE;
    COLORREF borderClr = CLR_BORDER;
    COLORREF textClr   = CLR_TEXT;
    bool selected = false;

    if (isStart)  { fillClr = CLR_GREEN;    borderClr = CLR_GREEN;   textClr = RGB(10,10,15); }
    if (isStop)   { fillClr = CLR_ACCENT2;  borderClr = CLR_ACCENT2; textClr = RGB(10,10,15); }

    if (isLeft   && g_selectedButton == 0) selected = true;
    if (isRight  && g_selectedButton == 1) selected = true;
    if (isMiddle && g_selectedButton == 2) selected = true;

    if (selected) { fillClr = CLR_ACCENT; borderClr = CLR_ACCENT; textClr = RGB(10,10,15); }

    if (pressed) {
        fillClr = RGB(
            (int)(GetRValue(fillClr) * 0.8),
            (int)(GetGValue(fillClr) * 0.8),
            (int)(GetBValue(fillClr) * 0.8)
        );
    }

    DrawRoundRect(hdc, rc, 8, fillClr, borderClr);

    // Текст
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textClr);

    wchar_t text[64] = {};
    GetWindowText(hCtrl, text, 64);

    HFONT hOldFont = (HFONT)SelectObject(hdc, g_hFontMed);
    DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

// ─── Создание элементов ───────────────────────────────────────────────────────
HWND CreateOwnerBtn(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id)
{
    return CreateWindowEx(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, GetModuleHandle(nullptr), nullptr);
}

void CreateControls(HWND hWnd)
{
    // ── Кнопка мыши ──────────────────────────────────────────────────────────
    g_hBtnLeft   = CreateOwnerBtn(hWnd, L"LEFT",   20,  80, 100, 40, IDC_BTN_LEFT);
    g_hBtnRight  = CreateOwnerBtn(hWnd, L"RIGHT", 130,  80, 100, 40, IDC_BTN_RIGHT);
    g_hBtnMiddle = CreateOwnerBtn(hWnd, L"MIDDLE",240,  80, 100, 40, IDC_BTN_MIDDLE);

    // ── CPS Slider ────────────────────────────────────────────────────────────
    g_hSlider = CreateWindowEx(0, TRACKBAR_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        20, 180, 310, 30, hWnd, (HMENU)IDC_CPS_SLIDER, GetModuleHandle(nullptr), nullptr);
    SendMessage(g_hSlider, TBM_SETRANGE,  TRUE, MAKELONG(1, 100));
    SendMessage(g_hSlider, TBM_SETPOS,    TRUE, g_cps);
    SendMessage(g_hSlider, TBM_SETPAGESIZE, 0, 5);

    // ── CPS Edit ──────────────────────────────────────────────────────────────
    g_hCpsEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"10",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
        340, 178, 60, 32, hWnd, (HMENU)IDC_CPS_EDIT, GetModuleHandle(nullptr), nullptr);
    SendMessage(g_hCpsEdit, EM_LIMITTEXT, 3, 0);
    SendMessage(g_hCpsEdit, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

    // ── Start / Stop ──────────────────────────────────────────────────────────
    g_hStartBtn = CreateOwnerBtn(hWnd, L"▶  СТАРТ",  20, 250, 180, 48, IDC_START_BTN);
    g_hStopBtn  = CreateOwnerBtn(hWnd, L"■  СТОП",  220, 250, 180, 48, IDC_STOP_BTN);
    EnableWindow(g_hStopBtn, FALSE);

    // ── Лейблы ───────────────────────────────────────────────────────────────
    g_hStatusLbl = CreateWindow(L"STATIC", L"○ ОСТАНОВЛЕН",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 318, 380, 24, hWnd, (HMENU)IDC_STATUS_LABEL, GetModuleHandle(nullptr), nullptr);

    g_hClicksLbl = CreateWindow(L"STATIC", L"Кликов: 0",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 348, 380, 20, hWnd, (HMENU)IDC_CLICKS_LABEL, GetModuleHandle(nullptr), nullptr);

    g_hCpsLbl = CreateWindow(L"STATIC", L"Горячая клавиша: F6 — вкл/выкл",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 374, 380, 18, hWnd, (HMENU)IDC_HOTKEY_LABEL, GetModuleHandle(nullptr), nullptr);

    // Шрифты для статик-контролов
    SendMessage(g_hStatusLbl, WM_SETFONT, (WPARAM)g_hFontBig,   TRUE);
    SendMessage(g_hClicksLbl, WM_SETFONT, (WPARAM)g_hFontMed,   TRUE);
    SendMessage(g_hCpsLbl,    WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);
}

// ─── Обновление CPS из разных источников ─────────────────────────────────────
void SyncCpsFromEdit()
{
    wchar_t buf[8] = {};
    GetWindowText(g_hCpsEdit, buf, 8);
    int v = _wtoi(buf);
    if (v < 1)   v = 1;
    if (v > 100) v = 100;
    g_cps = v;
    SendMessage(g_hSlider, TBM_SETPOS, TRUE, v);
}

void SyncCpsFromSlider()
{
    int v = (int)SendMessage(g_hSlider, TBM_GETPOS, 0, 0);
    g_cps = v;
    wchar_t buf[8];
    _itow_s(v, buf, 10);
    SetWindowText(g_hCpsEdit, buf);
}

// ─── WndProc ─────────────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hBgBrush   = CreateSolidBrush(CLR_BG);
        g_hSurfBrush = CreateSolidBrush(CLR_SURFACE);

        g_hFontBig   = CreateFont(24, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        g_hFontMed   = CreateFont(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        g_hFontSmall = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        g_hFontMono  = CreateFont(15, 0, 0, 0, FW_BOLD,   FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

        InitCommonControls();
        CreateControls(hWnd);

        // F6 как глобальный хоткей
        RegisterHotKey(hWnd, 1, 0, VK_F6);
        return 0;
    }

    case WM_HOTKEY:
        if (wParam == 1) {
            if (g_running.load()) stopClicking();
            else                  startClicking();
        }
        return 0;

    // ── Обновление счётчика из потока ────────────────────────────────────────
    case WM_USER + 1:
    {
        wchar_t buf[64];
        wsprintfW(buf, L"Кликов: %lld", g_clickCount.load());
        SetWindowText(g_hClicksLbl, buf);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_START_BTN:  startClicking(); break;
        case IDC_STOP_BTN:   stopClicking();  break;

        case IDC_BTN_LEFT:
            g_selectedButton = 0;
            InvalidateRect(g_hBtnLeft,   nullptr, TRUE);
            InvalidateRect(g_hBtnRight,  nullptr, TRUE);
            InvalidateRect(g_hBtnMiddle, nullptr, TRUE);
            break;
        case IDC_BTN_RIGHT:
            g_selectedButton = 1;
            InvalidateRect(g_hBtnLeft,   nullptr, TRUE);
            InvalidateRect(g_hBtnRight,  nullptr, TRUE);
            InvalidateRect(g_hBtnMiddle, nullptr, TRUE);
            break;
        case IDC_BTN_MIDDLE:
            g_selectedButton = 2;
            InvalidateRect(g_hBtnLeft,   nullptr, TRUE);
            InvalidateRect(g_hBtnRight,  nullptr, TRUE);
            InvalidateRect(g_hBtnMiddle, nullptr, TRUE);
            break;

        case IDC_CPS_EDIT:
            if (HIWORD(wParam) == EN_KILLFOCUS || HIWORD(wParam) == EN_CHANGE)
                SyncCpsFromEdit();
            break;
        }
        return 0;

    case WM_HSCROLL:
        if ((HWND)lParam == g_hSlider)
            SyncCpsFromSlider();
        return 0;

    // ── Тёмная тема: фон окна ─────────────────────────────────────────────────
    case WM_ERASEBKGND:
    {
        HDC   hdc = (HDC)wParam;
        RECT  rc;
        GetClientRect(hWnd, &rc);

        // Фон
        FillRect(hdc, &rc, g_hBgBrush);

        // Заголовочная плашка
        RECT header = {0, 0, rc.right, 60};
        FillRect(hdc, &header, g_hSurfBrush);

        // Линия под заголовком
        HPEN hPen = CreatePen(PS_SOLID, 1, CLR_BORDER);
        HPEN hOld = (HPEN)SelectObject(hdc, hPen);
        MoveToEx(hdc, 0, 60, nullptr);
        LineTo(hdc, rc.right, 60);
        SelectObject(hdc, hOld);
        DeleteObject(hPen);

        // Заголовок
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_ACCENT);
        HFONT hOldF = (HFONT)SelectObject(hdc, g_hFontBig);
        RECT  tRC   = {0, 0, rc.right, 60};
        DrawText(hdc, L"⚡ AUTO CLICKER", -1, &tRC, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldF);

        // Секция "Кнопка мыши"
        SetTextColor(hdc, CLR_SUBTEXT);
        HFONT hSF = (HFONT)SelectObject(hdc, g_hFontSmall);
        RECT secRC1 = {20, 65, 400, 85};
        DrawText(hdc, L"КНОПКА МЫШИ", -1, &secRC1, DT_LEFT | DT_TOP);

        // Секция "CPS"
        RECT secRC2 = {20, 148, 400, 168};
        DrawText(hdc, L"СКОРОСТЬ (CPS: 1–100)", -1, &secRC2, DT_LEFT | DT_TOP);

        // Секция "Управление"
        RECT secRC3 = {20, 232, 400, 250};
        DrawText(hdc, L"УПРАВЛЕНИЕ", -1, &secRC3, DT_LEFT | DT_TOP);
        SelectObject(hdc, hSF);

        // Разделитель перед статусом
        HPEN hPen2 = CreatePen(PS_SOLID, 1, CLR_BORDER);
        HPEN hOld2 = (HPEN)SelectObject(hdc, hPen2);
        MoveToEx(hdc, 20, 308, nullptr);
        LineTo(hdc, rc.right - 20, 308);
        SelectObject(hdc, hOld2);
        DeleteObject(hPen2);

        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    // ── Цвет фона дочерних контролов ─────────────────────────────────────────
    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        SetBkMode(hdcStatic, TRANSPARENT);
        HWND hCtrl = (HWND)lParam;

        if (hCtrl == g_hStatusLbl) {
            SetTextColor(hdcStatic, g_running.load() ? CLR_GREEN : CLR_ACCENT2);
        } else {
            SetTextColor(hdcStatic, CLR_SUBTEXT);
        }
        return (LRESULT)g_hBgBrush;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdcEdit = (HDC)wParam;
        SetBkColor(hdcEdit, CLR_SURFACE);
        SetTextColor(hdcEdit, CLR_ACCENT);
        return (LRESULT)g_hSurfBrush;
    }

    case WM_DRAWITEM:
        OnDrawItem(hWnd, (DRAWITEMSTRUCT*)lParam);
        return TRUE;

    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            SyncCpsFromEdit();
        }
        break;

    case WM_DESTROY:
        stopClicking();
        UnregisterHotKey(hWnd, 1);
        DeleteObject(g_hBgBrush);
        DeleteObject(g_hSurfBrush);
        DeleteObject(g_hFontBig);
        DeleteObject(g_hFontMed);
        DeleteObject(g_hFontSmall);
        DeleteObject(g_hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ─── WinMain ──────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    // Включаем DPI-aware для чёткого рендера
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc       = {};
    wc.cbSize           = sizeof(wc);
    wc.style            = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      = WndProc;
    wc.hInstance        = hInst;
    wc.hCursor          = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground    = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName    = L"AutoClickerWnd";
    wc.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm          = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    int winW = 420, winH = 420;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"AutoClickerWnd",
        L"AutoClicker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (scrW - winW) / 2, (scrH - winH) / 2,
        winW, winH,
        nullptr, nullptr, hInst, nullptr
    );

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
