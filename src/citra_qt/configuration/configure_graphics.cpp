// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QColorDialog>
#include "citra_qt/configuration/configuration_shared.h"
#include "citra_qt/configuration/configure_graphics.h"
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_graphics.h"
#include "video_core/renderer_vulkan/vk_instance.h"

ConfigureGraphics::ConfigureGraphics(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureGraphics>()) {
    ui->setupUi(this);

    DiscoverPhysicalDevices();
    SetupPerGameUI();

    const bool not_running = !Core::System::GetInstance().IsPoweredOn();
    ui->toggle_vsync_new->setEnabled(not_running);
    ui->physical_device_combo->setEnabled(not_running);
    ui->toggle_async_shaders->setEnabled(not_running);
    ui->toggle_async_present->setEnabled(not_running);
    // Set the index to -1 to ensure the below lambda is called with setCurrentIndex
    ui->graphics_api_combo->setCurrentIndex(-1);

    connect(ui->graphics_api_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const auto graphics_api =
                    ConfigurationShared::GetComboboxSetting(index, &Settings::values.graphics_api);
                const bool is_software = graphics_api == Settings::GraphicsAPI::Software;

                ui->hw_renderer_group->setEnabled(!is_software);
                ui->toggle_disk_shader_cache->setEnabled(!is_software &&
                                                         ui->toggle_hw_shader->isChecked());
            });

    connect(ui->toggle_hw_shader, &QCheckBox::toggled, this, [this] {
        const bool checked = ui->toggle_hw_shader->isChecked();
        ui->hw_shader_group->setEnabled(checked);
        ui->toggle_disk_shader_cache->setEnabled(checked);
    });

    connect(ui->graphics_api_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureGraphics::SetPhysicalDeviceComboVisibility);

    SetConfiguration();
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::SetConfiguration() {
    if (!Settings::IsConfiguringGlobal()) {
        ConfigurationShared::SetHighlight(ui->physical_device_group,
                                          !Settings::values.physical_device.UsingGlobal());
        ConfigurationShared::SetPerGameSetting(ui->physical_device_combo,
                                               &Settings::values.physical_device);
        ConfigurationShared::SetHighlight(ui->graphics_api_group,
                                          !Settings::values.graphics_api.UsingGlobal());
        ConfigurationShared::SetPerGameSetting(ui->graphics_api_combo,
                                               &Settings::values.graphics_api);
    } else {
        ui->physical_device_combo->setCurrentIndex(
            static_cast<int>(Settings::values.physical_device.GetValue()));
        ui->graphics_api_combo->setCurrentIndex(
            static_cast<int>(Settings::values.graphics_api.GetValue()));
    }

    ui->toggle_hw_shader->setChecked(Settings::values.use_hw_shader.GetValue());
    ui->toggle_accurate_mul->setChecked(Settings::values.shaders_accurate_mul.GetValue());
    ui->toggle_disk_shader_cache->setChecked(Settings::values.use_disk_shader_cache.GetValue());
    ui->toggle_vsync_new->setChecked(Settings::values.use_vsync_new.GetValue());
    ui->spirv_shader_gen->setChecked(Settings::values.spirv_shader_gen.GetValue());
    ui->toggle_async_shaders->setChecked(Settings::values.async_shader_compilation.GetValue());
    ui->toggle_async_present->setChecked(Settings::values.async_presentation.GetValue());

    if (Settings::IsConfiguringGlobal()) {
        ui->toggle_shader_jit->setChecked(Settings::values.use_shader_jit.GetValue());
    }
}

void ConfigureGraphics::ApplyConfiguration() {
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.graphics_api,
                                             ui->graphics_api_combo);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.physical_device,
                                             ui->physical_device_combo);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.async_shader_compilation,
                                             ui->toggle_async_shaders, async_shader_compilation);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.async_presentation,
                                             ui->toggle_async_present, async_presentation);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.spirv_shader_gen,
                                             ui->spirv_shader_gen, spirv_shader_gen);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.use_hw_shader, ui->toggle_hw_shader,
                                             use_hw_shader);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.shaders_accurate_mul,
                                             ui->toggle_accurate_mul, shaders_accurate_mul);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.use_disk_shader_cache,
                                             ui->toggle_disk_shader_cache, use_disk_shader_cache);
    ConfigurationShared::ApplyPerGameSetting(&Settings::values.use_vsync_new, ui->toggle_vsync_new,
                                             use_vsync_new);

    if (Settings::IsConfiguringGlobal()) {
        Settings::values.use_shader_jit = ui->toggle_shader_jit->isChecked();
    }
}

