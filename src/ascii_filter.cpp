// Graphics thread (video_render): the filter target is rendered into a texture,
// read back through two staging surfaces (the one mapped was staged a frame earlier,
// so the GPU is not stalled) and handed to a worker thread. The worker runs the
// face tracker and the ASCII renderer on the CPU; its latest result is uploaded into
// a dynamic texture and drawn instead of the source. A slow frame on the worker only
// lowers the effect's frame rate, never OBS's.

#include "ascii_filter.h"

#include "ascii_renderer.h"
#include "face_tracker.h"
#include "log.h"
#include "platform.h"
#include "settings.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec4.h>

#include <windows.h>
#include <objbase.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

constexpr const char* kFilterId  = "blackcamera_ascii_filter";
constexpr const char* kModelFile = "face_detection_yunet_2023mar.onnx";

// Property keys
constexpr const char* P_BRIGHTNESS     = "brightness";
constexpr const char* P_CONTRAST       = "contrast";
constexpr const char* P_BLUR           = "blur_size";
constexpr const char* P_MIN_BRIGHTNESS = "min_brightness";
constexpr const char* P_COLOR_MODE     = "color_mode";
constexpr const char* P_COLOR          = "custom_color";
constexpr const char* P_ANTIALIAS      = "antialias";
constexpr const char* P_CHARSET        = "charset";
constexpr const char* P_CUSTOM_CHARSET = "custom_charset";
constexpr const char* P_FONT           = "font_size";      // Settings::minFont
constexpr const char* P_SCALE_FONT     = "scale_font";
constexpr const char* P_FACE_ZOOM      = "face_zoom";      // checkable group
constexpr const char* P_MAX_FONT       = "max_font";
constexpr const char* P_ZOOM_SPEED     = "zoom_speed";
constexpr const char* P_SHOW_HUD       = "show_hud";
constexpr const char* P_HYSTERESIS     = "hysteresis";
constexpr const char* P_MAX_OCCUPANCY  = "max_occupancy";
constexpr const char* P_MIN_OCCUPANCY  = "min_occupancy";

const char* T(const char* key) { return obs_module_text(key); }

