#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "life_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------- globale Variablen ------------------- */
static int   paused        = 0;
static float offset_x      = 0;
static float offset_y      = 0;
static float scale         = 1.0f;
static int   dragging      = 0;
static int   last_mouse_x  = 0;
static int   last_mouse_y  = 0;
static int   window_width  = 1024;
static int   window_height = 1024;

static int   density_pct   = 25;
static int   sim_hz        = 10;

/* Eingabemodus: 0 = normal, 1 = density, 2 = speed */
static int   input_mode    = 0;
static char  input_buf[8]  = {0};
static int   input_len     = 0;

static SDL_Window *g_win   = NULL;

#define MIN_SCALE 0.2f
#define MAX_SCALE 64.0f
#define MIN_SIM_HZ 1
#define MAX_SIM_HZ 500

#define COLOR_BACKGROUND  0xFF000000
#define COLOR_CELL        0xFF00FF41

static void clamp_offset(void) {
    float wsw = WORLD_SIZE * scale;
    float wsh = WORLD_SIZE * scale;

    if (wsw <= window_width) {
        offset_x = -(window_width - wsw) / 2.0f;
    } else {
        if (offset_x < 0) offset_x = 0;
        if (offset_x > wsw - window_width) offset_x = wsw - window_width;
    }

    if (wsh <= window_height) {
        offset_y = -(window_height - wsh) / 2.0f;
    } else {
        if (offset_y < 0) offset_y = 0;
        if (offset_y > wsh - window_height) offset_y = wsh - window_height;
    }
}

static void update_title(void) {
    char title[256];
    if (input_mode == 1) {
        snprintf(title, sizeof(title),
            "Game of Life  |  Set Density (1-100): %s_  (Enter=OK  Esc=Cancel)",
            input_buf);
    } else if (input_mode == 2) {
        snprintf(title, sizeof(title),
            "Game of Life  |  Set UPS (1-500): %s_  (Enter=OK  Esc=Cancel)",
            input_buf);
    } else {
        snprintf(title, sizeof(title),
            "Game of Life  |  Density: %d%%  |  UPS: %d  |  [D]=Density  [S]=Speed  [W]=Randomize [Space]=Pause  [R]=Reset  [Up/Down]=Speed",
            density_pct, sim_hz);
    }
    SDL_SetWindowTitle(g_win, title);
}

static void do_randomize(void) {
    engine_stop_threads();
    engine_destroy();
    engine_init(density_pct);
    engine_start_threads();
    paused = 0;
}

static SDL_Texture *create_texture(SDL_Renderer *ren, int w, int h) {
    SDL_Texture *t = SDL_CreateTexture(ren,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        w, h);
    if (!t) fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
    return t;
}

static void handle_zoom(int wheel_y, int mouse_x, int mouse_y) {
    if (wheel_y == 0) return;

    float factor    = (wheel_y > 0) ? 1.1f : (1.0f / 1.1f);
    float new_scale = scale * factor;
    if (new_scale < MIN_SCALE) new_scale = MIN_SCALE;
    if (new_scale > MAX_SCALE) new_scale = MAX_SCALE;

    float world_x = (mouse_x + offset_x) / scale;
    float world_y = (mouse_y + offset_y) / scale;

    scale    = new_scale;
    offset_x = world_x * scale - mouse_x;
    offset_y = world_y * scale - mouse_y;

    clamp_offset();
}

static int speed_up(int hz) {
    int next = (int)(hz * 1.25f + 0.5f);
    if (next <= hz) next = hz + 1;
    if (next > MAX_SIM_HZ) next = MAX_SIM_HZ;
    return next;
}

static int speed_down(int hz) {
    int next = (int)(hz / 1.25f + 0.5f);
    if (next >= hz) next = hz - 1;
    if (next < MIN_SIM_HZ) next = MIN_SIM_HZ;
    return next;
}

static void handle_text_input(SDL_Event *e) {
    if (!input_mode) return;
    char c = e->text.text[0];
    if (c >= '0' && c <= '9' && input_len < 5) {
        input_buf[input_len++] = c;
        input_buf[input_len]   = '\0';
        update_title();
    }
}

