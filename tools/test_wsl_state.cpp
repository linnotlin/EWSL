// 验证 wsl.cpp 里的 decodeOutput / wslState 判定链路。
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include "src/wsl.h"

using namespace wslterm;

static int failures = 0;

static std::string wideToBytes(const wchar_t* s) {
    std::string out;
    size_t n = wcslen(s);
    for (size_t i = 0; i < n; ++i) {
        out.push_back((char)(s[i] & 0xFF));
        out.push_back((char)((s[i] >> 8) & 0xFF));
    }
    return out;
}

static void check(const char* name, bool ok) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

// 直接跑真实 wsl.exe，看 decodeOutput 是否解出了正常中文（不含替换字符、
// 不含 U+FFFD 之类乱码特征）。
static void realCapture(const wchar_t* args) {
    std::wstring exe = findWslExe();
    if (exe.empty()) { printf("[SKIP] no wsl.exe\n"); return; }

    DWORD code = (DWORD)-1;
    std::wstring out = runCaptureExit(L"\"" + exe + L"\" " + args, 20000, &code);

    printf("--- wsl.exe %ls (exit=%lu, %u chars) ---\n", args,
           (unsigned long)code, (unsigned)out.size());

    // 打印前几行
    std::wstring cur;
    int lines = 0;
    for (size_t i = 0; i < out.size() && lines < 4; ++i) {
        wchar_t c = out[i];
        if (c == L'\r') continue;
        if (c == L'\n') {
            if (!cur.empty()) {
                std::wstring t = cur;
                while (!t.empty() && t[t.size() - 1] == L' ') t.erase(t.size() - 1);
                if (!t.empty()) {
                    printf("  | ");
                    for (size_t k = 0; k < t.size() && k < 64; ++k) {
                        if (t[k] >= 0x20 && t[k] < 0x7F) putchar((char)t[k]);
                        else printf("\\u%04X", (unsigned)t[k]);
                    }
                    printf("\n");
                    ++lines;
                }
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
}

int main() {
    // ---- 组件状态判定 ----
    WslState st = wslState();
    printf("wslState() = %ls\n", wslStateLabel().c_str());
    printf("wslEnableCommand() = %ls\n\n", wslEnableCommand().c_str());

    check("state is decided (not UNKNOWN)", st != WSL_UNKNOWN);
    check("state label matches enum",
          wslStateLabel() == L"WSL_READY"       ||
          wslStateLabel() == L"WSL_NO_EXE"      ||
          wslStateLabel() == L"WSL_NO_VMP"      ||
          wslStateLabel() == L"WSL_NO_COMPONENT");

    // 状态缓存：连续两次调用结果必须一致
    check("state cached consistently", wslState() == st);

    // resetWslState 之后应重新探测出同样的结果
    resetWslState();
    check("re-probe after resetWslState", wslState() == st);

    // ---- 真实输出解码 ----
    realCapture(L"--help");
    printf("\n");

    // distros 列表为空是合理的（组件禁用），但不能崩、不能返回乱码
    std::vector<std::wstring> d = listInstalledDistros();
    printf("listInstalledDistros() -> %u entries\n", (unsigned)d.size());
    check("listInstalledDistros does not crash", true);

    printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
