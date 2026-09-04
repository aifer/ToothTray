// ToothTray.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "ToothTray.h"
#include <memory>
#include <winrt/base.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <mutex>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

#include "debuglog.h"
#include "BluetoothAudioDevices.h"
#include "TrayIcon.h"
#include "ToothTrayMenu.h"

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
constexpr UINT WM_TRAYICON = WM_APP;
constexpr UINT WM_BATTERY_RESULT = WM_APP + 1;

BluetoothAudioDeviceEnumerator bluetoothAudioDeviceEmumerator;
ToothTrayMenu trayMenu;
TrayIcon trayIcon;
std::atomic_bool batteryReadInProgress = false;
std::mutex batteryResultMutex;
int pendingBatteryLevel = -1;
std::wstring pendingBatteryTime;


static std::wstring BatteryCachePath() { wchar_t path[MAX_PATH] = {}; GetModuleFileNameW(nullptr, path, ARRAYSIZE(path)); std::wstring result(path); size_t slash = result.find_last_of(L"\\/"); return slash == std::wstring::npos ? L"last_battery.txt" : result.substr(0, slash + 1) + L"last_battery.txt"; }

static HICON CreateBatteryIcon(const std::wstring& text) {
    constexpr int size = 32; BITMAPV5HEADER info = {}; info.bV5Size = sizeof(info); info.bV5Width = size; info.bV5Height = -size; info.bV5Planes = 1; info.bV5BitCount = 32; info.bV5Compression = BI_BITFIELDS; info.bV5RedMask = 0x00ff0000; info.bV5GreenMask = 0x0000ff00; info.bV5BlueMask = 0x000000ff; info.bV5AlphaMask = 0xff000000;
    void* rawPixels = nullptr; HDC screen = GetDC(nullptr); HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &rawPixels, nullptr, 0); ReleaseDC(nullptr, screen); if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
    ZeroMemory(rawPixels, size * size * sizeof(DWORD));
    HDC dc = CreateCompatibleDC(nullptr); HGDIOBJ oldBitmap = SelectObject(dc, color); HFONT font = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); HGDIOBJ oldFont = SelectObject(dc, font); SetTextColor(dc, RGB(0, 120, 215)); SetBkMode(dc, TRANSPARENT); RECT rect = { 0, 0, size, size }; DrawTextW(dc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); SelectObject(dc, oldFont); DeleteObject(font); SelectObject(dc, oldBitmap); DeleteDC(dc);
    DWORD* pixels = static_cast<DWORD*>(rawPixels); for (int i = 0; i < size * size; ++i) if ((pixels[i] & 0x00ffffff) != 0) pixels[i] |= 0xff000000;
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr); HDC maskDc = CreateCompatibleDC(nullptr); HGDIOBJ oldMask = SelectObject(maskDc, mask); PatBlt(maskDc, 0, 0, size, size, BLACKNESS); SelectObject(maskDc, oldMask); DeleteDC(maskDc);
    ICONINFO iconInfo = {}; iconInfo.fIcon = TRUE; iconInfo.hbmMask = mask; iconInfo.hbmColor = color; HICON icon = CreateIconIndirect(&iconInfo); DeleteObject(mask); DeleteObject(color); return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

static int QueryBattery(HANDLE handle) { for (int attempt = 0; attempt < 4; ++attempt) { BYTE request[65] = {}; request[1] = 0x10; request[2] = 4; request[3] = 7; request[4] = 0x80; if (!HidD_SetFeature(handle, request, sizeof(request))) continue; for (int poll = 0; poll < 10; ++poll) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); BYTE response[65] = {}; if (HidD_GetFeature(handle, response, sizeof(response)) && response[1] == 0x12 && response[2] == 4 && response[3] == 7 && response[4] == 0x80 && response[8] <= 100) return response[8]; } } return -1; }

