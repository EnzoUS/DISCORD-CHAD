
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <atomic>
#include <fstream>
#include <commctrl.h>
#include <shlwapi.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
HWND hTokenEdit, hChannelsEdit, hPath1Edit, hPath2Edit, hIntervalEdit, hTextEdit, hLogList, hStartBtn;
std::atomic<bool> g_Running{ false };
HANDLE hSpamThread = NULL;
std::vector<BYTE> ReadFileBytes(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    if (file.read((char*)buffer.data(), size))
        return buffer;
    return {};
}

std::string GenBoundary() {
    return "----DiscordSpammerBoundary" + std::to_string(GetTickCount64());
}
std::string WStrToStr(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
std::vector<BYTE> BuildMultipart(
    const std::string& boundary,
    const std::vector<BYTE>& img1, const std::string& fname1,
    const std::vector<BYTE>& img2, const std::string& fname2,
    const std::string& content = "")
{
    std::string end = "\r\n";
    std::string dash = "--";
    std::vector<BYTE> data;
    auto add = [&](const std::string& s) { data.insert(data.end(), s.begin(), s.end()); };
    auto addb = [&](const std::vector<BYTE>& b) { data.insert(data.end(), b.begin(), b.end()); };
    add(dash + boundary + end);
    add("Content-Disposition: form-data; name=\"file1\"; filename=\"" + fname1 + "\"" + end);
    add("Content-Type: application/octet-stream" + end + end);
    addb(img1);
    add(end);
    add(dash + boundary + end);
    add("Content-Disposition: form-data; name=\"file2\"; filename=\"" + fname2 + "\"" + end);
    add("Content-Type: application/octet-stream" + end + end);
    addb(img2);
    add(end);
    if (!content.empty()) {
        add(dash + boundary + end);
        add("Content-Disposition: form-data; name=\"content\"" + end + end);
        add(content + end);
    }

    add(dash + boundary + dash + end);
    return data;
}
void AddLog(const std::wstring& msg) {
    if (hLogList) {
        SendMessage(hLogList, LB_ADDSTRING, 0, (LPARAM)msg.c_str());
        int cnt = SendMessage(hLogList, LB_GETCOUNT, 0, 0);
        SendMessage(hLogList, LB_SETTOPINDEX, cnt - 1, 0);
    }
}
DWORD WINAPI SpamProc(LPVOID) {
    g_Running = true;
    EnableWindow(hStartBtn, FALSE);
    WCHAR token[2048], channels[1024], path1[260], path2[260], intervalStr[10], customText[4096];
    GetWindowText(hTokenEdit, token, 2048);
    GetWindowText(hChannelsEdit, channels, 1024);
    GetWindowText(hPath1Edit, path1, 260);
    GetWindowText(hPath2Edit, path2, 260);
    GetWindowText(hIntervalEdit, intervalStr, 10);
    GetWindowText(hTextEdit, customText, 4096);
    int interval = _wtoi(intervalStr);
    if (interval < 1) interval = 1;
    if (wcslen(token) == 0 || wcslen(channels) == 0) {
        AddLog(L"Error: Token or channels empty.");
        g_Running = false;
        EnableWindow(hStartBtn, TRUE);
        return 0;
    }
    std::wstring chStr(channels);
    std::wistringstream wiss(chStr);
    std::vector<std::wstring> channelIds;
    std::wstring id;
    while (wiss >> id) channelIds.push_back(id);
    std::vector<BYTE> img1 = ReadFileBytes(path1);
    std::vector<BYTE> img2 = ReadFileBytes(path2);
    if (img1.empty() || img2.empty()) {
        AddLog(L"Error: Failed to load one or both images.");
        g_Running = false;
        EnableWindow(hStartBtn, TRUE);
        return 0;
    }
    std::wstring wpath1(path1), wpath2(path2);
    std::wstring wfname1 = wpath1.substr(wpath1.find_last_of(L"\\/") + 1);
    std::wstring wfname2 = wpath2.substr(wpath2.find_last_of(L"\\/") + 1);
    std::string fname1 = WStrToStr(wfname1);
    std::string fname2 = WStrToStr(wfname2);
    std::string content = WStrToStr(customText);
    std::wstring tokenW(token);
    HINTERNET hSession = WinHttpOpen(L"DiscordSpammer/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { AddLog(L"WinHttpOpen failed"); g_Running = false; EnableWindow(hStartBtn, TRUE); return 0; }
    while (g_Running) {
        for (const auto& chId : channelIds) {
            std::string boundary = GenBoundary();
            std::vector<BYTE> payload = BuildMultipart(boundary, img1, fname1, img2, fname2, content);

            std::wstring path = L"/api/v9/channels/" + chId + L"/messages";
            HINTERNET hConnect = WinHttpConnect(hSession, L"discord.com", 443, 0);
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
            if (!hRequest) { WinHttpCloseHandle(hConnect); continue; }
            std::wstring authHdr = L"Authorization: " + tokenW;
            std::wstring cType = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end());
            WinHttpAddRequestHeaders(hRequest, authHdr.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
            WinHttpAddRequestHeaders(hRequest, cType.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
            BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                payload.data(), (DWORD)payload.size(), (DWORD)payload.size(), 0);
            if (sent) {
                WinHttpReceiveResponse(hRequest, NULL);
                DWORD status = 0; DWORD sz = sizeof(status);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    NULL, &status, &sz, NULL);
                AddLog(L"Sent to " + chId + (status == 200 ? L" OK" : L" FAIL (" + std::to_wstring(status) + L")"));
            }
            else {
                AddLog(L"Send failed for " + chId);
            }
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
        }
        if (g_Running) Sleep(interval * 1000);
    }

    WinHttpCloseHandle(hSession);
    AddLog(L"Spam stopped.");
    EnableWindow(hStartBtn, TRUE);
    return 0;
}

void BrowseFile(HWND edit, const wchar_t* filter) {
    OPENFILENAME ofn = { sizeof(ofn) };
    WCHAR file[260] = { 0 };
    ofn.hwndOwner = GetParent(edit);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 260;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileName(&ofn))
        SetWindowText(edit, file);
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindow(L"STATIC", L"Discord Token:", WS_CHILD | WS_VISIBLE, 10, 10, 120, 20, hWnd, NULL, NULL, NULL);
        hTokenEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 140, 8, 550, 20, hWnd, (HMENU)100, NULL, NULL);
        SendMessage(hTokenEdit, EM_SETLIMITTEXT, 0, 0);
        CreateWindow(L"STATIC", L"Channel IDs (space):", WS_CHILD | WS_VISIBLE, 10, 40, 120, 20, hWnd, NULL, NULL, NULL);
        hChannelsEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 140, 38, 550, 20, hWnd, (HMENU)101, NULL, NULL);
        CreateWindow(L"STATIC", L"Image 1:", WS_CHILD | WS_VISIBLE, 10, 70, 80, 20, hWnd, NULL, NULL, NULL);
        hPath1Edit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, 68, 400, 20, hWnd, (HMENU)102, NULL, NULL);
        CreateWindow(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 510, 68, 80, 20, hWnd, (HMENU)200, NULL, NULL);
        CreateWindow(L"STATIC", L"Image 2:", WS_CHILD | WS_VISIBLE, 10, 100, 80, 20, hWnd, NULL, NULL, NULL);
        hPath2Edit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 100, 98, 400, 20, hWnd, (HMENU)103, NULL, NULL);
        CreateWindow(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 510, 98, 80, 20, hWnd, (HMENU)201, NULL, NULL);
        CreateWindow(L"STATIC", L"Interval (sec):", WS_CHILD | WS_VISIBLE, 10, 130, 80, 20, hWnd, NULL, NULL, NULL);
        hIntervalEdit = CreateWindow(L"EDIT", L"5", WS_CHILD | WS_VISIBLE | WS_BORDER, 100, 128, 50, 20, hWnd, (HMENU)104, NULL, NULL);
        CreateWindow(L"STATIC", L"Text/Link:", WS_CHILD | WS_VISIBLE, 10, 160, 80, 20, hWnd, NULL, NULL, NULL);
        hTextEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_MULTILINE, 100, 158, 550, 40, hWnd, (HMENU)105, NULL, NULL);
        SendMessage(hTextEdit, EM_SETLIMITTEXT, 0, 0);
        hStartBtn = CreateWindow(L"BUTTON", L"Start Spam", WS_CHILD | WS_VISIBLE, 10, 210, 120, 25, hWnd, (HMENU)300, NULL, NULL);
        CreateWindow(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE, 140, 210, 120, 25, hWnd, (HMENU)301, NULL, NULL);
        hLogList = CreateWindow(L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            10, 250, 700, 120, hWnd, (HMENU)302, NULL, NULL);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 200) BrowseFile(hPath1Edit, L"Images (*.png;*.jpg;*.gif)\0*.png;*.jpg;*.gif\0All\0*.*\0");
        if (id == 201) BrowseFile(hPath2Edit, L"Images (*.png;*.jpg;*.gif)\0*.png;*.jpg;*.gif\0All\0*.*\0");
        if (id == 300) {
            if (!g_Running) {
                DWORD tid;
                hSpamThread = CreateThread(NULL, 0, SpamProc, NULL, 0, &tid);
            }
        }
        if (id == 301) {
            if (g_Running) {
                g_Running = false;
                AddLog(L"Stopping...");
            }
        }
        break;
    }
    case WM_CLOSE:
        g_Running = false;
        if (hSpamThread) WaitForSingleObject(hSpamThread, 3000);
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DISCORDSPAMGUI";
    RegisterClass(&wc);
    HWND hWnd = CreateWindow(wc.lpszClassName, L"https://github.com/EnzoUS", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 750, 430, NULL, NULL, hInstance, NULL);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}