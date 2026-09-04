#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <windowsx.h>

#include "debuglog.h"

class TrayIcon {
public:
    ~TrayIcon() {
        Uninitialize();
    }

    void Initialize(const HWND hWnd, HICON icon, UINT iconId, UINT windowMessage, HMENU hContextMenu) {
        if (m_added)
            return;

        m_hWnd = hWnd;
        m_iconId = iconId;
        m_iconMsg = windowMessage;
        m_menu = hContextMenu;

        NOTIFYICONDATA notifyIconData = {};
        notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
        notifyIconData.hWnd = hWnd;
        notifyIconData.uID = iconId;
        notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE;
        notifyIconData.hIcon = icon;
        notifyIconData.uCallbackMessage = m_iconMsg;

        bool result = Shell_NotifyIcon(NIM_ADD, &notifyIconData);
        if (!result)
            DebugLog(L"Adding tray icon failed.\r\n");

        notifyIconData.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, &notifyIconData);
        m_added = true;
    }

    void Update(HICON newIcon, const wchar_t* tooltip) {
        NOTIFYICONDATA notifyIconData = {};
        notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
        notifyIconData.hWnd = m_hWnd;
        notifyIconData.uID = m_iconId;
        notifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        notifyIconData.hIcon = newIcon;
        StringCchCopyW(notifyIconData.szTip, ARRAYSIZE(notifyIconData.szTip), tooltip);

        bool result = Shell_NotifyIcon(NIM_MODIFY, &notifyIconData);
        if (!result)
            DebugLog(L"Updating tray icon failed.\r\n");
    }

    void Uninitialize() {
        if (!m_added)
            return;

        NOTIFYICONDATA notifyIconData = {};
        notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
        notifyIconData.hWnd = m_hWnd;
        notifyIconData.uID = m_iconId;
        Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
        m_added = false;
    }

    bool HandleMessage(UINT uMsg, LPARAM lParam, WORD* event) {
        if (uMsg != m_iconMsg)
            return false;

        *event = LOWORD(lParam);
        return true;
    }

private:
    bool m_added = false;
    HWND m_hWnd = nullptr;
    UINT m_iconId = 0;
    UINT m_iconMsg = 0;
    HMENU m_menu = nullptr;
};
