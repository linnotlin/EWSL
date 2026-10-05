#include "src/wsl.h"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace wslterm;

static int g_fail = 0;

static void check(bool ok, const char* what) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_fail;
}

static std::wstring cfgPath() {
    WCHAR buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return std::wstring(buf) + L"\\.wslconfig";
}

static bool writeRaw(const std::wstring& p, const std::string& data) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    BOOL ok = WriteFile(f, data.empty() ? NULL : data.data(),
                        (DWORD)data.size(), &put, NULL);
    CloseHandle(f);
    return ok && put == data.size();
}

static std::string readRaw(const std::wstring& p) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    std::string buf;
    DWORD want = 65536;
    buf.resize(want);
    DWORD got = 0;
    ReadFile(f, &buf[0], want, &got, NULL);
    CloseHandle(f);
    buf.resize(got);
    return buf;
}

static bool has(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    std::wstring p = cfgPath();
    std::string orig = readRaw(p);

    // 1) 空文件 -> 新建 [wsl2] 段
    writeRaw(p, "");
    check(disableWslg(), "disableWslg on empty file succeeds");
    {
        std::string t = readRaw(p);
        check(has(t, "[wsl2]") && has(t, "guiApplications=false"),
              "empty file gains [wsl2] + guiApplications=false");
        check(wslgState() == WSLG_OFF, "wslgState reports OFF after disable");
    }

    // 2) 幂等：再调一次不该产生第二行
    {
        const size_t kLen = 14;                 // "guiApplication" 的长度
        size_t n1 = 0, n2 = 0;
        std::string t = readRaw(p);
        for (size_t i = 0; i + kLen <= t.size(); ++i)
            if (t.compare(i, kLen, "guiApplication") == 0) ++n1;
        disableWslg();
        t = readRaw(p);
        for (size_t i = 0; i + kLen <= t.size(); ++i)
            if (t.compare(i, kLen, "guiApplication") == 0) ++n2;
        check(n1 == 1 && n2 == 1, "disableWslg is idempotent (no duplicate key)");
    }

    // 3) 已有 [wsl2] 段 -> 插进段内，不新建第二个段
    writeRaw(p, "[wsl2]\nmemory=4GB\r\nprocessors=2\r\n");
    check(disableWslg(), "disableWslg into existing [wsl2] succeeds");
    {
        std::string t = readRaw(p);
        const size_t secLen = 6;               // "[wsl2]" 的长度
        size_t n = 0;
        for (size_t i = 0; i + secLen <= t.size(); ++i)
            if (t.compare(i, secLen, "[wsl2]") == 0) ++n;
        check(n == 1, "existing [wsl2] not duplicated");
        check(has(t, "memory=4GB") && has(t, "processors=2"),
              "pre-existing keys preserved");
        // 键必须落在 [wsl2] 之后、任何别的段之前
        check(t.find("guiApplications=false") > t.find("[wsl2]"),
              "key inserted after [wsl2]");
        check(t.find("guiApplications=false") < t.find("memory=4GB"),
              "key placed before following keys (still inside the section)");
        // 回归：段名后紧跟的换行必须被吃掉，否则插进去会粘成
        // "guiApplications=falsememory=4GB"，WSL 根本读不出这个键。
        // 行尾写成 CRLF，所以两种都认。
        check(t.find("guiApplications=false\r\n") != std::string::npos ||
              t.find("guiApplications=false\n")   != std::string::npos,
              "key ends its own line (no run-on into next key)");
    }

    // 4) 已有别的段在前面 -> 键要落进 [wsl2] 而不是文件末尾
    writeRaw(p, "[experimental]\r\nsparseVhd=true\r\n\r\n[wsl2]\r\nmemory=8GB\r\n");
    check(disableWslg(), "disableWslg with preceding section succeeds");
    {
        std::string t = readRaw(p);
        size_t w2 = t.find("[wsl2]");
        size_t key = t.find("guiApplications=false");
        check(key > w2, "key lands inside [wsl2], not at file end");
        check(has(t, "sparseVhd=true"), "earlier section untouched");
    }

    // 5) enableWslg 只删那一行，其他内容一字不动
    {
        std::string before = readRaw(p);
        check(enableWslg(), "enableWslg succeeds");
        std::string after = readRaw(p);
        check(!has(after, "guiApplications"), "guiApplications line removed");
        check(after.find("[wsl2]") != std::string::npos, "[wsl2] section kept");
        check(has(after, "memory=8GB") && has(after, "sparseVhd=true"),
              "other content intact after enable");
        (void)before;
    }

    // 6) enableWslg 对没有该键的文件是安全的
    writeRaw(p, "[wsl2]\nmemory=2GB\n");
    check(enableWslg(), "enableWslg on file without the key is a no-op");
    {
        std::string t = readRaw(p);
        check(t == "[wsl2]\nmemory=2GB\n", "unchanged file stays byte-identical");
    }

    // 7) 恢复真实文件
    if (orig.empty()) {
        DeleteFileW(p.c_str());
    } else {
        writeRaw(p, orig);
    }
    printf("\nrestored original .wslconfig (%d bytes)\n", (int)orig.size());

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
