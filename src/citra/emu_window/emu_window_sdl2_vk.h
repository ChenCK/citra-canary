// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "citra/emu_window/emu_window_sdl2.h"

namespace Frontend {
class GraphicsContext;
}

class EmuWindow_SDL2_VK final : public EmuWindow_SDL2 {
public:
    explicit EmuWindow_SDL2_VK(bool fullscreen, bool is_secondary);
    ~EmuWindow_SDL2_VK() override;

    std::unique_ptr<Frontend::GraphicsContext> CreateSharedContext() const override;
};