static void handle_key_down(SDL_Keysym key) {
    if (input_mode) {
        switch (key.sym) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER: {
                int val = atoi(input_buf);
                if (input_mode == 1) {
                    if (val < 1)   val = 1;
                    if (val > 100) val = 100;
                    density_pct = val;
                    do_randomize();
                } else if (input_mode == 2) {
                    if (val < MIN_SIM_HZ) val = MIN_SIM_HZ;
                    if (val > MAX_SIM_HZ) val = MAX_SIM_HZ;
                    sim_hz = val;
                }
                input_mode = 0;
                input_len  = 0;
                input_buf[0] = '\0';
                update_title();
                break;
            }
            case SDLK_BACKSPACE:
                if (input_len > 0) {
                    input_buf[--input_len] = '\0';
                    update_title();
                }
                break;
            case SDLK_ESCAPE:
                input_mode   = 0;
                input_len    = 0;
                input_buf[0] = '\0';
                update_title();
                break;
            default: break;
        }
    } else {
        switch (key.sym) {
            case SDLK_SPACE:
                paused = !paused;
                update_title();
                break;
            case SDLK_d:
                input_mode = 1;
                input_len = 0;
                input_buf[0] = '\0';
                SDL_StartTextInput();
                update_title();
                break;
            case SDLK_s:
                input_mode = 2;
                input_len = 0;
                input_buf[0] = '\0';
                SDL_StartTextInput();
                update_title();
                break;
            case SDLK_w:
                do_randomize();
                update_title();
                break;
            case SDLK_r:
                do_randomize();
                offset_x = offset_y = 0;
                scale = 4.0f;
                clamp_offset();
                update_title();
                break;
            case SDLK_UP:
                sim_hz = speed_up(sim_hz);
                update_title();
                break;
            case SDLK_DOWN:
                sim_hz = speed_down(sim_hz);
                update_title();
                break;
            case SDLK_PLUS:
            case SDLK_KP_PLUS:
                handle_zoom(1,  window_width / 2, window_height / 2);
                break;
            case SDLK_MINUS:
            case SDLK_KP_MINUS:
                handle_zoom(-1, window_width / 2, window_height / 2);
                break;
            default: break;
        }
    }
}

