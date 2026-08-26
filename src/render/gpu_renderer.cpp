#include "render/gpu_renderer.hpp"

#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace whoami::render {
namespace {

constexpr std::size_t kMaxVertices = 262'144;
constexpr std::size_t kMaxIndices = 393'216;

constexpr auto kVertexShader = R"hlsl(
cbuffer Viewport : register(b0, space1)
{
    float2 viewport_size;
    float2 padding;
};

struct VertexInput
{
    float2 position : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(
        input.position.x * (2.0 / viewport_size.x) - 1.0,
        1.0 - input.position.y * (2.0 / viewport_size.y),
        0.0,
        1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
)hlsl";

constexpr auto kSolidFragmentShader = R"hlsl(
struct FragmentInput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
    return input.color;
}
)hlsl";

constexpr auto kTextFragmentShader = R"hlsl(
Texture2D<float4> atlas : register(t0, space2);
SamplerState atlas_sampler : register(s0, space2);

struct FragmentInput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
    return input.color * atlas.Sample(atlas_sampler, input.uv);
}
)hlsl";

[[noreturn]] void fail(std::string_view operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

SDL_GPUShader* compile_shader(SDL_GPUDevice* device, const char* source,
                              SDL_ShaderCross_ShaderStage stage) {
    const SDL_ShaderCross_HLSL_Info hlsl{
        .source = source,
        .entrypoint = "main",
        .include_dir = nullptr,
        .defines = nullptr,
        .shader_stage = stage,
        .props = 0,
    };
    std::size_t spirv_size{};
    auto* spirv = static_cast<Uint8*>(SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl, &spirv_size));
    if (!spirv) {
        fail("compile HLSL shader");
    }

    const auto* metadata = SDL_ShaderCross_ReflectGraphicsSPIRV(spirv, spirv_size, 0);
    if (!metadata) {
        SDL_free(spirv);
        fail("reflect shader");
    }

    const SDL_ShaderCross_SPIRV_Info info{
        .bytecode = spirv,
        .bytecode_size = spirv_size,
        .entrypoint = "main",
        .shader_stage = stage,
        .props = 0,
    };
    auto* shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
        device, &info, &metadata->resource_info, 0);
    SDL_free(const_cast<SDL_ShaderCross_GraphicsShaderMetadata*>(metadata));
    SDL_free(spirv);
    if (!shader) {
        fail("create GPU shader");
    }
    return shader;
}

std::filesystem::path first_existing(std::initializer_list<const char*> candidates) {
    for (const auto* candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("no suitable system font was found");
}

} // namespace

struct GpuRenderer::Impl {
    struct Vertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
        float u;
        float v;
    };

    enum class Pipeline { solid, text };

    struct Batch {
        Pipeline pipeline{};
        SDL_GPUTexture* texture{};
        std::uint32_t first_index{};
        std::uint32_t index_count{};
    };

    SDL_Window* window{};
    SDL_GPUDevice* device{};
    SDL_GPUShader* vertex_shader{};
    SDL_GPUShader* solid_fragment_shader{};
    SDL_GPUShader* text_fragment_shader{};
    SDL_GPUGraphicsPipeline* solid_pipeline{};
    SDL_GPUGraphicsPipeline* text_pipeline{};
    SDL_GPUSampler* sampler{};
    SDL_GPUBuffer* vertex_buffer{};
    SDL_GPUBuffer* index_buffer{};
    SDL_GPUTransferBuffer* transfer_buffer{};
    TTF_TextEngine* text_engine{};
    std::filesystem::path ui_font_path;
    std::filesystem::path mono_font_path;
    std::unordered_map<int, TTF_Font*> ui_fonts;
    std::unordered_map<int, TTF_Font*> mono_fonts;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Batch> batches;
    int width{};
    int height{};

    TTF_Font* font(float point_size, bool monospace) {
        const int key = std::max(8, static_cast<int>(point_size + 0.5F));
        auto& fonts = monospace ? mono_fonts : ui_fonts;
        if (const auto found = fonts.find(key); found != fonts.end()) {
            return found->second;
        }
        const auto& path = monospace ? mono_font_path : ui_font_path;
        auto* result = TTF_OpenFont(path.c_str(), static_cast<float>(key));
        if (!result) {
            fail("open font");
        }
        fonts.emplace(key, result);
        return result;
    }

    void add_batch(Pipeline pipeline, SDL_GPUTexture* texture,
                   std::uint32_t first, std::uint32_t count) {
        if (count == 0) {
            return;
        }
        if (!batches.empty()) {
            auto& previous = batches.back();
            if (previous.pipeline == pipeline && previous.texture == texture &&
                previous.first_index + previous.index_count == first) {
                previous.index_count += count;
                return;
            }
        }
        batches.push_back(Batch{pipeline, texture, first, count});
    }
};

