#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "gisengine/rhi/vulkan.h"
#include "gisengine/runtime/runtime.h"

// 关闭构建期着色器链时保留示例可编译，运行时会明确报告管线不可用。
#ifndef GISENGINE_TRIANGLE_VERTEX_SHADER_PATH
#define GISENGINE_TRIANGLE_VERTEX_SHADER_PATH ""
#endif
#ifndef GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH
#define GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH ""
#endif

// 进程入口已经将引擎初始化失败转换为退出码；流输出的理论异常不属于可恢复业务错误。
// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
    try {
        gisengine::runtime::RuntimeHost host{
            gisengine::core::EngineConfig{.project_name = "NativeSandbox", .fixed_update_hz = 60U}};
        host.initialize();

        std::cout << "GisEngine NativeSandbox\n"
                  << "平台: " << host.platform_info().operating_system << "\n"
                  << "编译器: " << host.platform_info().compiler << "\n"
                  << "状态: " << static_cast<int>(host.engine().state()) << "\n";

        const auto instance = gisengine::rhi::VulkanInstance::try_create();
        if (!instance.has_value()) {
            std::cout << "Vulkan 不可用，保持基线启动状态。\n";
            return 0;
        }
        auto surface = gisengine::rhi::VulkanSurface::try_create(
            *instance, gisengine::rhi::VulkanSurfaceCreateInfo{.width = 1280U, .height = 720U, .visible = true});
        if (!surface.has_value()) {
            std::cout << "Vulkan 窗口表面不可用，保持基线启动状态。\n";
            return 0;
        }
        auto device = gisengine::rhi::VulkanDevice::try_create(*instance, *surface);
        if (!device.has_value()) {
            std::cout << "Vulkan 图形与呈现设备不可用，保持基线启动状态。\n";
            return 0;
        }
        auto swapchain = gisengine::rhi::VulkanSwapchain::try_create(*device, *surface);
        if (!swapchain.has_value()) {
            std::cout << "Vulkan 交换链不可用，保持基线启动状态。\n";
            return 0;
        }
        auto command_context = gisengine::rhi::VulkanCommandContext::try_create(*device);
        auto frame_sync = gisengine::rhi::VulkanFrameSync::try_create(*device);
        if (!command_context.has_value() || !frame_sync.has_value()) {
            std::cout << "Vulkan 命令或同步资源不可用，保持基线启动状态。\n";
            return 0;
        }
        auto pipeline = gisengine::rhi::VulkanTrianglePipeline::try_create(
            *device, *swapchain,
            gisengine::rhi::VulkanShaderBinaryPaths{
                .vertex_shader_path = GISENGINE_TRIANGLE_VERTEX_SHADER_PATH,
                .fragment_shader_path = GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH,
            });
        if (!pipeline.has_value()) {
            std::cout << "Vulkan 三角形图形管线不可用，保持基线启动状态。\n";
            return 0;
        }
        const auto shader_paths = gisengine::rhi::VulkanShaderBinaryPaths{
            .vertex_shader_path = GISENGINE_TRIANGLE_VERTEX_SHADER_PATH,
            .fragment_shader_path = GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH,
        };
        // 先完整构造新资源，再释放旧管线和旧交换链，保证帧缓冲始终引用有效图像视图。
        const auto recreate_triangle_renderer = [&]() -> bool {
            auto recreated_swapchain = gisengine::rhi::VulkanSwapchain::try_create(*device, *surface);
            if (!recreated_swapchain.has_value()) {
                return false;
            }
            auto recreated_pipeline =
                gisengine::rhi::VulkanTrianglePipeline::try_create(*device, *recreated_swapchain, shader_paths);
            if (!recreated_pipeline.has_value()) {
                return false;
            }
            pipeline = std::move(*recreated_pipeline);
            swapchain = std::move(*recreated_swapchain);
            surface->acknowledge_resize();
            return true;
        };
        std::cout << "已打开 1280x720 Vulkan 窗口；当前显示彩色三角形，关闭窗口退出。\n" << std::flush;
        while (surface->process_events()) {
            if (!surface->has_renderable_extent()) {
                // 最小化时 Vulkan 不能创建零尺寸交换链；保留消息循环并等待窗口恢复。
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            if ((surface->has_pending_resize() || swapchain->requires_recreation()) && !recreate_triangle_renderer()) {
                break;
            }
            if (!swapchain->present_triangle(*device, *command_context, *frame_sync, *pipeline,
                                             gisengine::rhi::VulkanClearColor{})) {
                if (!swapchain->requires_recreation() || !recreate_triangle_renderer()) {
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "启动失败: " << error.what() << '\n';
        return 1;
    }
}