static int ReadBatteryOnce() {
    GUID guid = {}; HidD_GetHidGuid(&guid); HDEVINFO deviceSet = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE); if (deviceSet == INVALID_HANDLE_VALUE) return -1; int level = -1;
    for (DWORD index = 0; ; ++index) { SP_DEVICE_INTERFACE_DATA interfaceData = { sizeof(interfaceData) }; if (!SetupDiEnumDeviceInterfaces(deviceSet, nullptr, &guid, index, &interfaceData)) break; DWORD bytes = 0; SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, nullptr, 0, &bytes, nullptr); std::vector<BYTE> buffer(bytes); SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data()); detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W); if (!SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, detail, bytes, nullptr, nullptr)) continue; HANDLE handle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr); if (handle == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attributes = { sizeof(attributes) }; if (HidD_GetAttributes(handle, &attributes) && attributes.VendorID == 0x260d && attributes.ProductID == 0x0042) { PHIDP_PREPARSED_DATA preparsed = nullptr; HIDP_CAPS caps = {}; if (HidD_GetPreparsedData(handle, &preparsed)) { HidP_GetCaps(preparsed, &caps); HidD_FreePreparsedData(preparsed); if (caps.FeatureReportByteLength == 65) level = QueryBattery(handle); } } CloseHandle(handle); if (level >= 0) break; }
    SetupDiDestroyDeviceInfoList(deviceSet); return level;
}
static int ReadBattery() { for (int attempt = 0; attempt < 12; ++attempt) { int level = ReadBatteryOnce(); if (level >= 0) return level; std::this_thread::sleep_for(std::chrono::seconds(1)); } return -1; }
static std::wstring TimeText() { SYSTEMTIME now = {}; GetLocalTime(&now); wchar_t text[32] = {}; StringCchPrintfW(text, ARRAYSIZE(text), L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond); return text; }
static bool LoadBatteryCache(int& level, std::wstring& time) { std::wifstream input(BatteryCachePath()); std::wstring number; std::getline(input, number); std::getline(input, time); level = _wtoi(number.c_str()); return level >= 0 && level <= 100 && !time.empty(); }
static void SaveBatteryCache(int level, const std::wstring& time) { std::wofstream output(BatteryCachePath(), std::ios::trunc); output << level << L"\n" << time; }
static void ShowBattery(int level, const std::wstring& time, bool cached, const wchar_t* suffix) { wchar_t tip[128] = {}; StringCchPrintfW(tip, ARRAYSIZE(tip), L"EK75 电量：%d%%%s\n最后成功刷新：%s%s", level, cached ? L"（缓存）" : L"", time.c_str(), suffix ? suffix : L""); HICON icon = CreateBatteryIcon(level < 10 ? L"0" + std::to_wstring(level) : std::to_wstring(level)); trayIcon.Update(icon, tip); DestroyIcon(icon); }
static void ApplyBatteryResult() {
    int level = -1; std::wstring time;
    { std::lock_guard<std::mutex> lock(batteryResultMutex); level = pendingBatteryLevel; time = pendingBatteryTime; }
    if (level >= 0) { SaveBatteryCache(level, time); ShowBattery(level, time, false, nullptr); return; }
    if (LoadBatteryCache(level, time)) ShowBattery(level, time, true, L"\n查询失败，显示缓存");
    else { HICON icon = CreateBatteryIcon(L"!"); trayIcon.Update(icon, L"EK75 电量：暂时无法读取"); DestroyIcon(icon); }
}
static void RefreshBatteryAsync(HWND window) {
    if (batteryReadInProgress.exchange(true)) return;
    std::thread([window]() {
        int level = ReadBattery(); std::wstring time = level >= 0 ? TimeText() : L"";
        { std::lock_guard<std::mutex> lock(batteryResultMutex); pendingBatteryLevel = level; pendingBatteryTime = time; }
        batteryReadInProgress = false; PostMessageW(window, WM_BATTERY_RESULT, 0, 0);
    }).detach();
}
static void ShowStartupBattery() {
    int level = -1; std::wstring time;
    if (LoadBatteryCache(level, time)) ShowBattery(level, time, true, L"\n后台刷新中");
    else { HICON icon = CreateBatteryIcon(L"--"); trayIcon.Update(icon, L"EK75 电量：正在后台读取"); DestroyIcon(icon); }
}
static void ShowExitMenu(HWND window) { POINT point = {}; GetCursorPos(&point); HMENU menu = CreatePopupMenu(); AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit"); SetForegroundWindow(window); TrackPopupMenuEx(menu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, point.x, point.y, window, nullptr); DestroyMenu(menu); }

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

   HICON hIcon = CreateBatteryIcon(L"--");
   trayIcon.Initialize(hWnd, hIcon, 0, WM_TRAYICON, NULL);
   DestroyIcon(hIcon);
   ShowStartupBattery();
   RefreshBatteryAsync(hWnd);
   SetTimer(hWnd, 1, 300000, nullptr);
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
    case WM_TIMER:
        if (wParam == 1) RefreshBatteryAsync(hWnd);
        break;
    case WM_BATTERY_RESULT:
        ApplyBatteryResult();
        break;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    default:
        WORD event;
        if (trayIcon.HandleMessage(message, lParam, &event)) {
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                ShowExitMenu(hWnd);
                break;
            }
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
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
