#define _POSIX_C_SOURCE 200112L
#include "life_engine.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#ifdef _WIN32
  #include <windows.h>

  typedef HANDLE plat_thread_t;

  typedef struct {
      CRITICAL_SECTION  mutex;
      CONDITION_VARIABLE cv;
      int               count;
      int               total;
      int               generation;
  } plat_barrier_t;

  static void barrier_init(plat_barrier_t *b, int n) {
      InitializeCriticalSection(&b->mutex);
      InitializeConditionVariable(&b->cv);
      b->count = 0;
      b->total = n;
      b->generation = 0;
  }

  static void barrier_wait(plat_barrier_t *b) {
      EnterCriticalSection(&b->mutex);
      int gen = b->generation;
      if (++b->count == b->total) {
          b->count = 0;
          b->generation++;
          WakeAllConditionVariable(&b->cv);
      } else {
          while (b->generation == gen) {
              SleepConditionVariableCS(&b->cv, &b->mutex, INFINITE);
          }
      }
      LeaveCriticalSection(&b->mutex);
  }

  static void barrier_destroy(plat_barrier_t *b) {
      DeleteCriticalSection(&b->mutex);
  }

  static DWORD WINAPI win_worker(LPVOID arg);
  static void thread_create(plat_thread_t *t, LPTHREAD_START_ROUTINE fn, void *arg) {
      *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
  }

  static void thread_join(plat_thread_t t) {
      WaitForSingleObject(t, INFINITE);
      CloseHandle(t);
  }
#else
  #include <pthread.h>

  typedef pthread_t         plat_thread_t;
  typedef pthread_barrier_t plat_barrier_t;

  static void barrier_init(plat_barrier_t *b, int n) {
      pthread_barrier_init(b, NULL, n);
  }

  static void barrier_wait(plat_barrier_t *b) {
      pthread_barrier_wait(b);
  }

  static void barrier_destroy(plat_barrier_t *b) {
      pthread_barrier_destroy(b);
  }

  static void *posix_worker(void *arg);
  static void thread_create(plat_thread_t *t, void *(*fn)(void *), void *arg) {
      pthread_create(t, NULL, fn, arg);
  }

  static void thread_join(plat_thread_t t) {
      pthread_join(t, NULL);
  }
#endif

#define THREAD_COUNT  4
#define BARRIER_COUNT (THREAD_COUNT + 1)
_Static_assert((WORLD_SIZE & (WORLD_SIZE - 1)) == 0, "WORLD_SIZE must be a power of two");
_Static_assert((CHUNK_COUNT % THREAD_COUNT) == 0, "CHUNK_COUNT must be divisible by THREAD_COUNT");

static plat_thread_t  threads[THREAD_COUNT];
static plat_barrier_t barrier;
static atomic_int     running;

static cell_t (*world_a)[WORLD_SIZE];
static cell_t (*world_b)[WORLD_SIZE];

static cell_t (*world_sim)[WORLD_SIZE];
static cell_t (*world_next)[WORLD_SIZE];

cell_t (*world_render)[WORLD_SIZE];

unsigned char active_chunk[CHUNK_COUNT][CHUNK_COUNT];
static unsigned char next_active_chunk[CHUNK_COUNT][CHUNK_COUNT];
static unsigned char thread_next_active[THREAD_COUNT][CHUNK_COUNT][CHUNK_COUNT];

static atomic_int alive_count;
static int ids[THREAD_COUNT];

static inline int wrap(int v) {
    return v & (WORLD_SIZE - 1);
}

static inline int neighbors(int y, int x) {
    int n = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!dy && !dx) continue;
            n += world_sim[wrap(y + dy)][wrap(x + dx)];
        }
    }
    return n;
}

static void merge_thread_active_chunks(void) {
    memset(next_active_chunk, 0, sizeof(next_active_chunk));
    for (int t = 0; t < THREAD_COUNT; t++) {
        for (int cy = 0; cy < CHUNK_COUNT; cy++) {
            for (int cx = 0; cx < CHUNK_COUNT; cx++) {
                if (thread_next_active[t][cy][cx]) {
                    next_active_chunk[cy][cx] = 1;
                }
            }
        }
    }
}

