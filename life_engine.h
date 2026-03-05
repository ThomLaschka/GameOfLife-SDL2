#ifndef LIFE_ENGINE_H
#define LIFE_ENGINE_H

#include <stdatomic.h>

#define WORLD_SIZE  2048
#define CHUNK_SIZE  64
#define CHUNK_COUNT (WORLD_SIZE / CHUNK_SIZE)

typedef unsigned char cell_t;

extern unsigned char active_chunk[CHUNK_COUNT][CHUNK_COUNT];
extern cell_t (*world_render)[WORLD_SIZE];

void   engine_init(int density);
void   engine_destroy(void);
void   engine_start_threads(void);
void   engine_stop_threads(void);
void   engine_step_begin(void);
void   engine_step_end(void);

int    engine_count_alive(void);

cell_t engine_get_cell(int y, int x);

#endif