// OBS colors are 0xAABBGGRR
uint32_t toObsColor(const std::array<float, 3>& rgb) {
    auto c = [](float v) { return (uint32_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); };
    return 0xFF000000u | (c(rgb[2]) << 16) | (c(rgb[1]) << 8) | c(rgb[0]);
}
std::array<float, 3> fromObsColor(uint32_t v) {
    return { (v & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, ((v >> 16) & 0xFF) / 255.0f };
}

struct AsciiFilter {
    explicit AsciiFilter(obs_source_t* source) : context(source) {}

    obs_source_t* context;
    SettingsStore settings{ Settings{} };
    std::string modelPath; // UTF-8, empty if the model is missing

    // ── Graphics thread only ────────────────────────────────────────────────
    gs_texrender_t* texrender = nullptr;
    gs_stagesurf_t* stage[2] = {};
    bool stageFilled[2] = {};
    int stageIndex = 0;
    uint32_t stageW = 0, stageH = 0;
    gs_texture_t* output = nullptr;
    uint32_t outputW = 0, outputH = 0;
    bool captureThisFrame = false; // one readback per frame even if the source is shown twice

    // ── Shared with the worker (mutex) ──────────────────────────────────────
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    cv::Mat input;  bool inputFresh = false;  // BGRA, from the graphics thread
    cv::Mat result; bool resultFresh = false; // BGRA, from the worker
};

// ── Worker ──────────────────────────────────────────────────────────────────

void workerLoop(AsciiFilter* f) {
    // WIC (used by the glyph atlas) needs COM on this thread
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    FaceTracker tracker;
    if (!f->modelPath.empty()) tracker.load(platform::widen(f->modelPath));

    AsciiRenderer renderer;
    bool rendererReady = false, rendererFailed = false;
    cv::Mat frame, bgr, bgra;

    for (;;) {
        {
            std::unique_lock lock(f->mutex);
            f->wake.wait(lock, [f] { return f->stopping || f->inputFresh; });
            if (f->stopping) break;
            std::swap(frame, f->input); // the old buffer is reused for the next readback
            f->inputFresh = false;
        }
        if (rendererFailed) continue;

        if (!rendererReady) {
            rendererReady = renderer.init(frame.cols, frame.rows);
            if (!rendererReady) {
                LOG_ERROR("Failed to init ASCII renderer (Direct2D/DirectWrite/WIC)");
                rendererFailed = true;
                continue;
            }
        } else if (renderer.width() != frame.cols || renderer.height() != frame.rows) {
            renderer.resize(frame.cols, frame.rows);
        }

        Settings s = f->settings.get();
        if (s.scaleFont) {
            // Font sizes are set for a 1080-px-high picture
            const float k = frame.rows / 1080.0f;
            s.minFont *= k;
            s.maxFont *= k;
        }
        cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
        const float fontSize = tracker.update(bgr, true, s, bgr.cols, bgr.rows);
        const cv::Mat ascii = renderer.render(bgr, fontSize, s, s.charset(), tracker.hud());
        cv::cvtColor(ascii, bgra, cv::COLOR_BGR2BGRA);

        std::lock_guard lock(f->mutex);
        std::swap(f->result, bgra);
        f->resultFresh = true;
    }

    if (com) CoUninitialize();
}

// ── Graphics thread ─────────────────────────────────────────────────────────

void destroyStages(AsciiFilter* f) {
    for (int i = 0; i < 2; ++i) {
        gs_stagesurface_destroy(f->stage[i]);
        f->stage[i] = nullptr;
        f->stageFilled[i] = false;
    }
    f->stageW = f->stageH = 0;
}

// Renders the filter target and passes the frame staged on the previous call to the worker.
// The target is rendered in SDR sRGB: OBS tone-maps HDR sources into it, so the CPU
// always gets 8-bit sRGB pixels.
void captureTarget(AsciiFilter* f, obs_source_t* target, uint32_t w, uint32_t h) {
    if (!f->texrender) f->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    gs_texrender_reset(f->texrender);
    if (!gs_texrender_begin_with_color_space(f->texrender, w, h, GS_CS_SRGB)) return;
    vec4 clear;
    vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
    gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    obs_source_video_render(target);
    gs_blend_state_pop();
    gs_texrender_end(f->texrender);

    if (f->stageW != w || f->stageH != h) {
        destroyStages(f);
        for (int i = 0; i < 2; ++i) f->stage[i] = gs_stagesurface_create(w, h, GS_BGRA);
        f->stageW = w;
        f->stageH = h;
    }

    const int cur = f->stageIndex, prev = cur ^ 1;
    gs_stage_texture(f->stage[cur], gs_texrender_get_texture(f->texrender));
    f->stageFilled[cur] = true;
    f->stageIndex = prev;
    if (!f->stageFilled[prev]) return;

    uint8_t* data = nullptr;
    uint32_t linesize = 0;
    if (!gs_stagesurface_map(f->stage[prev], &data, &linesize)) return;
    {
        std::lock_guard lock(f->mutex);
        f->input.create((int)h, (int)w, CV_8UC4);
        for (uint32_t y = 0; y < h; ++y) memcpy(f->input.ptr(y), data + (size_t)y * linesize, (size_t)w * 4);
        f->inputFresh = true;
    }
    gs_stagesurface_unmap(f->stage[prev]);
    f->wake.notify_one();
}

void uploadResult(AsciiFilter* f) {
    std::lock_guard lock(f->mutex);
    if (!f->resultFresh) return;
    f->resultFresh = false;
    const uint32_t w = (uint32_t)f->result.cols, h = (uint32_t)f->result.rows;
    if (!f->output || f->outputW != w || f->outputH != h) {
        gs_texture_destroy(f->output);
        f->output = gs_texture_create(w, h, GS_BGRA, 1, nullptr, GS_DYNAMIC);
        f->outputW = w;
        f->outputH = h;
    }
    if (f->output) gs_texture_set_image(f->output, f->result.data, (uint32_t)f->result.step, false);
}

// ── obs_source_info callbacks ───────────────────────────────────────────────

const char* filterName(void*) { return T("BlackCameraAscii"); }

void filterUpdate(void* data, obs_data_t* d) {
    auto* f = static_cast<AsciiFilter*>(data);
    Settings s;
    s.brightness    = (int)obs_data_get_int(d, P_BRIGHTNESS);
    s.contrast      = (float)obs_data_get_double(d, P_CONTRAST);
    s.blurSize      = (int)obs_data_get_int(d, P_BLUR);
    s.minBrightness = (int)obs_data_get_int(d, P_MIN_BRIGHTNESS);
    s.colorMode     = (int)obs_data_get_int(d, P_COLOR_MODE);
    s.color         = fromObsColor((uint32_t)obs_data_get_int(d, P_COLOR));
    s.antialiasMode = (int)obs_data_get_int(d, P_ANTIALIAS);
    s.charsetPreset = (int)obs_data_get_int(d, P_CHARSET);
    s.customCharset = obs_data_get_string(d, P_CUSTOM_CHARSET);
    s.minFont       = (float)obs_data_get_double(d, P_FONT);
    s.scaleFont     = obs_data_get_bool(d, P_SCALE_FONT);
    s.faceZoom      = obs_data_get_bool(d, P_FACE_ZOOM);
    s.maxFont       = (float)obs_data_get_double(d, P_MAX_FONT);
    s.zoomSpeed     = (float)obs_data_get_double(d, P_ZOOM_SPEED);
    s.showHud       = obs_data_get_bool(d, P_SHOW_HUD);
    s.hysteresis    = (float)obs_data_get_double(d, P_HYSTERESIS);
    s.maxOccupancy  = (float)obs_data_get_double(d, P_MAX_OCCUPANCY);
    s.minOccupancy  = (float)obs_data_get_double(d, P_MIN_OCCUPANCY);
    s.clamp();
    f->settings.set(s);
}

void filterDefaults(obs_data_t* d) {
    const Settings s;
    obs_data_set_default_int(d, P_BRIGHTNESS, s.brightness);
    obs_data_set_default_double(d, P_CONTRAST, s.contrast);
    obs_data_set_default_int(d, P_BLUR, s.blurSize);
    obs_data_set_default_int(d, P_MIN_BRIGHTNESS, s.minBrightness);
    obs_data_set_default_int(d, P_COLOR_MODE, s.colorMode);
    obs_data_set_default_int(d, P_COLOR, toObsColor(s.color));
    obs_data_set_default_int(d, P_ANTIALIAS, s.antialiasMode);
    obs_data_set_default_int(d, P_CHARSET, s.charsetPreset);
    obs_data_set_default_string(d, P_CUSTOM_CHARSET, s.customCharset.c_str());
    obs_data_set_default_double(d, P_FONT, s.minFont);
    obs_data_set_default_bool(d, P_SCALE_FONT, s.scaleFont);
    obs_data_set_default_bool(d, P_FACE_ZOOM, s.faceZoom);
    obs_data_set_default_double(d, P_MAX_FONT, s.maxFont);
    obs_data_set_default_double(d, P_ZOOM_SPEED, s.zoomSpeed);
    obs_data_set_default_bool(d, P_SHOW_HUD, s.showHud);
    obs_data_set_default_double(d, P_HYSTERESIS, s.hysteresis);
    obs_data_set_default_double(d, P_MAX_OCCUPANCY, s.maxOccupancy);
    obs_data_set_default_double(d, P_MIN_OCCUPANCY, s.minOccupancy);
}

bool colorModeChanged(obs_properties_t* props, obs_property_t*, obs_data_t* d) {
    obs_property_set_visible(obs_properties_get(props, P_COLOR), obs_data_get_int(d, P_COLOR_MODE) == 0);
    return true;
}

bool charsetChanged(obs_properties_t* props, obs_property_t*, obs_data_t* d) {
    const int preset = (int)obs_data_get_int(d, P_CHARSET);
    const bool custom = preset >= 0 && preset < kCharsetPresetCount && !kCharsetPresets[preset].chars;
    obs_property_set_visible(obs_properties_get(props, P_CUSTOM_CHARSET), custom);
    return true;
}

obs_properties_t* filterProperties(void*) {
    obs_properties_t* props = obs_properties_create();

    obs_properties_add_int_slider(props, P_BRIGHTNESS, T("Brightness"), -100, 100, 1);
    obs_properties_add_float_slider(props, P_CONTRAST, T("Contrast"), 0.1, 3.0, 0.05);
    obs_properties_add_int_slider(props, P_BLUR, T("BlurSize"), 1, 31, 2);
    obs_properties_add_int_slider(props, P_MIN_BRIGHTNESS, T("MinBrightness"), 0, 255, 1);

    obs_property_t* mode = obs_properties_add_list(props, P_COLOR_MODE, T("ColorMode"),
                                                   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    const char* modes[] = { "ColorMode.Custom", "ColorMode.Camera", "ColorMode.Matrix",
                            "ColorMode.Amber", "ColorMode.Cyberpunk", "ColorMode.Rainbow" };
    for (int i = 0; i < (int)std::size(modes); ++i) obs_property_list_add_int(mode, T(modes[i]), i);
    obs_property_set_modified_callback(mode, colorModeChanged);
    obs_properties_add_color(props, P_COLOR, T("CustomColor"));

    obs_property_t* aa = obs_properties_add_list(props, P_ANTIALIAS, T("Antialiasing"),
                                                 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    const char* aaModes[] = { "Antialiasing.Default", "Antialiasing.ClearType",
                              "Antialiasing.Grayscale", "Antialiasing.Aliased" };
    for (int i = 0; i < (int)std::size(aaModes); ++i) obs_property_list_add_int(aa, T(aaModes[i]), i);

    obs_property_t* charset = obs_properties_add_list(props, P_CHARSET, T("Charset"),
                                                      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    for (int i = 0; i < kCharsetPresetCount; ++i) {
        const std::string key = "Charset." + std::to_string(i);
        obs_property_list_add_int(charset, T(key.c_str()), i);
    }
    obs_property_set_modified_callback(charset, charsetChanged);
    obs_properties_add_text(props, P_CUSTOM_CHARSET, T("CustomCharset"), OBS_TEXT_DEFAULT);

    obs_properties_add_float_slider(props, P_FONT, T("FontSize"), 5.0, 150.0, 0.5);
    obs_property_t* scale = obs_properties_add_bool(props, P_SCALE_FONT, T("ScaleFont"));
    obs_property_set_long_description(scale, T("ScaleFont.Description"));

    obs_properties_t* zoom = obs_properties_create();
    obs_properties_add_float_slider(zoom, P_MAX_FONT, T("MaxFont"), 30.0, 150.0, 0.5);
    obs_properties_add_float_slider(zoom, P_ZOOM_SPEED, T("ZoomSpeed"), 0.01, 1.0, 0.01);
    obs_properties_add_bool(zoom, P_SHOW_HUD, T("ShowHud"));
    obs_properties_add_float_slider(zoom, P_HYSTERESIS, T("Hysteresis"), 0.0, 0.3, 0.005);
    obs_properties_add_float_slider(zoom, P_MAX_OCCUPANCY, T("MaxOccupancy"), 0.05, 0.6, 0.005);
    obs_properties_add_float_slider(zoom, P_MIN_OCCUPANCY, T("MinOccupancy"), 0.005, 0.2, 0.001);
    obs_properties_add_group(props, P_FACE_ZOOM, T("FaceZoom"), OBS_GROUP_CHECKABLE, zoom);

    return props;
}

void* filterCreate(obs_data_t* settings, obs_source_t* source) {
    auto* f = new AsciiFilter(source);
    if (char* model = obs_module_file(kModelFile)) {
        f->modelPath = model;
        bfree(model);
    } else {
        LOG_WARN("%s not found in the plugin data folder, face zoom disabled", kModelFile);
    }
    filterUpdate(f, settings);
    f->worker = std::thread(workerLoop, f);
    return f;
}

void filterDestroy(void* data) {
    auto* f = static_cast<AsciiFilter*>(data);
    {
        std::lock_guard lock(f->mutex);
        f->stopping = true;
    }
    f->wake.notify_one();
    if (f->worker.joinable()) f->worker.join();

    obs_enter_graphics();
    gs_texrender_destroy(f->texrender);
    destroyStages(f);
    gs_texture_destroy(f->output);
    obs_leave_graphics();
    delete f;
}

void filterTick(void* data, float) {
    static_cast<AsciiFilter*>(data)->captureThisFrame = true;
}

void filterRender(void* data, gs_effect_t*) {
    auto* f = static_cast<AsciiFilter*>(data);
    obs_source_t* target = obs_filter_get_target(f->context);
    obs_source_t* parent = obs_filter_get_parent(f->context);
    const uint32_t w = target ? obs_source_get_base_width(target) : 0;
    const uint32_t h = target ? obs_source_get_base_height(target) : 0;
    if (!parent || !w || !h) {
        obs_source_skip_video_filter(f->context);
        return;
    }

    if (f->captureThisFrame) {
        f->captureThisFrame = false;
        captureTarget(f, target, w, h);
    }
    uploadResult(f);

    if (!f->output) { // the first frames, until the worker delivers
        obs_source_skip_video_filter(f->context);
        return;
    }
    // The result holds sRGB-encoded pixels: sample them linearized and let the framebuffer
    // encode again, which is exact for 8-bit targets and correct for 16-bit (linear) ones.
    // OBS converts the SRGB output to an HDR canvas itself (filterColorSpace).
    const bool previousSrgb = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(true);
    gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect, "image"), f->output);
    while (gs_effect_loop(effect, "Draw")) gs_draw_sprite(f->output, 0, w, h);
    gs_enable_framebuffer_srgb(previousSrgb);
}

// The effect output is SDR sRGB. Until the first result the source is passed through,
// so its own color space applies.
gs_color_space filterColorSpace(void* data, size_t count, const gs_color_space* preferred) {
    auto* f = static_cast<AsciiFilter*>(data);
    if (f->output) return GS_CS_SRGB;
    obs_source_t* target = obs_filter_get_target(f->context);
    return target ? obs_source_get_color_space(target, count, preferred) : GS_CS_SRGB;
}

} // namespace

void registerAsciiFilter() {
    static obs_source_info info = {};
    info.id             = kFilterId;
    info.type           = OBS_SOURCE_TYPE_FILTER;
    info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
    info.get_name       = filterName;
    info.create         = filterCreate;
    info.destroy        = filterDestroy;
    info.update         = filterUpdate;
    info.get_defaults   = filterDefaults;
    info.get_properties = filterProperties;
    info.video_tick     = filterTick;
    info.video_render   = filterRender;
    info.video_get_color_space = filterColorSpace;
    obs_register_source(&info);
}