static void worker_logic(int id) {
    int rows_per_thread = CHUNK_COUNT / THREAD_COUNT;
    int start_row = id * rows_per_thread;
    int end_row = start_row + rows_per_thread;
    unsigned char (*local_next_active)[CHUNK_COUNT] = thread_next_active[id];

    for (;;) {
        barrier_wait(&barrier);
        if (!atomic_load(&running)) break;

        int local_delta = 0;
        memset(local_next_active, 0, sizeof(thread_next_active[id]));

        for (int cy = start_row; cy < end_row; cy++) {
            for (int cx = 0; cx < CHUNK_COUNT; cx++) {
                if (!active_chunk[cy][cx]) continue;

                int start_y = cy * CHUNK_SIZE;
                int start_x = cx * CHUNK_SIZE;
                int changed = 0;

                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        int wy = start_y + y;
                        int wx = start_x + x;
                        int n = neighbors(wy, wx);

                        cell_t old_cell = world_sim[wy][wx];
                        cell_t new_cell = old_cell ? (n == 2 || n == 3) : (n == 3);
                        world_next[wy][wx] = new_cell;

                        if (new_cell != old_cell) {
                            changed = 1;
                            local_delta += new_cell ? 1 : -1;
                        }
                    }
                }

                if (changed) {
                    local_next_active[cy][cx] = 1;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            local_next_active[(cy + dy + CHUNK_COUNT) % CHUNK_COUNT]
                                             [(cx + dx + CHUNK_COUNT) % CHUNK_COUNT] = 1;
                        }
                    }
                }
            }
        }

        atomic_fetch_add(&alive_count, local_delta);
        barrier_wait(&barrier);

        if (id == 0) {
            merge_thread_active_chunks();

            cell_t (*tmp)[WORLD_SIZE] = world_sim;
            world_sim = world_next;
            world_next = tmp;
            world_render = world_sim;

            memcpy(active_chunk, next_active_chunk, sizeof(active_chunk));
        }

        barrier_wait(&barrier);
    }
}

#ifdef _WIN32
static DWORD WINAPI win_worker(LPVOID arg) {
    worker_logic(*(int *)arg);
    return 0;
}
#else
static void *posix_worker(void *arg) {
    worker_logic(*(int *)arg);
    return NULL;
}
#endif

void engine_init(int density) {
    srand((unsigned)time(NULL));

    world_a = malloc(sizeof(*world_a) * WORLD_SIZE);
    world_b = malloc(sizeof(*world_b) * WORLD_SIZE);
    if (!world_a || !world_b) {
        fprintf(stderr, "engine_init: out of memory\n");
        free(world_a);
        free(world_b);
        exit(EXIT_FAILURE);
    }

    world_sim = world_a;
    world_next = world_b;
    world_render = world_sim;

    int alive = 0;
    for (int y = 0; y < WORLD_SIZE; y++) {
        for (int x = 0; x < WORLD_SIZE; x++) {
            cell_t c = (rand() % 100 < density) ? 1 : 0;
            world_sim[y][x] = c;
            world_next[y][x] = c;
            alive += c;
        }
    }

    atomic_store(&alive_count, alive);
    memset(active_chunk, 1, sizeof(active_chunk));
    memset(next_active_chunk, 0, sizeof(next_active_chunk));
    memset(thread_next_active, 0, sizeof(thread_next_active));
}

void engine_start_threads(void) {
    atomic_store(&running, 1);
    barrier_init(&barrier, BARRIER_COUNT);

    for (int i = 0; i < THREAD_COUNT; i++) {
        ids[i] = i;
#ifdef _WIN32
        thread_create(&threads[i], win_worker, &ids[i]);
#else
        thread_create(&threads[i], posix_worker, &ids[i]);
#endif
    }
}

void engine_stop_threads(void) {
    if (!atomic_load(&running)) return;

    atomic_store(&running, 0);
    barrier_wait(&barrier);

    for (int i = 0; i < THREAD_COUNT; i++) {
        thread_join(threads[i]);
    }

    barrier_destroy(&barrier);
}

void engine_step_begin(void) {
    barrier_wait(&barrier);
}

void engine_step_end(void) {
    barrier_wait(&barrier);
    barrier_wait(&barrier);
}

int engine_count_alive(void) {
    return atomic_load(&alive_count);
}

cell_t engine_get_cell(int y, int x) {
    return world_render[wrap(y)][wrap(x)];
}

void engine_destroy(void) {
    free(world_a);
    free(world_b);
    world_a = NULL;
    world_b = NULL;
    world_sim = NULL;
    world_next = NULL;
    world_render = NULL;
}

