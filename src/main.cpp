#define SDL_MAIN_USE_CALLBACKS  // use the callbacks instead of main()
#define GL_GLEXT_PROTOTYPES

#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengles2.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
#include "color_palette.hpp"

// All co-ordinates used are normalized as follows
// x: [0.0, 1.0]
// y: [0.0, 1/ASPECT_RATIO]
// origin at top-left

constexpr int SEQ_LEN = 4;
constexpr float ASPECT_RATIO = 16.f / 9.f;
constexpr float NORM_HEIGHT = 1.f / ASPECT_RATIO;

constexpr glm::vec4 BG_COLOR = Color::darkgrey;

constexpr float BUTTON_PANEL_WIDTH = 0.5f;
constexpr glm::vec4 BUTTON_LINE_COLOR = Color::white;
constexpr glm::vec4 BUTTON_FILL_COLOR = Color::blue;
float BUTTON_LINE_THICKNESS = 0.005f;
constexpr float BUTTON_RADIUS = 0.06f;

constexpr glm::vec4 FONT_FG = Color::yellow;
constexpr glm::vec4 FONT_FG2 = Color::yellow;
constexpr glm::vec4 FONT_BG = Color::transparent;
constexpr glm::vec4 FONT_OUTLINE = Color::white;
constexpr glm::vec4 FONT_OUTLINE2 = Color::white;
constexpr float FONT_OUTLINE_FACTOR = 0.0f;
constexpr float FONT_WIDTH = 0.15f;
constexpr float FONT_ENLARGE_SCALE = 1.3f;
const glm::vec2 FONT_OFFSET = {-0.02f, 0.05f};

constexpr float BOUNCE_ANIM_INITIAL_VEL = -0.25f;
constexpr float BOUNCE_ANIM_ACC = 1.f;
constexpr float BOUNCE_ANIM_DECAY = 0.75f;
constexpr float BOUNCE_ANIM_DURATION_SEC = 2.5f;

constexpr float GAME_DELAY_DURATION_SEC = 1.f;

enum class AudioEnum { BGM, CLICK, WIN };

struct AppState {
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_GLContext gl_ctx;
    SDL_AudioDeviceID audio_device = 0;

    std::map<AudioEnum, Audio> audio;

    VertexArrayPtr vao{{}, {}};
    Shape draw_area_bg;

    bool init = false;
    std::array<int, SEQ_LEN> number_sequence;
    std::array<bool, SEQ_LEN> number_done;

    FontAtlas font;
    FontShader font_shader;

    std::array<VertexBufferPtr, 10> number{
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
        VertexBufferPtr{{}, {}},
    };

    std::array<BBox, 10> number_bbox;

    ShapeShader shape_shader;
    Shape button;
    std::array<glm::vec2, 10> button_center;

    // time dependent events
    uint64_t bounce_anim_start = 0;
    uint64_t bounce_anim_end = 0;
    float bounce_vel = -1.f;

    uint64_t game_delay_end = 0;
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

    glm::vec2 draw_area_size;
    glm::vec2 draw_area_offset;

    if (win_w > win_h) {
        draw_area_size.y = win_hf;
        draw_area_size.x = win_hf * ASPECT_RATIO;
        draw_area_offset.x = (win_wf - draw_area_size.x) / 2;
        draw_area_offset.y = 0;
    } else {
        draw_area_size.x = win_wf;
        draw_area_size.y = win_wf / ASPECT_RATIO;
        draw_area_offset.x = 0;
        draw_area_offset.y = (win_hf - draw_area_size.y) / 2;
    }

    glViewport(0, 0, win_w, win_h);
    glm::mat4 ortho = glm::ortho(0.f, win_wf, win_hf, 0.f);

    float scale = draw_area_size.x;

    as.shape_shader.set_ortho(ortho);
    as.shape_shader.set_drawing_area_offset(draw_area_offset);
    as.shape_shader.set_screen_scale(scale);

