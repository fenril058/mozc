// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "config/stats_config_util.h"

#ifdef _WIN32
#include <lmcons.h>
#include <atlbase.h>
#include <sddl.h>
#include <shlobj.h>
#include <time.h>
#include <windows.h>
#else  // _WIN32
#include <pwd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#endif  // _WIN32

#include <cstdint>
#ifdef __APPLE__
#include <fstream>
#include <string>

#include "base/mac/mac_util.h"
#endif  // __APPLE__

#if defined(__ANDROID__)
#include "config/config_handler.h"
#include "protocol/config.pb.h"
#endif  // __ANDROID__

#include "base/file_util.h"
#include "base/singleton.h"
#include "base/system_util.h"
#include "absl/synchronization/mutex.h"

namespace mozc {
namespace config {
namespace {

#ifdef GOOGLE_JAPANESE_INPUT_BUILD
#ifdef _WIN32

const wchar_t kOmahaGUID[] = L"{DDCCD2A9-025E-4142-BCEB-F467B88CF830}";
const wchar_t kOmahaUsageKey[] =
    L"Software\\Google\\Update\\ClientState\\"
    L"{DDCCD2A9-025E-4142-BCEB-F467B88CF830}";
const wchar_t kOmahaUsageKeyForEveryone[] =
    L"Software\\Google\\Update\\ClientStateMedium\\"
    L"{DDCCD2A9-025E-4142-BCEB-F467B88CF830}";

const wchar_t kSendStatsName[] = L"usagestats";

class WinStatsConfigUtilImpl : public StatsConfigUtilInterface {
 public:
  WinStatsConfigUtilImpl() {}
  WinStatsConfigUtilImpl(const WinStatsConfigUtilImpl &) = delete;
  WinStatsConfigUtilImpl &operator=(const WinStatsConfigUtilImpl &) = delete;
  virtual ~WinStatsConfigUtilImpl() {}
  virtual bool IsEnabled();
  virtual bool SetEnabled(bool val);
};

bool WinStatsConfigUtilImpl::IsEnabled() {
#ifdef CHANNEL_DEV
  return true;
#else   // CHANNEL_DEV
  const REGSAM sam_desired =
      KEY_QUERY_VALUE | (SystemUtil::IsWindowsX64() ? KEY_WOW64_32KEY : 0);
  // Like the crash handler, check the "ClientStateMedium" key first.
  // Then we check "ClientState" key.
  {
    CRegKey key_medium;
    LONG result = key_medium.Open(HKEY_LOCAL_MACHINE, kOmahaUsageKeyForEveryone,
                                  sam_desired);
    if (result == ERROR_SUCCESS && key_medium) {
      DWORD value = 0;
      result = key_medium.QueryDWORDValue(kSendStatsName, value);
      if (result == ERROR_SUCCESS) {
        return (value != 0);
      }
    }
  }  // Close |key_medium|

  CRegKey key;
  LONG result = key.Open(HKEY_LOCAL_MACHINE, kOmahaUsageKey, sam_desired);
  if (result != ERROR_SUCCESS || !key) {
    return false;
  }
  DWORD value = 0;
  result = key.QueryDWORDValue(kSendStatsName, value);
  if (result != ERROR_SUCCESS) {
    return false;
  }
  return (value != 0);
#endif  // CHANNEL_DEV
}

bool WinStatsConfigUtilImpl::SetEnabled(bool val) {
#ifdef CHANNEL_DEV
  // On Dev channel, usage stats and crash report should be always sent.
  val = true;
  // We always returns true in DevChannel.
  constexpr bool kReturnCodeInError = true;
#else   // CHANNEL_DEV
  constexpr bool kReturnCodeInError = false;
#endif  // CHANNEL_DEV

  CRegKey key;
  const REGSAM sam_desired =
      KEY_WRITE | (SystemUtil::IsWindowsX64() ? KEY_WOW64_32KEY : 0);
  LONG result = key.Create(HKEY_LOCAL_MACHINE, kOmahaUsageKey, REG_NONE,
                           REG_OPTION_NON_VOLATILE, sam_desired);
  if (ERROR_SUCCESS != result) {
    return kReturnCodeInError;
  }

  result = key.SetDWORDValue(kSendStatsName, val ? 1 : 0);
  if (ERROR_SUCCESS != result) {
    return kReturnCodeInError;
  }

  return true;
}

#endif  // _WIN32

#ifdef __APPLE__
class MacStatsConfigUtilImpl : public StatsConfigUtilInterface {
 public:
  MacStatsConfigUtilImpl();

  MacStatsConfigUtilImpl(const MacStatsConfigUtilImpl &) = delete;
  MacStatsConfigUtilImpl &operator=(const MacStatsConfigUtilImpl &) = delete;

  ~MacStatsConfigUtilImpl() override = default;
  bool IsEnabled() override;
  bool SetEnabled(bool val) override;

