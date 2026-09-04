// ToothTray.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "ToothTray.h"
#include <memory>
#include <winrt/base.h>

#include "debuglog.h"
#include "BluetoothAudioDevices.h"
#include "TrayIcon.h"
#include "ToothTrayMenu.h"

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
constexpr UINT WM_TRAYICON = WM_APP;

BluetoothAudioDeviceEnumerator bluetoothAudioDeviceEmumerator;
ToothTrayMenu trayMenu;
TrayIcon trayIcon;

static HICON CreateHeadphonesIcon()
{
    constexpr int iconSize = 32;

    BITMAPV5HEADER bitmapInfo = {};
    bitmapInfo.bV5Size = sizeof(bitmapInfo);
    bitmapInfo.bV5Width = iconSize;
    bitmapInfo.bV5Height = -iconSize;
    bitmapInfo.bV5Planes = 1;
    bitmapInfo.bV5BitCount = 32;
    bitmapInfo.bV5Compression = BI_BITFIELDS;
    bitmapInfo.bV5RedMask = 0x00ff0000;
    bitmapInfo.bV5GreenMask = 0x0000ff00;
    bitmapInfo.bV5BlueMask = 0x000000ff;
    bitmapInfo.bV5AlphaMask = 0xff000000;

    void* rawPixels = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bitmapInfo),
        DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    ReleaseDC(nullptr, screen);

    if (!colorBitmap)
        return LoadIconW(nullptr, IDI_APPLICATION);

    HDC canvas = CreateCompatibleDC(nullptr);
    HGDIOBJ oldBitmap = SelectObject(canvas, colorBitmap);
    const COLORREF iconColor = RGB(39, 148, 255);
    HPEN pen = CreatePen(PS_SOLID, 3, iconColor);
    HBRUSH brush = CreateSolidBrush(iconColor);
    HGDIOBJ oldPen = SelectObject(canvas, pen);
    HGDIOBJ oldBrush = SelectObject(canvas, brush);

    SetBkMode(canvas, TRANSPARENT);
    Arc(canvas, 5, 3, 27, 29, 5, 18, 27, 18);
    RoundRect(canvas, 4, 16, 10, 28, 3, 3);
    RoundRect(canvas, 22, 16, 28, 28, 3, 3);

    SelectObject(canvas, oldBrush);
    SelectObject(canvas, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
    SelectObject(canvas, oldBitmap);
    DeleteDC(canvas);

    DWORD* pixels = static_cast<DWORD*>(rawPixels);
    for (int i = 0; i < iconSize * iconSize; ++i) {
        if ((pixels[i] & 0x00ffffff) != 0)
            pixels[i] |= 0xff000000;
    }

    HBITMAP maskBitmap = CreateBitmap(iconSize, iconSize, 1, 1, nullptr);
    HDC maskCanvas = CreateCompatibleDC(nullptr);
    HGDIOBJ oldMask = SelectObject(maskCanvas, maskBitmap);
    PatBlt(maskCanvas, 0, 0, iconSize, iconSize, BLACKNESS);
    SelectObject(maskCanvas, oldMask);
    DeleteDC(maskCanvas);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);

    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);

    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    winrt::init_apartment();

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TOOTHTRAY, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TOOTHTRAY));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_TOOTHTRAY);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance;

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   HICON hIcon = CreateHeadphonesIcon();
   trayIcon.Initialize(hWnd, hIcon, 0, WM_TRAYICON, NULL);

   UpdateWindow(hWnd);
   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int commandId = LOWORD(wParam);
            switch (commandId)
            {
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                if (trayMenu.TryHandleCommand(commandId))
                    break;

                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        WORD event;
        if (trayIcon.HandleMessage(message, lParam, &event)) {
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                DestroyWindow(hWnd);
                break;
            }

            if (event == NIN_SELECT || event == NIN_KEYSELECT) {
                std::vector<BluetoothConnector> connectors = bluetoothAudioDeviceEmumerator.EnumerateAudioDevices();
                trayMenu.BuildMenu(connectors);
                trayMenu.ShowPopupMenu(hWnd, wParam);
            }
            break;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
