#include "Playground/Host/WiiUHost.h"

#include <coreinit/filesystem.h>
#include <nn/swkbd/swkbd_cpp.h>
#include <vpad/input.h>
#include <whb/proc.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <string>
#include <vector>

namespace {

std::u16string fromUtf8(const char *input) {
  std::u16string output;
  const auto *cursor = reinterpret_cast<const unsigned char *>(
      input ? input : "");
  while (*cursor) {
    std::uint32_t codepoint = 0xfffd;
    std::size_t count = 1;
    if (*cursor < 0x80) {
      codepoint = *cursor;
    } else if ((*cursor & 0xe0) == 0xc0 && cursor[1]) {
      codepoint = ((*cursor & 0x1f) << 6) | (cursor[1] & 0x3f);
      count = 2;
    } else if ((*cursor & 0xf0) == 0xe0 && cursor[1] && cursor[2]) {
      codepoint = ((*cursor & 0x0f) << 12) | ((cursor[1] & 0x3f) << 6) |
                  (cursor[2] & 0x3f);
      count = 3;
    } else if ((*cursor & 0xf8) == 0xf0 && cursor[1] && cursor[2] &&
               cursor[3]) {
      codepoint = ((*cursor & 0x07) << 18) | ((cursor[1] & 0x3f) << 12) |
                  ((cursor[2] & 0x3f) << 6) | (cursor[3] & 0x3f);
      count = 4;
    }
    cursor += count;
    if (codepoint > 0xffff && codepoint <= 0x10ffff) {
      codepoint -= 0x10000;
      output.push_back(static_cast<char16_t>(0xd800 | (codepoint >> 10)));
      output.push_back(static_cast<char16_t>(0xdc00 | (codepoint & 0x3ff)));
    } else if (codepoint < 0xd800 || codepoint > 0xdfff) {
      output.push_back(static_cast<char16_t>(codepoint));
    } else {
      output.push_back(u'\ufffd');
    }
  }
  return output;
}

std::string toUtf8(const char16_t *input) {
  std::string output;
  if (!input)
    return output;
  while (*input) {
    std::uint32_t codepoint = *input++;
    if (codepoint >= 0xd800 && codepoint <= 0xdbff && *input >= 0xdc00 &&
        *input <= 0xdfff) {
      codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*input++ - 0xdc00);
    }
    if (codepoint < 0x80) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
      output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint < 0x10000) {
      output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
  }
  return output;
}

} // namespace

extern "C" bool playgroundWiiUHostShowSoftwareKeyboard(
    PlaygroundWiiUHost *host, bool password, const char *initialText,
    const char *hintText, std::uint32_t maximumCharacters, char *output,
    std::uint32_t outputBytes) {
  if (!host)
    host = playgroundWiiUHostGetActive();
  if (!host || !output || !outputBytes)
    return false;
  output[0] = '\0';

  FSClient fsClient{};
  const FSStatus fsStatus = FSAddClient(&fsClient, FS_ERROR_FLAG_NONE);
  std::fprintf(stderr, "wiiu keyboard: FSAddClient=%d\n",
               static_cast<int>(fsStatus));
  if (fsStatus != FS_STATUS_OK)
    return false;
  const std::uint32_t workSize = nn::swkbd::GetWorkMemorySize(0);
  std::fprintf(stderr, "wiiu keyboard: work size=%u\n", workSize);
  void *work = memalign(0x40, workSize);
  if (!work) {
    std::fprintf(stderr, "wiiu keyboard: work allocation failed\n");
    FSDelClient(&fsClient, FS_ERROR_FLAG_NONE);
    return false;
  }

  nn::swkbd::CreateArg create;
  create.workMemory = work;
  create.regionType = nn::swkbd::RegionType::Japan;
  create.fsClient = &fsClient;
  if (!nn::swkbd::Create(create)) {
    std::fprintf(stderr, "wiiu keyboard: Create failed\n");
    std::free(work);
    FSDelClient(&fsClient, FS_ERROR_FLAG_NONE);
    return false;
  }
  std::fprintf(stderr, "wiiu keyboard: Create succeeded\n");

  const std::u16string initial = fromUtf8(initialText);
  const std::u16string hint = fromUtf8(hintText);
  nn::swkbd::AppearArg appear;
  appear.keyboardArg.configArg.languageType =
      nn::swkbd::LanguageType::Japanese;
  appear.keyboardArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Full;
  appear.keyboardArg.configArg.disableNewLine = true;
  appear.inputFormArg.initialText = initial.c_str();
  appear.inputFormArg.hintText = hint.empty() ? nullptr : hint.c_str();
  appear.inputFormArg.maxTextLength = maximumCharacters
                                          ? static_cast<std::int32_t>(
                                                maximumCharacters)
                                          : -1;
  appear.inputFormArg.passwordMode =
      password ? nn::swkbd::PasswordMode::Hide
               : nn::swkbd::PasswordMode::Clear;

  bool accepted = false;
  bool closing = false;
  std::string result;
  if (nn::swkbd::AppearInputForm(appear)) {
    std::fprintf(stderr, "wiiu keyboard: AppearInputForm succeeded\n");
    nn::swkbd::State previousState = nn::swkbd::State::Hidden;
    while (WHBProcIsRunning()) {
      VPADStatus status{};
      VPADReadError readError = VPAD_READ_SUCCESS;
      nn::swkbd::ControllerInfo controller;
      if (VPADRead(VPAD_CHAN_0, &status, 1, &readError) == 1 &&
          readError == VPAD_READ_SUCCESS) {
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &status.tpNormal,
                                &status.tpNormal);
        controller.vpad = &status;
      }
      nn::swkbd::Calc(controller);
      const nn::swkbd::State state = nn::swkbd::GetStateInputForm();
      if (state != previousState) {
        std::fprintf(stderr, "wiiu keyboard: state=%d\n",
                     static_cast<int>(state));
        previousState = state;
      }
      if (nn::swkbd::IsNeedCalcSubThreadFont())
        nn::swkbd::CalcSubThreadFont();
      if (nn::swkbd::IsNeedCalcSubThreadPredict())
        nn::swkbd::CalcSubThreadPredict();

      if (!closing && nn::swkbd::IsDecideOkButton(nullptr)) {
        std::fprintf(stderr, "wiiu keyboard: OK selected\n");
        result = toUtf8(nn::swkbd::GetInputFormString());
        accepted = true;
        closing = nn::swkbd::DisappearInputForm();
      } else if (!closing && nn::swkbd::IsDecideCancelButton(nullptr)) {
        std::fprintf(stderr, "wiiu keyboard: Cancel selected\n");
        closing = nn::swkbd::DisappearInputForm();
      }
      nn::swkbd::DrawTV();
      nn::swkbd::DrawDRC();
      playgroundWiiUHostSwapBuffers(host);
      if (closing &&
          nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden)
        break;
    }
  } else {
    std::fprintf(stderr, "wiiu keyboard: AppearInputForm failed\n");
  }

  std::fprintf(stderr, "wiiu keyboard: destroying accepted=%d closing=%d\n",
               accepted ? 1 : 0, closing ? 1 : 0);
  nn::swkbd::Destroy();
  std::free(work);
  FSDelClient(&fsClient, FS_ERROR_FLAG_NONE);
  if (!accepted || result.size() >= outputBytes)
    return false;
  std::memcpy(output, result.c_str(), result.size() + 1);
  return true;
}
