#include <SDL3/SDL_events.h>
#define SDL_MAIN_USE_CALLBACKS  // use the callbacks instead of main()
#define GL_GLEXT_PROTOTYPES

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengles2.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <map>
#include <optional>
#include <random>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "audio.hpp"
#include "font.hpp"
#include "geometry.hpp"
#include "gl_helper.hpp"
#include "log.hpp"

constexpr float ASPECT_RATIO = 4.f / 3.f;
constexpr float NORM_HEIGHT = 1.f / ASPECT_RATIO;

const glm::vec4 BG_COLOR{0.3f, 0.3f, 0.3f, 1.f};
const glm::vec4 BUTTON_LINE_COLOR{1.f, 1.f, 1.f, 1.f};
const glm::vec4 BUTTON_FILL_COLOR{1.f, 0.f, 0.f, 1.f};
constexpr float BUTTON_RADIUS = 0.05f;
constexpr float BUTTON_LINE_THICKNESS = 0.01f;

const glm::vec4 FONT_FG{231 / 255.0, 202 / 255.0, 96 / 255.0, 1.0};
const glm::vec4 FONT_BG{0, 0, 0, 0};
const glm::vec4 FONT_OUTLINE{1, 1, 1, 1};
constexpr float FONT_OUTLINE_FACTOR = 0.1f;
constexpr float FONT_WIDTH = 0.2f;

std::map<std::string, glm::vec4> tableau10_palette() {
    const std::map<std::string, uint32_t> color{
        {"blue", 0x5778a4},
        {"orange", 0xe49444},
        {"red", 0xd1615d},
        {"teal", 0x85b6b2},
        {"green", 0x6a9f58},
        {"yellow", 0xe7ca60},
        {"purple", 0xa87c9f},
        {"pink", 0xf1a2a9},
        {"brown", 0x967662},
        {"grey", 0xb8b0ac},
    };

    std::map<std::string, glm::vec4> ret;

    for (auto it : color) {
        uint32_t c = it.second;
        uint8_t r = static_cast<uint8_t>(c >> 16);
        uint8_t g = (c >> 8) & 0xff;
        uint8_t b = c & 0xff;

        ret[it.first] = {r / 255.f, g / 255.f, b / 255.f, 1.0f};
    }

    return ret;
}

enum class AudioEnum { BGM, CORRECT, WIN };

struct AppState {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_GLContext gl_ctx;

    SDL_AudioDeviceID audio_device = 0;
    std::map<AudioEnum, Audio> audio;

    int score = 0;
    bool init = false;

    FontAtlas font;
    FontShader font_shader;

    // drawing area within the window
    glm::vec2 draw_area_offset;
    glm::vec2 draw_area_size;
    glm::vec2 draw_area_grid_size;
    Shape draw_area_bg;

    VertexArrayPtr vao{{}, {}};
    VertexBufferPtr score_vertex{{}, {}};
    BBox score_vertex_bbox;

    ShapeShader shape_shader;
    Shape button; 
    std::array<glm::vec2, 10> button_center;  // position for src shape, normalized units

    uint64_t last_tick = 0;
};

