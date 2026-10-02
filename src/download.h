#pragma once

#include <string>

namespace wslterm {

struct DownloadProgress {
    unsigned long long got;
    unsigned long long total;
    int                percent;
    unsigned long long speedBps;
    std::wstring       note;
};

typedef bool (*DownloadTickFn)(void* user, const DownloadProgress& p);

bool httpDownload(const std::wstring& url,
                  const std::wstring& destPath,
                  DownloadTickFn tick,
                  void* user,
                  std::wstring& err,
                  volatile long* cancel);

std::wstring formatBytes(unsigned long long n);
std::wstring formatSpeed(unsigned long long bps);

}