void ConfigureGraphics::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGraphics::SetupPerGameUI() {
    // Block the global settings if a game is currently running that overrides them
    if (Settings::IsConfiguringGlobal()) {
        ui->graphics_api_group->setEnabled(Settings::values.graphics_api.UsingGlobal());
        ui->toggle_hw_shader->setEnabled(Settings::values.use_hw_shader.UsingGlobal());
        ui->toggle_accurate_mul->setEnabled(Settings::values.shaders_accurate_mul.UsingGlobal());
        ui->toggle_disk_shader_cache->setEnabled(
            Settings::values.use_disk_shader_cache.UsingGlobal());
        ui->toggle_vsync_new->setEnabled(ui->toggle_vsync_new->isEnabled() &&
                                         Settings::values.use_vsync_new.UsingGlobal());
        ui->toggle_async_shaders->setEnabled(
            Settings::values.async_shader_compilation.UsingGlobal());
        ui->toggle_async_present->setEnabled(Settings::values.async_presentation.UsingGlobal());
        ui->graphics_api_combo->setEnabled(Settings::values.graphics_api.UsingGlobal());
        ui->physical_device_combo->setEnabled(Settings::values.physical_device.UsingGlobal());
        return;
    }

    ui->toggle_shader_jit->setVisible(false);

    ConfigurationShared::SetColoredComboBox(
        ui->graphics_api_combo, ui->graphics_api_group,
        static_cast<u32>(Settings::values.graphics_api.GetValue(true)));

    ConfigurationShared::SetColoredComboBox(
        ui->physical_device_combo, ui->physical_device_group,
        static_cast<u32>(Settings::values.physical_device.GetValue(true)));

    ConfigurationShared::SetColoredTristate(ui->toggle_hw_shader, Settings::values.use_hw_shader,
                                            use_hw_shader);
    ConfigurationShared::SetColoredTristate(
        ui->toggle_accurate_mul, Settings::values.shaders_accurate_mul, shaders_accurate_mul);
    ConfigurationShared::SetColoredTristate(ui->toggle_disk_shader_cache,
                                            Settings::values.use_disk_shader_cache,
                                            use_disk_shader_cache);
    ConfigurationShared::SetColoredTristate(ui->toggle_vsync_new, Settings::values.use_vsync_new,
                                            use_vsync_new);
    ConfigurationShared::SetColoredTristate(ui->toggle_async_shaders,
                                            Settings::values.async_shader_compilation,
                                            async_shader_compilation);
    ConfigurationShared::SetColoredTristate(
        ui->toggle_async_present, Settings::values.async_presentation, async_presentation);
    ConfigurationShared::SetColoredTristate(ui->spirv_shader_gen, Settings::values.spirv_shader_gen,
                                            spirv_shader_gen);
}

void ConfigureGraphics::DiscoverPhysicalDevices() {
    if (physical_devices_discovered) {
        return;
    }

    try {
        Vulkan::Instance instance{};
        const auto physical_devices = instance.GetPhysicalDevices();

        for (const vk::PhysicalDevice& physical_device : physical_devices) {
            const QString name = QString::fromLocal8Bit(physical_device.getProperties().deviceName);
            ui->physical_device_combo->addItem(name);
        }

        physical_devices_discovered = true;
    } catch (...) {
        LOG_ERROR(Frontend, "Device does not support Vulkan");
        ui->graphics_api_combo->removeItem(2);
        Settings::values.graphics_api = Settings::GraphicsAPI::OpenGL;
    }
}

void ConfigureGraphics::SetPhysicalDeviceComboVisibility(int index) {
    bool is_visible{false};

    // When configuring per-game the physical device combo should be
    // shown either when the global api is used and that is Vulkan or
    // Vulkan is set as the per-game api.
    if (!Settings::IsConfiguringGlobal()) {
        const auto global_graphics_api = Settings::values.graphics_api.GetValue(true);
        const bool using_global = index == 0;
        if (!using_global) {
            index -= ConfigurationShared::USE_GLOBAL_OFFSET;
        }
        const auto graphics_api = static_cast<Settings::GraphicsAPI>(index);
        is_visible = (using_global && global_graphics_api == Settings::GraphicsAPI::Vulkan) ||
                     graphics_api == Settings::GraphicsAPI::Vulkan;
    } else {
        const auto graphics_api = static_cast<Settings::GraphicsAPI>(index);
        is_visible = graphics_api == Settings::GraphicsAPI::Vulkan;
    }
    ui->physical_device_group->setVisible(is_visible);
    ui->spirv_shader_gen->setVisible(is_visible);
}