GpuRenderer::GpuRenderer(int width, int height, std::string_view title)
    : impl_(std::make_unique<Impl>()) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail("initialize SDL");
    }
    if (!TTF_Init()) {
        fail("initialize SDL_ttf");
    }
    if (!SDL_ShaderCross_Init()) {
        fail("initialize SDL_shadercross");
    }

    impl_->window = SDL_CreateWindow(std::string(title).c_str(), width, height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!impl_->window) {
        fail("create window");
    }
    impl_->device = SDL_CreateGPUDevice(SDL_ShaderCross_GetHLSLShaderFormats(), true, nullptr);
    if (!impl_->device) {
        fail("create GPU device");
    }
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window)) {
        fail("claim window for GPU device");
    }
    (void)SDL_SetGPUAllowedFramesInFlight(impl_->device, 2);

    impl_->vertex_shader = compile_shader(
        impl_->device, kVertexShader, SDL_SHADERCROSS_SHADERSTAGE_VERTEX);
    impl_->solid_fragment_shader = compile_shader(
        impl_->device, kSolidFragmentShader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);
    impl_->text_fragment_shader = compile_shader(
        impl_->device, kTextFragmentShader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT);

    const SDL_GPUVertexBufferDescription buffer_description{
        .slot = 0,
        .pitch = sizeof(Impl::Vertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };
    const std::array attributes{
        SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                               offsetof(Impl::Vertex, x)},
        SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                               offsetof(Impl::Vertex, r)},
        SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                               offsetof(Impl::Vertex, u)},
    };
    SDL_GPUColorTargetDescription color_target{
        .format = SDL_GetGPUSwapchainTextureFormat(impl_->device, impl_->window),
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true,
        },
    };
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{
        .vertex_shader = impl_->vertex_shader,
        .fragment_shader = impl_->solid_fragment_shader,
        .vertex_input_state = {
            .vertex_buffer_descriptions = &buffer_description,
            .num_vertex_buffers = 1,
            .vertex_attributes = attributes.data(),
            .num_vertex_attributes = static_cast<Uint32>(attributes.size()),
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        },
        .multisample_state = {.sample_count = SDL_GPU_SAMPLECOUNT_1},
        .target_info = {
            .color_target_descriptions = &color_target,
            .num_color_targets = 1,
        },
    };
    impl_->solid_pipeline = SDL_CreateGPUGraphicsPipeline(impl_->device, &pipeline_info);
    if (!impl_->solid_pipeline) {
        fail("create solid graphics pipeline");
    }
    pipeline_info.fragment_shader = impl_->text_fragment_shader;
    impl_->text_pipeline = SDL_CreateGPUGraphicsPipeline(impl_->device, &pipeline_info);
    if (!impl_->text_pipeline) {
        fail("create text graphics pipeline");
    }

    const SDL_GPUSamplerCreateInfo sampler_info{
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    impl_->sampler = SDL_CreateGPUSampler(impl_->device, &sampler_info);
    if (!impl_->sampler) {
        fail("create text sampler");
    }

    const SDL_GPUBufferCreateInfo vertex_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = static_cast<Uint32>(kMaxVertices * sizeof(Impl::Vertex)),
    };
    impl_->vertex_buffer = SDL_CreateGPUBuffer(impl_->device, &vertex_buffer_info);
    const SDL_GPUBufferCreateInfo index_buffer_info{
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = static_cast<Uint32>(kMaxIndices * sizeof(std::uint32_t)),
    };
    impl_->index_buffer = SDL_CreateGPUBuffer(impl_->device, &index_buffer_info);
    const SDL_GPUTransferBufferCreateInfo transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(kMaxVertices * sizeof(Impl::Vertex) +
                                    kMaxIndices * sizeof(std::uint32_t)),
    };
    impl_->transfer_buffer = SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
    if (!impl_->vertex_buffer || !impl_->index_buffer || !impl_->transfer_buffer) {
        fail("create streaming GPU buffers");
    }

    impl_->text_engine = TTF_CreateGPUTextEngine(impl_->device);
    if (!impl_->text_engine) {
        fail("create GPU text engine");
    }
    TTF_SetGPUTextEngineWinding(impl_->text_engine, TTF_GPU_TEXTENGINE_WINDING_COUNTER_CLOCKWISE);
    impl_->ui_font_path = first_existing({
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    });
    impl_->mono_font_path = first_existing({
        "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    });

    SDL_GetWindowSizeInPixels(impl_->window, &impl_->width, &impl_->height);
    impl_->vertices.reserve(16'384);
    impl_->indices.reserve(24'576);
    impl_->batches.reserve(256);
}

GpuRenderer::~GpuRenderer() {
    if (!impl_) {
        return;
    }
    for (const auto& [_, font] : impl_->ui_fonts) {
        TTF_CloseFont(font);
    }
    for (const auto& [_, font] : impl_->mono_fonts) {
        TTF_CloseFont(font);
    }
    if (impl_->text_engine) TTF_DestroyGPUTextEngine(impl_->text_engine);
    if (impl_->transfer_buffer) SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->transfer_buffer);
    if (impl_->index_buffer) SDL_ReleaseGPUBuffer(impl_->device, impl_->index_buffer);
    if (impl_->vertex_buffer) SDL_ReleaseGPUBuffer(impl_->device, impl_->vertex_buffer);
    if (impl_->sampler) SDL_ReleaseGPUSampler(impl_->device, impl_->sampler);
    if (impl_->text_pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->text_pipeline);
    if (impl_->solid_pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->solid_pipeline);
    if (impl_->text_fragment_shader) SDL_ReleaseGPUShader(impl_->device, impl_->text_fragment_shader);
    if (impl_->solid_fragment_shader) SDL_ReleaseGPUShader(impl_->device, impl_->solid_fragment_shader);
    if (impl_->vertex_shader) SDL_ReleaseGPUShader(impl_->device, impl_->vertex_shader);
    if (impl_->device && impl_->window) SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
    if (impl_->device) SDL_DestroyGPUDevice(impl_->device);
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    SDL_ShaderCross_Quit();
    TTF_Quit();
    SDL_Quit();
}

SDL_Window* GpuRenderer::window() const noexcept { return impl_->window; }
int GpuRenderer::width() const noexcept { return impl_->width; }
int GpuRenderer::height() const noexcept { return impl_->height; }

void GpuRenderer::begin_frame() {
    SDL_GetWindowSizeInPixels(impl_->window, &impl_->width, &impl_->height);
    impl_->vertices.clear();
    impl_->indices.clear();
    impl_->batches.clear();
}

void GpuRenderer::fill_rect(Rect rect, Color color) {
    if (rect.w <= 0.0F || rect.h <= 0.0F) {
        return;
    }
    const auto base = static_cast<std::uint32_t>(impl_->vertices.size());
    const auto first = static_cast<std::uint32_t>(impl_->indices.size());
    impl_->vertices.insert(impl_->vertices.end(), {
        {rect.x, rect.y, color.r, color.g, color.b, color.a, 0.0F, 0.0F},
        {rect.x + rect.w, rect.y, color.r, color.g, color.b, color.a, 1.0F, 0.0F},
        {rect.x + rect.w, rect.y + rect.h, color.r, color.g, color.b, color.a, 1.0F, 1.0F},
        {rect.x, rect.y + rect.h, color.r, color.g, color.b, color.a, 0.0F, 1.0F},
    });
    impl_->indices.insert(impl_->indices.end(), {
        base, base + 1, base + 2, base, base + 2, base + 3,
    });
    impl_->add_batch(Impl::Pipeline::solid, nullptr, first, 6);
}

void GpuRenderer::stroke_rect(Rect rect, float thickness, Color color) {
    fill_rect({rect.x, rect.y, rect.w, thickness}, color);
    fill_rect({rect.x, rect.y + rect.h - thickness, rect.w, thickness}, color);
    fill_rect({rect.x, rect.y + thickness, thickness, rect.h - thickness * 2.0F}, color);
    fill_rect({rect.x + rect.w - thickness, rect.y + thickness,
               thickness, rect.h - thickness * 2.0F}, color);
}

void GpuRenderer::text(std::string_view value, float x, float y, float point_size,
                       Color color, bool monospace) {
    if (value.empty()) {
        return;
    }
    auto* text = TTF_CreateText(impl_->text_engine, impl_->font(point_size, monospace),
                                value.data(), value.size());
    if (!text) {
        fail("create GPU text");
    }
    for (auto* sequence = TTF_GetGPUTextDrawData(text); sequence; sequence = sequence->next) {
        const auto base = static_cast<std::uint32_t>(impl_->vertices.size());
        const auto first = static_cast<std::uint32_t>(impl_->indices.size());
        impl_->vertices.reserve(impl_->vertices.size() + sequence->num_vertices);
        for (int index = 0; index < sequence->num_vertices; ++index) {
            impl_->vertices.push_back({
                x + sequence->xy[index].x,
                y - sequence->xy[index].y,
                color.r, color.g, color.b, color.a,
                sequence->uv[index].x,
                sequence->uv[index].y,
            });
        }
        impl_->indices.reserve(impl_->indices.size() + sequence->num_indices);
        for (int index = 0; index < sequence->num_indices; ++index) {
            impl_->indices.push_back(base + static_cast<std::uint32_t>(sequence->indices[index]));
        }
        impl_->add_batch(Impl::Pipeline::text, sequence->atlas_texture, first,
                         static_cast<std::uint32_t>(sequence->num_indices));
    }
    TTF_DestroyText(text);
}

void GpuRenderer::present(Color clear_color) {
    if (impl_->vertices.size() > kMaxVertices || impl_->indices.size() > kMaxIndices) {
        throw std::runtime_error("GPU UI frame exceeded streaming buffer capacity");
    }

    auto* command_buffer = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (!command_buffer) {
        fail("acquire GPU command buffer");
    }
    SDL_GPUTexture* swapchain{};
    Uint32 swapchain_width{};
    Uint32 swapchain_height{};
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, impl_->window,
                                                &swapchain, &swapchain_width,
                                                &swapchain_height)) {
        fail("acquire swapchain texture");
    }
    if (!swapchain) {
        SDL_SubmitGPUCommandBuffer(command_buffer);
        return;
    }

    const auto vertex_bytes = impl_->vertices.size() * sizeof(Impl::Vertex);
    const auto index_bytes = impl_->indices.size() * sizeof(std::uint32_t);
    if (vertex_bytes + index_bytes > 0) {
        auto* mapped = static_cast<std::byte*>(
            SDL_MapGPUTransferBuffer(impl_->device, impl_->transfer_buffer, true));
        if (!mapped) {
            fail("map GPU transfer buffer");
        }
        std::memcpy(mapped, impl_->vertices.data(), vertex_bytes);
        std::memcpy(mapped + vertex_bytes, impl_->indices.data(), index_bytes);
        SDL_UnmapGPUTransferBuffer(impl_->device, impl_->transfer_buffer);

        auto* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
        const SDL_GPUTransferBufferLocation vertex_source{impl_->transfer_buffer, 0};
        const SDL_GPUBufferRegion vertex_destination{
            impl_->vertex_buffer, 0, static_cast<Uint32>(vertex_bytes)};
        SDL_UploadToGPUBuffer(copy_pass, &vertex_source, &vertex_destination, true);
        const SDL_GPUTransferBufferLocation index_source{
            impl_->transfer_buffer, static_cast<Uint32>(vertex_bytes)};
        const SDL_GPUBufferRegion index_destination{
            impl_->index_buffer, 0, static_cast<Uint32>(index_bytes)};
        SDL_UploadToGPUBuffer(copy_pass, &index_source, &index_destination, true);
        SDL_EndGPUCopyPass(copy_pass);
    }

    const SDL_GPUColorTargetInfo target{
        .texture = swapchain,
        .clear_color = {clear_color.r, clear_color.g, clear_color.b, clear_color.a},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE,
    };
    auto* render_pass = SDL_BeginGPURenderPass(command_buffer, &target, 1, nullptr);
    const SDL_GPUBufferBinding vertex_binding{impl_->vertex_buffer, 0};
    const SDL_GPUBufferBinding index_binding{impl_->index_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    const std::array<float, 4> viewport{
        static_cast<float>(swapchain_width), static_cast<float>(swapchain_height), 0.0F, 0.0F};
    SDL_PushGPUVertexUniformData(command_buffer, 0, viewport.data(), sizeof(viewport));

    Impl::Pipeline bound_pipeline = static_cast<Impl::Pipeline>(-1);
    SDL_GPUTexture* bound_texture{};
    for (const auto& batch : impl_->batches) {
        if (batch.pipeline != bound_pipeline) {
            SDL_BindGPUGraphicsPipeline(
                render_pass, batch.pipeline == Impl::Pipeline::solid
                                 ? impl_->solid_pipeline : impl_->text_pipeline);
            bound_pipeline = batch.pipeline;
        }
        if (batch.pipeline == Impl::Pipeline::text && batch.texture != bound_texture) {
            const SDL_GPUTextureSamplerBinding binding{batch.texture, impl_->sampler};
            SDL_BindGPUFragmentSamplers(render_pass, 0, &binding, 1);
            bound_texture = batch.texture;
        }
        SDL_DrawGPUIndexedPrimitives(render_pass, batch.index_count, 1,
                                     batch.first_index, 0, 0);
    }
    SDL_EndGPURenderPass(render_pass);
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        fail("submit GPU command buffer");
    }
}

} // namespace whoami::render