static void render_world(SDL_Renderer *ren, SDL_Texture *tex) {
    void *pixels_raw;
    int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels_raw, &pitch) != 0) {
        fprintf(stderr, "SDL_LockTexture: %s\n", SDL_GetError());
        return;
    }

    Uint32 *pixels  = (Uint32*)pixels_raw;
    int     pitch32 = pitch / 4;

    for (int i = 0; i < pitch32 * window_height; i++) {
        pixels[i] = COLOR_BACKGROUND;
    }

    if (scale >= 4.0f) {
        Uint32 grid_color = 0xFF1F3F1F;

        int first_cell_x = (int)(offset_x / scale);
        for (int cx = first_cell_x; ; cx++) {
            int sx = (int)(cx * scale - offset_x);
            if (sx >= window_width) break;
            if (sx < 0 || sx >= window_width) continue;
            for (int sy = 0; sy < window_height; sy++) {
                pixels[sy * pitch32 + sx] = grid_color;
            }
        }

        int first_cell_y = (int)(offset_y / scale);
        for (int cy = first_cell_y; ; cy++) {
            int sy = (int)(cy * scale - offset_y);
            if (sy >= window_height) break;
            if (sy < 0 || sy >= window_height) continue;
            for (int sx = 0; sx < window_width; sx++) {
                pixels[sy * pitch32 + sx] = grid_color;
            }
        }
    }

    for (int cy = 0; cy < CHUNK_COUNT; cy++) {
        float chunk_screen_start_y = cy * CHUNK_SIZE * scale - offset_y;
        float chunk_screen_end_y   = chunk_screen_start_y + CHUNK_SIZE * scale;
        if (chunk_screen_end_y < 0) continue;
        if (chunk_screen_start_y > window_height) continue;

        for (int cx = 0; cx < CHUNK_COUNT; cx++) {
            if (!active_chunk[cy][cx]) continue;

            float chunk_screen_start_x = cx * CHUNK_SIZE * scale - offset_x;
            float chunk_screen_end_x   = chunk_screen_start_x + CHUNK_SIZE * scale;
            if (chunk_screen_end_x < 0) continue;
            if (chunk_screen_start_x > window_width) continue;

            int world_y_start = cy * CHUNK_SIZE;
            int world_y_end   = world_y_start + CHUNK_SIZE;
            int world_x_start = cx * CHUNK_SIZE;
            int world_x_end   = world_x_start + CHUNK_SIZE;

            for (int y = world_y_start; y < world_y_end; y++) {
                int screen_y = (int)(y * scale - offset_y);
                if (screen_y < 0 || screen_y >= window_height) continue;

                for (int x = world_x_start; x < world_x_end; x++) {
                    if (!engine_get_cell(y, x)) continue;

                    int screen_x = (int)(x * scale - offset_x);
                    if (screen_x < 0 || screen_x >= window_width) continue;

                    int cell_px = (scale >= 4.0f) ? (int)scale - 1 : (int)scale;
                    if (cell_px < 1) cell_px = 1;

                    for (int py = 0; py < cell_px; py++) {
                        int sy = screen_y + py;
                        if (sy >= window_height) break;
                        for (int px2 = 0; px2 < cell_px; px2++) {
                            int sx = screen_x + px2;
                            if (sx >= window_width) break;
                            pixels[sy * pitch32 + sx] = COLOR_CELL;
                        }
                    }
                }
            }
        }
    }

    SDL_UnlockTexture(tex);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Initializing %dx%d world... density=%d%%\n", WORLD_SIZE, WORLD_SIZE, density_pct);
    engine_init(density_pct);
    engine_start_threads();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        engine_stop_threads();
        engine_destroy();
        return 1;
    }

    g_win = SDL_CreateWindow(
        "Game of Life",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        engine_stop_threads();
        engine_destroy();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(g_win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win);
        SDL_Quit();
        engine_stop_threads();
        engine_destroy();
        return 1;
    }

    SDL_Texture *tex = create_texture(ren, window_width, window_height);
    if (!tex) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(g_win);
        SDL_Quit();
        engine_stop_threads();
        engine_destroy();
        return 1;
    }

    SDL_StartTextInput();
    update_title();

    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last_counter = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const int max_steps_per_frame = 5;

    Uint32 last_fps_time = SDL_GetTicks();
    int frames = 0;
    int steps_this_sec = 0;
    int running = 1;
    SDL_Event e;

    while (running) {
        while (SDL_PollEvent(&e)) {
            switch(e.type) {
                case SDL_QUIT:
                    running = 0;
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                        window_width  = e.window.data1;
                        window_height = e.window.data2;
                        SDL_DestroyTexture(tex);
                        tex = create_texture(ren, window_width, window_height);
                        clamp_offset();
                    }
                    break;
                case SDL_TEXTINPUT:
                    handle_text_input(&e);
                    break;
                case SDL_KEYDOWN:
                    handle_key_down(e.key.keysym);
                    break;
                case SDL_MOUSEWHEEL:
                    if (!input_mode) {
                        handle_zoom(e.wheel.y, last_mouse_x, last_mouse_y);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        dragging = 1;
                        last_mouse_x = e.button.x;
                        last_mouse_y = e.button.y;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT) dragging = 0;
                    break;
                case SDL_MOUSEMOTION:
                    if (dragging) {
                        offset_x -= e.motion.xrel;
                        offset_y -= e.motion.yrel;
                        clamp_offset();
                    }
                    last_mouse_x = e.motion.x;
                    last_mouse_y = e.motion.y;
                    break;
            }
        }

        Uint64 now_counter = SDL_GetPerformanceCounter();
        double frame_time = (double)(now_counter - last_counter) / (double)perf_freq;
        last_counter = now_counter;
        if (frame_time > 0.25) frame_time = 0.25;

        if (!paused) {
            accumulator += frame_time;
        } else {
            accumulator = 0.0;
        }

        int steps = 0;
        double step_dt = 1.0 / (double)sim_hz;
        while (!paused && accumulator >= step_dt && steps < max_steps_per_frame) {
            engine_step_begin();
            engine_step_end();
            accumulator -= step_dt;
            steps++;
        }

        if (steps == max_steps_per_frame && accumulator > step_dt) {
            accumulator = step_dt;
        }

        steps_this_sec += steps;
        render_world(ren, tex);

        frames++;
        Uint32 now_ms = SDL_GetTicks();
        if (now_ms - last_fps_time >= 1000) {
            if (!input_mode) {
                char title[256];
                snprintf(title, sizeof(title),
                    "Game of Life  |  Alive: %d  |  FPS: %d  |  UPS: %d/%d  |  Density: %d%%  |  Scale: %.1f  |  [Up/Down]=Speed [W]=Randomize",
                    engine_count_alive(), frames, steps_this_sec, sim_hz, density_pct, scale);
                SDL_SetWindowTitle(g_win, title);
            }

            frames = 0;
            steps_this_sec = 0;
            last_fps_time = now_ms;
        }
    }

    SDL_StopTextInput();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();

    engine_stop_threads();
    engine_destroy();
    return 0;
}