    as.font_shader.set_ortho(ortho);
    as.font_shader.set_screen_scale(scale);
    as.font_shader.set_drawing_area_offset(draw_area_offset);

    return true;
}

void init_game(AppState &as) {
    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<> dice(0, 9);

    std::generate(as.number_sequence.begin(), as.number_sequence.end(), [&]{ return dice(g); });
    std::fill(as.number_done.begin(), as.number_done.end(), false);

    resize_event(as);
}

void mouse_up_event(AppState &as) {
    if (as.game_delay_end > 0) {
        return;
    }

    float cx = 0, cy = 0;
    SDL_GetMouseState(&cx, &cy);

    glm::vec2 pos = screen_pos_to_normalize_pos(as.shape_shader, {cx, cy});
    glm::vec2 radius{BUTTON_RADIUS, BUTTON_RADIUS};

    for (size_t i = 0; i < as.button_center.size(); i++) {
        const glm::vec2 &c = as.button_center[i];
        glm::vec2 start = c - radius;
        glm::vec2 end = c + radius;

        if ((pos.x > start.x) && (pos.x < end.x) && (pos.y > start.y) && (pos.y < end.y)) {
            as.audio[AudioEnum::CLICK].play();
            int num_click = static_cast<int>(i + 1) % 10;

            for (size_t j = 0; j < as.number_done.size(); j++) {
                if (!as.number_done[j]) {
                    if (num_click == as.number_sequence[j]) {
                        as.number_done[j] = true;
                        as.bounce_anim_start = 0;
                    }

                    break;
                }
            }

            break;
        }
    }

    // check if we wont
    auto is_true = [](bool b) { return b; };
    if (std::all_of(as.number_done.begin(), as.number_done.end(), is_true)) {
        as.audio[AudioEnum::WIN].play();
        as.game_delay_end = SDL_GetTicksNS() + SDL_SECONDS_TO_NS(GAME_DELAY_DURATION_SEC);
    }
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

    if (auto w = load_ogg(as.audio_device, (base_path + "switch30.ogg").c_str())) {
        as.audio[AudioEnum::CLICK] = *w;
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

    return true;
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

    if (!init_audio(*as, base_path)) {
        return SDL_APP_FAILURE;
    }

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

    for (size_t i = 0; i < as->number.size(); i++) {
        auto [vertex_buffer, bbox] = as->font.make_text(std::to_string(i).c_str(), true);
        as->number[i] = std::move(vertex_buffer);
        as->number_bbox[i] = bbox;
    }

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

        as->button =
            make_shape(vertex, BUTTON_LINE_THICKNESS, BUTTON_LINE_COLOR, BUTTON_FILL_COLOR);
    }

    // position for the src and dst shape
    float xdiv = 3 * 2;
    float ydiv = 4 * 2;

    size_t idx = 0;
    for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < 3; j++) {
            float x = static_cast<float>(2 * j + 1) / xdiv;
            float y = static_cast<float>(2 * i + 1) / ydiv;

            as->button_center[idx] = {x * BUTTON_PANEL_WIDTH, y * NORM_HEIGHT};
            idx++;
        }
    }

    as->button_center[9] = {(2 * 1 + 1) / xdiv * BUTTON_PANEL_WIDTH, (2 * 3 + 1) / ydiv * NORM_HEIGHT};

    init_game(*as);

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
            mouse_up_event(as);
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

    if (as.game_delay_end != 0 && SDL_GetTicksNS() > as.game_delay_end) {
        as.game_delay_end = 0;
        init_game(as);
    }

    auto &bgm = as.audio[AudioEnum::BGM];
    if (SDL_GetAudioStreamAvailable(bgm.stream) < static_cast<int>(bgm.data.size())) {
        bgm.play();
    }

#ifndef __EMSCRIPTEN__
    SDL_GL_MakeCurrent(as.window, as.gl_ctx);