 private:
  std::string config_file_;
  absl::Mutex mutex_;
};

MacStatsConfigUtilImpl::MacStatsConfigUtilImpl() {
  config_file_ =
      SystemUtil::GetUserProfileDirectory() + "/.usagestats.db";  // hidden file
}

bool MacStatsConfigUtilImpl::IsEnabled() {
#ifdef CHANNEL_DEV
  return true;
#else   // CHANNEL_DEV
  absl::MutexLock l(&mutex_);
  constexpr bool kDefaultValue = false;

  std::ifstream ifs(config_file_.c_str(), std::ios::binary | std::ios::in);

  if (!ifs.is_open()) {
    return kDefaultValue;
  }

  uint32_t value = kDefaultValue;
  if (!ifs.read(reinterpret_cast<char *>(&value), sizeof(value))) {
    return kDefaultValue;
  }

  // The value of usage stats is a 32-bit int and 1 (true) means
  // "sending the usage stats to Google".  When the meaning of the
  // value changed, we should fix mac/ActivatePane.m too.
  // TODO(mukai): export MacStatsConfigUtilImpl and share the code
  // among them.
  return static_cast<bool>(value);
#endif  // CHANNEL_DEV
}

bool MacStatsConfigUtilImpl::SetEnabled(bool val) {
#ifdef CHANNEL_DEV
  return true;
#else   // CHANNEL_DEV
  absl::MutexLock l(&mutex_);
  const uint32_t value = static_cast<uint32_t>(val);

  if (FileUtil::FileExists(config_file_).ok()) {
    ::chmod(config_file_.c_str(), S_IRUSR | S_IWUSR);  // read/write
  }
  std::ofstream ofs(config_file_.c_str(),
                    std::ios::binary | std::ios::out | std::ios::trunc);
  if (!ofs) {
    return false;
  }
  ofs.write(reinterpret_cast<const char *>(&value), sizeof(value));
  if (!ofs.good()) {
    return false;
  }
  return 0 == ::chmod(config_file_.c_str(), S_IRUSR);  // read only
#endif  // CHANNEL_DEV
}
#endif  // MACOSX

#ifdef __ANDROID__
class AndroidStatsConfigUtilImpl : public StatsConfigUtilInterface {
 public:
  AndroidStatsConfigUtilImpl() {}
  AndroidStatsConfigUtilImpl(const AndroidStatsConfigUtilImpl &) = delete;
  AndroidStatsConfigUtilImpl &operator=(const AndroidStatsConfigUtilImpl &) =
      delete;
  virtual ~AndroidStatsConfigUtilImpl() {}
  virtual bool IsEnabled() {
    Config config;
    ConfigHandler::GetConfig(&config);
    return config.general_config().upload_usage_stats();
  }
  virtual bool SetEnabled(bool val) {
    // TODO(horo): Implement this.
    return false;
  }
};
#endif  // __ANDROID__

#endif  // GOOGLE_JAPANESE_INPUT_BUILD

class NullStatsConfigUtilImpl : public StatsConfigUtilInterface {
 public:
  NullStatsConfigUtilImpl() = default;
  NullStatsConfigUtilImpl(const NullStatsConfigUtilImpl &) = delete;
  NullStatsConfigUtilImpl &operator=(const NullStatsConfigUtilImpl &) = delete;
  ~NullStatsConfigUtilImpl() override = default;
  bool IsEnabled() override { return false; }
  bool SetEnabled(bool val) override { return true; }
};

StatsConfigUtilInterface *g_stats_config_util_handler = nullptr;

// GetStatsConfigUtil and SetHandler are not thread safe.

#if !defined(GOOGLE_JAPANESE_INPUT_BUILD)
// For non-official build, use null implementation.
typedef NullStatsConfigUtilImpl DefaultConfigUtilImpl;
#elif defined(_WIN32)
typedef WinStatsConfigUtilImpl DefaultConfigUtilImpl;
#elif defined(__APPLE__)
typedef MacStatsConfigUtilImpl DefaultConfigUtilImpl;
#elif defined(__ANDROID__)
typedef AndroidStatsConfigUtilImpl DefaultConfigUtilImpl;
#else   // Platforms
// Fall back mode.  Use null implementation.
typedef NullStatsConfigUtilImpl DefaultConfigUtilImpl;
#endif  // Platforms

StatsConfigUtilInterface &GetStatsConfigUtil() {
  if (g_stats_config_util_handler == nullptr) {
    return *(Singleton<DefaultConfigUtilImpl>::get());
  } else {
    return *g_stats_config_util_handler;
  }
}
}  // namespace

void StatsConfigUtil::SetHandler(StatsConfigUtilInterface *handler) {
  g_stats_config_util_handler = handler;
}

bool StatsConfigUtil::IsEnabled() { return GetStatsConfigUtil().IsEnabled(); }

bool StatsConfigUtil::SetEnabled(bool val) {
  return GetStatsConfigUtil().SetEnabled(val);
}

}  // namespace config
}  // namespace mozc
