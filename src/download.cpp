#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winhttp.h>

#include <cstdio>

#include "download.h"

#include "lang.h"

namespace wslterm {

namespace {

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port;
    bool secure;

    UrlParts() : port(0), secure(true) {}
};

bool crack(const std::wstring& url, UrlParts& out) {
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;

    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.port = uc.nPort;
    out.secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return true;
}

std::wstring winErr(const wchar_t* what) {
    DWORD e = GetLastError();
    wchar_t buf[320];
    swprintf(buf, 320, LS(L"%s 失败（错误码 %lu）"), what, (unsigned long)e);

    std::wstring s = buf;

    switch (e) {
    case 12007: s += LS(L"；域名解析失败，检查网络或代理"); break;
    case 12029: s += LS(L"；无法连接到下载服务器"); break;
    case 12002: s += LS(L"；连接超时，稍后重试"); break;
    case 12037: s += LS(L"；服务器证书无效"); break;
    case 12019: s += LS(L"；网络不可用"); break;
    default: break;
    }
    return s;
}

}

namespace {

std::wstring fmt1(double v) {
    bool neg = (v < 0.0);
    if (neg) v = -v;

    unsigned long long scaled = (unsigned long long)(v * 10.0 + 0.5);
    unsigned long long whole = scaled / 10ULL;
    unsigned long long frac = scaled % 10ULL;

    wchar_t buf[32];
    if (neg) {
        swprintf(buf, 32, L"-%llu.%llu", whole, frac);
    } else {
        swprintf(buf, 32, L"%llu.%llu", whole, frac);
    }
    return buf;
}

std::wstring fmt2(double v) {
    unsigned long long scaled = (unsigned long long)(v * 100.0 + 0.5);
    unsigned long long whole = scaled / 100ULL;
    unsigned long long frac = scaled % 100ULL;

    wchar_t buf[32];
    swprintf(buf, 32, L"%llu.%02llu", whole, frac);
    return buf;
}

}

std::wstring formatBytes(unsigned long long n) {
    if (n >= 1024ULL * 1024ULL * 1024ULL) {
        return fmt2((double)n / (1024.0 * 1024.0 * 1024.0)) + L" GB";
    }
    if (n >= 1024ULL * 1024ULL) {
        return fmt1((double)n / (1024.0 * 1024.0)) + L" MB";
    }
    if (n >= 1024ULL) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%llu", n / 1024ULL);
        return std::wstring(buf) + L" KB";
    }
    wchar_t buf[32];
    swprintf(buf, 32, L"%llu", n);
    return std::wstring(buf) + L" B";
}

std::wstring formatSpeed(unsigned long long bps) {
    return formatBytes(bps) + L"/s";
}

bool httpDownload(const std::wstring& url,
                  const std::wstring& destPath,
                  DownloadTickFn tick,
                  void* user,
                  std::wstring& err,
                  volatile long* cancel) {
    UrlParts up;
    if (!crack(url, up)) {
        err = LS(L"无法解析下载地址：") + url;
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"EWSL/1.0",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        err = winErr(LS(L"初始化 WinHTTP"));
        return false;
    }

    DWORD timeout = 30000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

    bool ok = false;
    HINTERNET hConn = NULL;
    HINTERNET hReq = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    std::wstring redirect;

    do {
        hConn = WinHttpConnect(hSession, up.host.c_str(), up.port, 0);
        if (!hConn) { err = winErr(LS(L"连接下载服务器")); break; }

        DWORD flags = up.secure ? WINHTTP_FLAG_SECURE : 0;
        hReq = WinHttpOpenRequest(hConn, L"GET", up.path.c_str(),
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hReq) { err = winErr(LS(L"创建 HTTP 请求")); break; }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirectPolicy, sizeof(redirectPolicy));

        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            err = winErr(LS(L"发送 HTTP 请求"));
            break;
        }
        if (!WinHttpReceiveResponse(hReq, NULL)) { err = winErr(LS(L"读取 HTTP 响应")); break; }

        DWORD status = 0;
        DWORD slen = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen,
                            WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            wchar_t buf[160];
            wsprintfW(buf, LS(L"服务器返回 HTTP %lu"), (unsigned long)status);
            err = buf;
            break;
        }

        unsigned long long total = 0;
        {
            wchar_t lenBuf[64];
            DWORD lenSize = sizeof(lenBuf);
            if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, lenBuf,
                                    &lenSize, WINHTTP_NO_HEADER_INDEX)) {
                lenBuf[lenSize / sizeof(wchar_t)] = 0;
                total = _wtoi64(lenBuf);
            }
        }

        hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            err = winErr(LS(L"创建本地文件"));
            break;
        }

        unsigned long long got = 0;
        DWORD  tickBase = GetTickCount();
        unsigned long long speedBase = 0;
        char buf[65536];

        for (;;) {
            if (cancel && InterlockedCompareExchange(cancel, 0, 0) != 0) {
                err = LS(L"已取消");
                break;
            }

            DWORD n = 0;
            if (!WinHttpReadData(hReq, buf, (DWORD)sizeof(buf), &n)) {
                err = winErr(LS(L"读取数据"));
                break;
            }
            if (n == 0) { ok = true; break; }

            DWORD w = 0;
            if (!WriteFile(hFile, buf, n, &w, NULL) || w != n) {
                err = LS(L"写入磁盘失败（空间不足？）");
                break;
            }

            got += n;

            DWORD now = GetTickCount();
            if (tick && (now - tickBase >= 120)) {
                double secs = (now - tickBase) / 1000.0;
                unsigned long long speed = secs > 0.0
                    ? (unsigned long long)((got - speedBase) / secs) : 0;

                DownloadProgress p;
                p.got = got;
                p.total = total;
                p.percent = (total > 0) ? (int)((got * 100) / total) : -1;
                p.speedBps = speed;
                p.note.clear();

                tickBase = now;
                speedBase = got;

                if (!tick(user, p)) { err = LS(L"已取消"); break; }
            }
        }

        if (ok && tick) {
            DownloadProgress p;
            p.got = got;
            p.total = total;
            p.percent = 100;
            p.speedBps = 0;
            p.note.clear();
            tick(user, p);
        }
    } while (false);

    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        if (!ok) DeleteFileW(destPath.c_str());
    }
    if (hReq) WinHttpCloseHandle(hReq);
    if (hConn) WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);

    return ok;
}

}