#endif

    as.shape_shader.shader->use();

    if (!as.init) {
        resize_event(as);
        as.init = true;
    }

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    as.vao->use();

    float cx = 0, cy = 0;
    SDL_GetMouseState(&cx, &cy);

    draw_shape(as.shape_shader, as.draw_area_bg, true, false, false);

    as.font_shader.set_fg(FONT_FG);
    as.font_shader.set_bg(FONT_BG);
    as.font_shader.set_outline(FONT_OUTLINE);
    as.font_shader.set_outline_factor(FONT_OUTLINE_FACTOR);
    as.font_shader.set_font_target_width(FONT_WIDTH);

    size_t i = 0;
    for (const auto &center : as.button_center) {
        as.button.trans = center;
        draw_shape(as.shape_shader, as.button, true, true, false);

        glm::vec2 bbox_center = (as.number_bbox[i].start + as.number_bbox[i].end) * 0.5f;
        bbox_center -= FONT_OFFSET;
        bbox_center *= FONT_WIDTH;

        as.font_shader.set_trans(center - bbox_center);
        draw_vertex_buffer(as.font_shader.shader, as.number[(i + 1) % 10], as.font.tex);

        i++;
    }

    as.font_shader.set_bg(FONT_BG);
    as.font_shader.set_outline_factor(0.1f);

    float ydiv = 4 * 2;
    bool do_anim = true;

    for (size_t i = 0; i < as.number_sequence.size(); i++) {
        glm::vec2 pos{0.6 + static_cast<float>(i) * 0.1, NORM_HEIGHT * 3 / ydiv};

        int num = as.number_sequence[i];

        glm::vec2 bbox_center = (as.number_bbox[i].start + as.number_bbox[i].end) * 0.5f;
        bbox_center -= FONT_OFFSET;

        if (as.number_done[i]) {
            bbox_center *= FONT_WIDTH * FONT_ENLARGE_SCALE;

            as.font_shader.set_font_target_width(FONT_WIDTH * FONT_ENLARGE_SCALE);
            as.font_shader.set_fg(FONT_FG2);
            as.font_shader.set_outline(FONT_OUTLINE);
        } else {
            bbox_center *= FONT_WIDTH;

            as.font_shader.set_font_target_width(FONT_WIDTH);
            as.font_shader.set_fg(Color::transparent);
            as.font_shader.set_outline(FONT_OUTLINE2);

            if (do_anim) {
                if (as.bounce_anim_start == 0) {
                    as.bounce_anim_start = SDL_GetTicksNS();
                    as.bounce_anim_end = as.bounce_anim_start + SDL_SECONDS_TO_NS(BOUNCE_ANIM_DURATION_SEC);
                    as.bounce_vel = BOUNCE_ANIM_INITIAL_VEL;
                }

                float u = as.bounce_vel;
                float a = BOUNCE_ANIM_ACC;
                float t = static_cast<float>(static_cast<double>(SDL_GetTicksNS() - as.bounce_anim_start) * 1e-9);
                float d = u*t + a*t*t*0.5f;

                if (d > 0) {
                    d = 0;
                    as.bounce_vel *= BOUNCE_ANIM_DECAY;
                    as.bounce_anim_start = SDL_GetTicksNS();

                    if (SDL_GetTicksNS() > as.bounce_anim_end) {
                        as.bounce_vel = BOUNCE_ANIM_INITIAL_VEL;
                        as.bounce_anim_end = as.bounce_anim_start + SDL_SECONDS_TO_NS(BOUNCE_ANIM_DURATION_SEC);
                    }
                }

                pos.y += d;

                do_anim = false;
            }
        }

        as.font_shader.set_trans(pos - bbox_center);
        draw_vertex_buffer(as.font_shader.shader, as.number[static_cast<size_t>(num)], as.font.tex);
    }

    SDL_GL_SwapWindow(as.window);

    return SDL_APP_CONTINUE;
}

