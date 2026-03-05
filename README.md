# GameOfLife-SDL2
An automatic, random Game of Life simulation in C using SDL2. Users can only observe and adjust density or speed.

## Features
- Automatic, random world generation
- Zoom & dragging
- Adjustable density and speed
- Pause, reset, randomize
- Grid display at high zoom

## Controls
- **D**: Set density (1–100%)  
- **S**: Set speed (UPS 1–500)  
- **W**: Randomize world  
- **R**: Reset  
- **Space**: Pause / Resume  
- **Arrow ↑/↓**: Increase / decrease speed  
- **Mouse wheel**: Zoom  
- **Left mouse button**: Dragging  
- **Enter**: Confirm  
- **Esc**: Cancel input

## Compile
```bash
gcc src/main.c src/life_engine.c -o game_of_life -lSDL2 -lSDL2_image -pthread
