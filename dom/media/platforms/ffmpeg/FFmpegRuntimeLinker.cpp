/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FFmpegRuntimeLinker.h"
#include "mozilla/ArrayUtils.h"
#include "FFmpegLog.h"
#include "mozilla/Preferences.h"
#include "prlink.h"

namespace mozilla
{

FFmpegRuntimeLinker::LinkStatus FFmpegRuntimeLinker::sLinkStatus =
  LinkStatus_INIT;

template <int V> class FFmpegDecoderModule
{
public:
  static already_AddRefed<PlatformDecoderModule> Create();
};

static const char* sLibs[] = {
#if defined(XP_DARWIN)
  "libavcodec.57.dylib",
  "libavcodec.56.dylib",
  "libavcodec.55.dylib",
  "libavcodec.54.dylib",
  "libavcodec.53.dylib",
#else
  "libavcodec-ffmpeg.so.57",
  "libavcodec-ffmpeg.so.56",
  "libavcodec.so.57",
  "libavcodec.so.56",
  "libavcodec.so.55",
  "libavcodec.so.54",
  "libavcodec.so.53",
#endif
};

PRLibrary* FFmpegRuntimeLinker::sLinkedLib = nullptr;
const char* FFmpegRuntimeLinker::sLib = nullptr;
static unsigned (*avcodec_version)() = nullptr;

#define AV_FUNC(func, ver) void (*func)();

#define LIBAVCODEC_ALLVERSION
#include "FFmpegFunctionList.h"
#undef LIBAVCODEC_ALLVERSION
#undef AV_FUNC

/* static */ bool
FFmpegRuntimeLinker::Link()
{
  if (sLinkStatus) {
    return sLinkStatus == LinkStatus_SUCCEEDED;
  }

  MOZ_ASSERT(NS_IsMainThread());

  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
    const char* lib = sLibs[i];
    PRLibSpec lspec;
    lspec.type = PR_LibSpec_Pathname;
    lspec.value.pathname = lib;
    sLinkedLib = PR_LoadLibraryWithFlags(lspec, PR_LD_NOW | PR_LD_LOCAL);
    if (sLinkedLib) {
      if (Bind(lib)) {
        sLib = lib;
        sLinkStatus = LinkStatus_SUCCEEDED;
        return true;
      }
      // Shouldn't happen but if it does then we try the next lib..
      Unlink();
    }
  }

  FFMPEG_LOG("H264/AAC codecs unsupported without [");
  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
    FFMPEG_LOG("%s %s", i ? "," : "", sLibs[i]);
  }
  FFMPEG_LOG(" ]\n");

  Unlink();

  sLinkStatus = LinkStatus_FAILED;
  return false;
}

/* static */ bool
FFmpegRuntimeLinker::Bind(const char* aLibName)
{
  avcodec_version = (typeof(avcodec_version))PR_FindSymbol(sLinkedLib,
                                                           "avcodec_version");
  uint32_t fullVersion, major, minor, micro;
  fullVersion = GetVersion(major, minor, micro);
  if (!fullVersion) {
    return false;
  }

  if (micro < 100 &&
      fullVersion < (54u << 16 | 35u << 8 | 1u) &&
      !Preferences::GetBool("media.libavcodec.allow-obsolete", false)) {
    // Refuse any libavcodec version prior to 54.35.1.
    // (Unless media.libavcodec.allow-obsolete==true)
    Unlink();
    LogToBrowserConsole(NS_LITERAL_STRING(
      "libavcodec may be vulnerable or is not supported, and should be updated to play video."));
    return false;
  }

  int version;
  switch (major) {
    case 53:
      version = AV_FUNC_53;
      break;
    case 54:
      version = AV_FUNC_54;
      break;
    case 56:
      // We use libavcodec 55 code instead. Fallback
    case 55:
      version = AV_FUNC_55;
      break;
    case 57:
      if (micro < 100) {
        // a micro version >= 100 indicates that it's FFmpeg (as opposed to LibAV).
        // Due to current AVCodecContext binary incompatibility we can only
        // support FFmpeg at this stage.
        return false;
      }
      version = AV_FUNC_57;
      break;
    default:
      // Not supported at this stage.
      return false;
  }

#define LIBAVCODEC_ALLVERSION
#define AV_FUNC(func, ver)                                                     \
  if ((ver) & version) {                                                       \
    if (!(func = (typeof(func))PR_FindSymbol(sLinkedLib, #func))) {            \
      FFMPEG_LOG("Couldn't load function " #func " from %s.", aLibName);       \
      return false;                                                            \
    }                                                                          \
  } else {                                                                     \
    func = (typeof(func))nullptr;                                              \
  }
#include "FFmpegFunctionList.h"
#undef AV_FUNC
#undef LIBAVCODEC_ALLVERSION
  return true;
}

/* static */ already_AddRefed<PlatformDecoderModule>
FFmpegRuntimeLinker::CreateDecoderModule()
{
  if (!Link()) {
    return nullptr;
  }
  uint32_t major, minor, micro;
  if (!GetVersion(major, minor, micro)) {
    return  nullptr;
  }

  RefPtr<PlatformDecoderModule> module;
  switch (major) {
    case 53: module = FFmpegDecoderModule<53>::Create(); break;
    case 54: module = FFmpegDecoderModule<54>::Create(); break;
    case 55:
    case 56: module = FFmpegDecoderModule<55>::Create(); break;
    case 57: module = FFmpegDecoderModule<57>::Create(); break;
    default: module = nullptr;
  }
  return module.forget();
}

/* static */ void
FFmpegRuntimeLinker::Unlink()
{
  if (sLinkedLib) {
    PR_UnloadLibrary(sLinkedLib);
    sLinkedLib = nullptr;
    sLib = nullptr;
    sLinkStatus = LinkStatus_INIT;
    avcodec_version = nullptr;
  }
}

/* static */ uint32_t
FFmpegRuntimeLinker::GetVersion(uint32_t& aMajor, uint32_t& aMinor, uint32_t& aMicro)
{
  if (!avcodec_version) {
    return 0u;
  }
  uint32_t version = avcodec_version();
  aMajor = (version >> 16) & 0xff;
  aMinor = (version >> 8) & 0xff;
  aMicro = version & 0xff;
  return version;
}

} // namespace mozilla
