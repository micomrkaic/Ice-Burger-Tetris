/*
  Mico Mrkaic, September 2025

 * IceBurger Tetris — C + SDL2 (single-file)
 * Blocks are made from 🍦 and 🍔 “tiles”; clearing lines triggers a chunky particle explosion.
 *
 * Build (macOS/Homebrew):
 *   brew install sdl2 sdl2_ttf
 *   clang -O2 -Wall -Wextra -std=c11 tetris.c \
 *     -I/usr/local/include -L/usr/local/lib \
 *     -lSDL2 -lSDL2_ttf -o iceburger
 *
 * Build (Linux/Debian/Ubuntu):
 *   sudo apt-get install libsdl2-dev libsdl2-ttf-dev
 *   gcc -O2 -Wall -Wextra -std=c11 iceburger_tetris.c -lSDL2 -lSDL2_ttf -o iceburger
 *
 * Run:
 *   ./tetris
 *
 * Controls:
 *   ←/→ move, ↓ soft drop, ↑ rotate CW, Z rotate CCW, Space hard drop, C hold, P pause, R restart, Esc quit
 *
 * Notes:
 * - Uses SDL2 for rendering and SDL_ttf to draw emoji/text. If your system font lacks color emoji, tiles fall back to colored squares.
 * - Particle system is simple + efficient; feel free to tweak constants.
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define COLS 10
#define ROWS 20
#define TILE 32
#define BORDER 2
#define PREVIEW_W 6
#define PREVIEW_H 6

#define START_SPEED_MS 900
#define SPEED_STEP_MS 70
#define MIN_SPEED_MS 90

#define MAX_PARTICLES 4096

// Utility min/max
static int imax(int a, int b){return a>b?a:b;}
static int imin(int a, int b){return a<b?a:b;}

// RNG
static float frandf(){return (float)rand()/(float)RAND_MAX;}

// Colors
static SDL_Color col_bg = {20, 24, 28, 255};
static SDL_Color col_grid = {36, 42, 48, 255};
static SDL_Color col_text = {235, 235, 235, 255};

// Per-piece tint
static SDL_Color col_piece[7] = {
  {45, 212, 191, 255}, // I
  {250, 204, 21, 255}, // O
  {192, 132, 252, 255}, // T
  {74, 222, 128, 255}, // S
  {251, 113, 133, 255}, // Z
  {96, 165, 250, 255}, // J
  {245, 158, 11, 255}  // L
};

// Emoji strings
static const char *EMOJI_ICE = "🍦"; // UTF-8
static const char *EMOJI_BURGER = "🍔"; // UTF-8

// Board cell
typedef struct { bool filled; int type; int tint; } Cell; // type: 0=ice,1=burger

// Piece
typedef struct {
  int k;                // 0..6 which tetromino
  int w, h;             // dims of shape
  int x, y;             // top-left on board
  unsigned char m[4][4];// shape mask (up to 4x4)
  int type;             // 0 ice / 1 burger (visual)
  int tint;             // color index
} Piece;

// Particle
typedef struct {
  bool alive;
  float x,y,vx,vy; // in pixels
  float life, maxlife;
  SDL_Color c;
} Particle;

// Game state
typedef struct {
  Cell board[ROWS][COLS];
  Piece cur, next, hold;
  bool has_hold;
  bool can_hold;
  bool game_over;
  int score;
  int lines;
  int level;
  int fall_ms;
  Uint32 fall_accum; // ms accumulator
} Game;

// Shapes
static const unsigned char SHAPES[7][4][4] = {
  // I
  {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
  // O
  {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
  // T
  {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
  // S
  {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
  // Z
  {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
  // J
  {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
  // L
  {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
};

static void piece_from_k(Piece *p, int k){
  memset(p,0,sizeof(*p));
  p->k = k;
  p->w = 4; p->h = 4;
  for(int r=0;r<4;r++)for(int c=0;c<4;c++) p->m[r][c]=SHAPES[k][r][c];
  p->x = COLS/2 - 2; p->y = 0;
  p->type = (rand()%2);
  p->tint = k;
}

static void rotate_cw(Piece *p){
  unsigned char t[4][4] = {0};
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) t[c][3-r]=p->m[r][c];
  memcpy(p->m,t,sizeof t);
}
static void rotate_ccw(Piece *p){
  unsigned char t[4][4] = {0};
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) t[3-c][r]=p->m[r][c];
  memcpy(p->m,t,sizeof t);
}

static bool collide(const Game *g, const Piece *p, int nx, int ny){
  for(int r=0;r<4;r++){
    for(int c=0;c<4;c++){
      if(!p->m[r][c]) continue;
      int x = nx + c;
      int y = ny + r;
      if(x<0||x>=COLS||y<0||y>=ROWS) return true;
      if(g->board[y][x].filled) return true;
    }
  }
  return false;
}

static void lock_piece(Game *g){
  for(int r=0;r<4;r++){
    for(int c=0;c<4;c++){
      if(!g->cur.m[r][c]) continue;
      int x=g->cur.x+c, y=g->cur.y+r;
      if(y>=0 && y<ROWS && x>=0 && x<COLS){
        g->board[y][x].filled=true;
        g->board[y][x].type=g->cur.type;
        g->board[y][x].tint=g->cur.tint;
      }
    }
  }
}

// Particle system
static Particle particles[MAX_PARTICLES];
static void particles_reset(){ memset(particles,0,sizeof particles); }
static void spawn_explosion(int cx, int cy, SDL_Color base){
  // cx,cy in pixels centre where line cleared
  int count = 120 + rand()%80;
  for(int i=0;i<count;i++){
    for(int j=0;j<MAX_PARTICLES;j++) if(!particles[j].alive){
      particles[j].alive=true;
      particles[j].x=cx+(frandf()-0.5f)*TILE*COLS*0.1f;
      particles[j].y=cy+(frandf()-0.5f)*TILE*2;
      float ang = frandf()*6.28318f;
      float spd = 100.0f + frandf()*300.0f;
      particles[j].vx=cosf(ang)*spd;
      particles[j].vy=sinf(ang)*spd - (50.0f+frandf()*100.0f);
      particles[j].life=0.0f;
      particles[j].maxlife=0.6f+frandf()*0.6f;
      SDL_Color c = base;
      int d = (int)(frandf()*40.0f);
      c.r = (Uint8)imax(0, imin(255, c.r + d - 20));
      c.g = (Uint8)imax(0, imin(255, c.g + d - 20));
      c.b = (Uint8)imax(0, imin(255, c.b + d - 20));
      particles[j].c=c;
      break;
    }
  }
}
static void particles_update(float dt){
  for(int i=0;i<MAX_PARTICLES;i++) if(particles[i].alive){
    particles[i].life += dt;
    if(particles[i].life >= particles[i].maxlife){ particles[i].alive=false; continue; }
    // gravity + drag
    particles[i].vy += 900.0f * dt;
    particles[i].vx *= (1.0f - 0.8f*dt);
    particles[i].x += particles[i].vx * dt;
    particles[i].y += particles[i].vy * dt;
  }
}

static void clear_lines(Game *g){
  int cleared = 0;
  for(int r=ROWS-1;r>=0;r--){
    bool full=true; for(int c=0;c<COLS;c++) if(!g->board[r][c].filled){ full=false; break; }
    if(full){
      // explosion centre
      int cx = TILE*COLS/2; int cy = TILE*(r+0.5f);
      SDL_Color base = {255, 200, 120, 255};
      spawn_explosion(cx, cy, base);
      cleared++;
      // pull down
      for(int rr=r; rr>0; rr--) memcpy(g->board[rr], g->board[rr-1], sizeof g->board[rr]);
      memset(g->board[0], 0, sizeof g->board[0]);
      r++; // recheck same row after pull
    }
  }
  if(cleared){
    static const int score_tbl[5]={0,40,100,300,1200};
    g->score += score_tbl[cleared]*(g->level+1);
    g->lines += cleared;
    g->level = g->lines/10;
    g->fall_ms = imax(MIN_SPEED_MS, START_SPEED_MS - g->level * SPEED_STEP_MS);
  }
}

static void new_bag_piece(Piece *p){ piece_from_k(p, rand()%7); }

static void spawn_piece(Game *g){
  g->cur = g->next;
  new_bag_piece(&g->next);
  g->cur.x = COLS/2 - 2; g->cur.y = 0;
  if(collide(g,&g->cur,g->cur.x,g->cur.y)) g->game_over=true;
  g->can_hold=true;
}

static void hold_piece(Game *g){
  if(!g->can_hold) return;
  if(!g->has_hold){ g->hold = g->cur; g->has_hold=true; spawn_piece(g); }
  else { Piece tmp=g->hold; g->hold=g->cur; g->cur=tmp; g->cur.x=COLS/2-2; g->cur.y=0; if(collide(g,&g->cur,g->cur.x,g->cur.y)) g->game_over=true; }
  g->can_hold=false;
}

static void hard_drop(Game *g){
  while(!collide(g,&g->cur,g->cur.x,g->cur.y+1)) g->cur.y++;
  lock_piece(g);
  clear_lines(g);
  spawn_piece(g);
}

static void soft_step(Game *g){
  if(!collide(g,&g->cur,g->cur.x,g->cur.y+1)) g->cur.y++;
  else { lock_piece(g); clear_lines(g); spawn_piece(g); }
}

static void attempt_rotate(Game *g, bool cw){
  Piece t = g->cur; if(cw) rotate_cw(&t); else rotate_ccw(&t);
  int kicks[5][2]={{0,0},{1,0},{-1,0},{0,-1},{0,1}};
  for(int i=0;i<5;i++) if(!collide(g,&t,t.x+kicks[i][0],t.y+kicks[i][1])){ t.x+=kicks[i][0]; t.y+=kicks[i][1]; g->cur=t; return; }
}

static void game_reset(Game *g){
  memset(g,0,sizeof *g);
  g->fall_ms = START_SPEED_MS; g->level=0; g->lines=0; g->score=0; g->fall_accum=0;
  for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) g->board[r][c].filled=false;
  new_bag_piece(&g->cur); new_bag_piece(&g->next);
  g->cur.x=COLS/2-2; g->cur.y=0;
  g->can_hold=true; g->has_hold=false; g->game_over=false;
  particles_reset();
}

// Rendering helpers
static void fill_rect(SDL_Renderer *ren, int x,int y,int w,int h, SDL_Color c){
  SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a); SDL_Rect R={x,y,w,h}; SDL_RenderFillRect(ren,&R);
}

static void draw_text(SDL_Renderer *ren, TTF_Font *font, const char *txt, int x, int y, SDL_Color color){
  if(!font||!txt||!*txt) return;
  SDL_Surface *surf = TTF_RenderUTF8_Blended(font, txt, color);
  if(!surf) return;
  SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
  SDL_Rect dst = {x, y, surf->w, surf->h};
  SDL_FreeSurface(surf);
  SDL_RenderCopy(ren, tex, NULL, &dst);
  SDL_DestroyTexture(tex);
}

static void draw_tile(SDL_Renderer *ren, TTF_Font *emoji_font, int px, int py, int type, SDL_Color tint){
  // Background rounded-ish square
  SDL_Color shadow = { (Uint8)(tint.r*0.6f), (Uint8)(tint.g*0.6f), (Uint8)(tint.b*0.6f), 255 };
  fill_rect(ren, px+2, py+2, TILE-4, TILE-4, shadow);
  fill_rect(ren, px, py, TILE-4, TILE-4, tint);
  // Emoji overlay if possible
  if(emoji_font){
    const char *e = type==0?EMOJI_ICE:EMOJI_BURGER;
    // Center
    // Render a small text to fit tile
    SDL_Surface *surf = TTF_RenderUTF8_Blended(emoji_font, e, (SDL_Color){255,255,255,255});
    if(surf){
      float scale = (float)(TILE-6) / (float)imax(1, imax(surf->w, surf->h));
      int w = (int)(surf->w * scale); int h=(int)(surf->h * scale);
      SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, surf);
      SDL_FreeSurface(surf);
      SDL_Rect dst={px + (TILE-4-w)/2, py + (TILE-4-h)/2, w, h};
      SDL_RenderCopy(ren, tex, NULL, &dst);
      SDL_DestroyTexture(tex);
    }
  }
}

static void render_board(SDL_Renderer *ren, TTF_Font *emoji_font, const Game *g, int ox, int oy){
  // grid bg
  fill_rect(ren, ox-8, oy-8, COLS*TILE+16, ROWS*TILE+16, col_grid);
  for(int r=0;r<ROWS;r++){
    for(int c=0;c<COLS;c++){
      int px = ox + c*TILE; int py = oy + r*TILE;
      fill_rect(ren, px, py, TILE-1, TILE-1, (SDL_Color){30,35,40,255});
      if(g->board[r][c].filled){
        draw_tile(ren, emoji_font, px, py, g->board[r][c].type, col_piece[g->board[r][c].tint]);
      }
    }
  }
  // current piece
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) if(g->cur.m[r][c]){
    int x = g->cur.x+c, y=g->cur.y+r; if(y<0) continue; if(x<0||x>=COLS||y>=ROWS) continue;
    int px = ox + x*TILE; int py = oy + y*TILE;
    draw_tile(ren, emoji_font, px, py, g->cur.type, col_piece[g->cur.tint]);
  }
}

static void render_preview(SDL_Renderer *ren, TTF_Font *emoji_font, const Piece *p, int ox, int oy){
  fill_rect(ren, ox-8, oy-8, PREVIEW_W*TILE+16, PREVIEW_H*TILE+16, col_grid);
  Piece t=*p; // draw centered
  for(int r=0;r<4;r++) for(int c=0;c<4;c++) if(t.m[r][c]){
    int px = ox + c*TILE; int py = oy + r*TILE;
    draw_tile(ren, emoji_font, px, py, t.type, col_piece[t.tint]);
  }
}

static void render_particles(SDL_Renderer *ren){
  for(int i=0;i<MAX_PARTICLES;i++) if(particles[i].alive){
    float a = 1.0f - (particles[i].life / particles[i].maxlife);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, particles[i].c.r, particles[i].c.g, particles[i].c.b, (Uint8)(a*255));
    SDL_Rect R = { (int)particles[i].x, (int)particles[i].y, 4, 4 };
    SDL_RenderFillRect(ren, &R);
  }
}

int main(int argc, char **argv){
  (void)argc; (void)argv;
  srand((unsigned)time(NULL));
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){ fprintf(stderr,"SDL_Init error: %s\n", SDL_GetError()); return 1; }
  if(TTF_Init()!=0){ fprintf(stderr,"TTF_Init error: %s\n", TTF_GetError()); return 1; }

  int winW = 720, winH = 760;
  SDL_Window *win = SDL_CreateWindow("IceBurger Tetris", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);

  // Try to load a default font for emoji (system dependent). Fallback to NULL.
  // You can replace path below with a known emoji-capable TTF on your system (e.g., NotoColorEmoji.ttf)
  const char *font_candidates[] = {
    "/System/Library/Fonts/Apple Color Emoji.ttc", // macOS
    "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    NULL
  };
  TTF_Font *emoji_font = NULL;
  for(int i=0;font_candidates[i];i++){
    emoji_font = TTF_OpenFont(font_candidates[i], 64);
    if(emoji_font) break;
  }

  TTF_Font *ui_font = NULL;
  const char *ui_candidates[] = {
    "/System/Library/Fonts/SFNS.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    NULL
  };
  for(int i=0;ui_candidates[i];i++){ ui_font = TTF_OpenFont(ui_candidates[i], 22); if(ui_font) break; }

  Game g; game_reset(&g);

  bool running=true, paused=false;
  Uint64 now=SDL_GetPerformanceCounter();
  Uint64 last=now; double freq=(double)SDL_GetPerformanceFrequency();

  while(running){
    last = now; now = SDL_GetPerformanceCounter();
    float dt = (float)((now-last)/freq);

    // input
    SDL_Event e; while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) running=false;
      if(e.type==SDL_KEYDOWN){
        SDL_Keycode k = e.key.keysym.sym;
        if(k==SDLK_ESCAPE) running=false;
        else if(k==SDLK_p) paused=!paused;
        else if(k==SDLK_r) { game_reset(&g); paused=false; }
        if(g.game_over||paused) continue;
        if(k==SDLK_LEFT && !collide(&g,&g.cur,g.cur.x-1,g.cur.y)) g.cur.x--;
        else if(k==SDLK_RIGHT && !collide(&g,&g.cur,g.cur.x+1,g.cur.y)) g.cur.x++;
        else if(k==SDLK_DOWN) soft_step(&g);
        else if(k==SDLK_SPACE) hard_drop(&g);
        else if(k==SDLK_c) hold_piece(&g);
        else if(k==SDLK_z) attempt_rotate(&g,false);
        else if(k==SDLK_UP) attempt_rotate(&g,true);
      }
    }

    if(!paused && !g.game_over){
      g.fall_accum += (Uint32)(dt*1000.0f);
      while(g.fall_accum >= (Uint32)g.fall_ms){ g.fall_accum -= g.fall_ms; soft_step(&g); }
    }

    particles_update(dt);

    // draw
    SDL_SetRenderDrawColor(ren, col_bg.r,col_bg.g,col_bg.b,255);
    SDL_RenderClear(ren);

    int ox = 40, oy = 40;
    render_board(ren, emoji_font, &g, ox, oy);
    render_preview(ren, emoji_font, &g.next, ox + COLS*TILE + 40, oy);
    if(g.has_hold) render_preview(ren, emoji_font, &g.hold, ox + COLS*TILE + 40, oy + PREVIEW_H*TILE + 24);

    render_particles(ren);

    char buf[128];
    snprintf(buf,sizeof buf, "Score %d  Lines %d  Level %d", g.score, g.lines, g.level);
    draw_text(ren, ui_font, buf, ox, oy + ROWS*TILE + 24, col_text);

    if(paused) draw_text(ren, ui_font, "PAUSED (P)", ox+220, oy+200, (SDL_Color){255,210,60,255});
    if(g.game_over) draw_text(ren, ui_font, "GAME OVER (R to restart)", ox+120, oy+220, (SDL_Color){255,120,120,255});

    SDL_RenderPresent(ren);
  }

  if(emoji_font) TTF_CloseFont(emoji_font);
  if(ui_font) TTF_CloseFont(ui_font);
  SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
  TTF_Quit(); SDL_Quit();
  return 0;
}