bool resize_event(AppState &as) {
    int win_w, win_h;

    if (!SDL_GetWindowSize(as.window, &win_w, &win_h)) {
        LOG("%s", SDL_GetError());
        return false;
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_canvas_element_size("#canvas", win_w, win_h);
#endif

    float win_wf = static_cast<float>(win_w);
    float win_hf = static_cast<float>(win_h);

    if (win_w > win_h) {
        as.draw_area_size.y = win_hf;
        as.draw_area_size.x = win_hf * ASPECT_RATIO;
        as.draw_area_offset.x = (win_wf - as.draw_area_size.x) / 2;
        as.draw_area_offset.y = 0;
    } else {
        as.draw_area_size.x = win_wf;
        as.draw_area_size.y = win_wf / ASPECT_RATIO;
        as.draw_area_offset.x = 0;
        as.draw_area_offset.y = (win_hf - as.draw_area_size.y) / 2;
    }

    glViewport(0, 0, win_w, win_h);
    glm::mat4 ortho = glm::ortho(0.f, win_wf, win_hf, 0.f);

    float scale = as.draw_area_size.x;

    as.shape_shader.set_ortho(ortho);
    as.shape_shader.set_drawing_area_offset(as.draw_area_offset);
    as.shape_shader.set_screen_scale(scale);

    as.font_shader.set_ortho(ortho);
    as.font_shader.set_screen_scale(scale);
    as.font_shader.set_drawing_area_offset(as.draw_area_offset);

    return true;
}

void init_game(AppState &as) {
    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_real_distribution<float> dice_binary(0, 1);

    resize_event(as);
}

bool init_audio(AppState &as, const std::string &base_path) {
    as.audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (as.audio_device == 0) {
        LOG("Couldn't open audio device: %s", SDL_GetError());
        return false;
    }

    if (auto w = load_ogg(as.audio_device, (base_path + "bgm.ogg").c_str(), 0.1f)) {
        as.audio[AudioEnum::BGM] = *w;
    } else {
        return false;
    }

    if (auto w = load_ogg(as.audio_device, (base_path + "win.ogg").c_str())) {
        as.audio[AudioEnum::WIN] = *w;
    } else {
        return false;
    }

    if (auto w = load_wav(as.audio_device, (base_path + "ding.wav").c_str())) {
        as.audio[AudioEnum::CORRECT] = *w;
    } else {
        return false;
    }

    return true;
}

bool init_font(AppState &as, const std::string &base_path) {
    if (!as.font.load(base_path + "atlas.bmp", base_path + "atlas.txt")) {
        return false;
    }

    if (!as.font_shader.init(as.font)) {
        return false;
    }

    as.font_shader.set_font_distance_range(static_cast<float>(as.font.distance_range));
    as.font_shader.set_font_grid_width(static_cast<float>(as.font.grid_width));
    as.font_shader.set_font_target_width(FONT_WIDTH);

    as.font_shader.set_fg(FONT_FG);
    as.font_shader.set_bg(FONT_BG);
    as.font_shader.set_outline(FONT_OUTLINE);
    as.font_shader.set_outline_factor(FONT_OUTLINE_FACTOR);

    return true;
}

void update_score_text(AppState &as) {
    auto [vertex, index] = as.font.make_text_vertex(std::to_string(as.score), true);
    as.score_vertex_bbox = bbox(vertex);
    as.score_vertex->update_vertex(
        glm::value_ptr(vertex[0]), sizeof(decltype(vertex)::value_type) * vertex.size(), index);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    // Unused
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        LOG("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState *as = new AppState();

    if (!as) {
        LOG("can't alloc memory for AppState");
        return SDL_APP_FAILURE;
    }

    *appstate = as;

    std::string base_path = "assets/";
#ifdef __ANDROID__
    base_path = "";
#endif

    // if (!init_audio(*as, base_path)) {
    //     return SDL_APP_FAILURE;
    // }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);

    if (!SDL_CreateWindowAndRenderer(
            "Shape Game", 640, 480, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL, &as->window, &as->renderer)) {
        LOG("SDL_CreateWindowAndRenderer failed");
        return SDL_APP_FAILURE;
    }

    if (!SDL_SetRenderVSync(as->renderer, 1)) {
        LOG("SDL_SetRenderVSync failed");
        return SDL_APP_FAILURE;
    }

#ifndef __EMSCRIPTEN__
    as->gl_ctx = SDL_GL_CreateContext(as->window);
    SDL_GL_MakeCurrent(as->window, as->gl_ctx);
    enable_gl_debug_callback();
#endif

    if (!init_font(*as, base_path)) {
        return SDL_APP_FAILURE;
    }

    // pre-allocate all vertex we need
    // number of space needs to be >= MAX_SCORE string
    as->score_vertex = as->font.make_text("    ", true);
    update_score_text(*as);

    if (!as->shape_shader.init()) {
        return SDL_APP_FAILURE;
    }

    as->vao = make_vertex_array();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // background color for drawing area
    {
        float h = 1.0f / ASPECT_RATIO;

        std::vector<glm::vec2> vertex{
            {0.f, 0.f},
            {1.f, 0.f},
            {1.f, h},
            {0.f, h},
        };

        as->draw_area_bg = make_shape(vertex, 0, {}, BG_COLOR);
    }

    {
        std::vector<glm::vec2> vertex{
            {-BUTTON_RADIUS, -BUTTON_RADIUS},
            {BUTTON_RADIUS, -BUTTON_RADIUS},
            {BUTTON_RADIUS, BUTTON_RADIUS},
            {-BUTTON_RADIUS, BUTTON_RADIUS},
        };

        as->button = make_shape(vertex, BUTTON_LINE_THICKNESS, BUTTON_LINE_COLOR, BUTTON_FILL_COLOR);
    }

    // position for the src and dst shape
    float xdiv = 3 * 2;
    float ydiv = 4 * 2;

    size_t idx =0;
    for (size_t i = 0; i < 3; i++) {
        for (size_t j=0; j < 3; j++) {
            float x = (2*j+1) / xdiv; 
            float y = (2*i+1) / ydiv; 

            as->button_center[idx] = {x, y * NORM_HEIGHT};
            idx++;
        }
    }

    as->button_center[9] = {(2*1 + 1)/xdiv, (2*3+1)/ydiv * NORM_HEIGHT}; 

    init_game(*as);

    as->last_tick = SDL_GetTicks();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState &as = *static_cast<AppState *>(appstate);

    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
#ifndef __EMSCRIPTEN__
            if (event->key.key == SDLK_ESCAPE) {
                SDL_Quit();
                return SDL_APP_SUCCESS;
            }
#endif
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            resize_event(as);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            break;

        case SDL_EVENT_MOUSE_MOTION:
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            break;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // Unused
    (void)result;

    if (appstate) {
        AppState &as = *static_cast<AppState *>(appstate);
        SDL_DestroyRenderer(as.renderer);
        SDL_DestroyWindow(as.window);

        SDL_CloseAudioDevice(as.audio_device);

        // TODO: This code causes a crash as of libSDL preview-3.1.6
        // for (auto &a: as.audio) {
        //     if (a.second.stream) {
        //         SDL_DestroyAudioStream(a.second.stream);
        //     }
        // }

        delete &as;
    }
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState &as = *static_cast<AppState *>(appstate);

    float dt = static_cast<float>(SDL_GetTicksNS() - as.last_tick) * 1e-9f;
    as.last_tick = SDL_GetTicksNS();

    // auto &bgm = as.audio[AudioEnum::BGM];
    // if (SDL_GetAudioStreamAvailable(bgm.stream) < static_cast<int>(bgm.data.size())) {
    //     bgm.play();
    // }

#ifndef __EMSCRIPTEN__
    SDL_GL_MakeCurrent(as.window, as.gl_ctx);
#endif

    as.shape_shader.shader->use();

    if (!as.init) {
        resize_event(as);
        as.init = true;
    }

    // glDisable(GL_DEPTH_TEST);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    as.vao->use();

    float cx = 0, cy = 0;
    SDL_GetMouseState(&cx, &cy);

    draw_shape(as.shape_shader, as.draw_area_bg, true, false, false);
    
    for (const auto &center: as.button_center) {
        as.button.trans = center;
        draw_shape(as.shape_shader, as.button, true, false, false);
    }

    // if (as.score > 0) {
    //     // draw the score in the middle of the drawing area
    //     const BBox &bbox = as.score_vertex_bbox;
    //
    //     glm::vec2 text_center = (bbox.start + bbox.end) * 0.5f * FONT_WIDTH;
    //     glm::vec2 trans = glm::vec2{0.5f, NORM_HEIGHT * 0.5f} - text_center;
    //
    //     as.font_shader.set_trans(trans);
    //     draw_vertex_buffer(as.font_shader.shader, as.score_vertex, as.font.tex);
    // }

    SDL_GL_SwapWindow(as.window);

    return SDL_APP_CONTINUE;
}
