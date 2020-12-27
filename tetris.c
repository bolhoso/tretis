// TODO: bug de quando rota perto das bordas, ela colide com a parede e fica no ar...
//   - ao inves de impedir rotacao, tem que mover ela uma casa para o lado
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>

#include <allegro5/allegro5.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>

#define SCREEN_W 800
#define SCREEN_H 600

#define FIELD_ROWS 20
#define FIELD_COLS 10

#define TILE_SIZE_PX 19
#define TILE_PADDING 1

#define COLOR_BG      al_map_rgb_f(0, 0, 0)
#define tile_center() (FIELD_COLS / 2)

#define FPS 30
#define GAME_TIMER_SPEED ((float)1/FPS)
#define INITIAL_SPEED 1
#define OVERRIDE_TICKS -1

#define KEY_SEEN     1
#define KEY_RELEASED 2

#define TILES_BY_PIECE 4

#define ABS(x) (x<0?-x:x)

// interval to rate-limit keyboard, following keys enum
const int KEY_INTERVAL_MS[] = {
	65, //left right
	50, // down
	250, // rotate
	500
};

typedef struct object {
	// this is discrete x,y in the field, not in the screen, relative to tile top
	int col, row;

	ALLEGRO_COLOR color;
} tile;

enum piece_type {

	//  XX  XX
	// XX    XX
	PC_Z = 0,
	PC_INV_Z,

	//  X
	// XXX
	PC_MIDDLE_FINGER,

	// X      X
	// XXX  XXX
	PC_L,
	PC_INV_L,

	// XX
	// XX
	PC_SQUARE,

	// XXXX
	PC_LONG,

	PC_N,
};

enum direction {
	D_NORTH = 0,
	D_EAST,
	D_SOUTH,
	D_WEST,
	D_N
};


enum keys {
	KEY_LEFT_RIGHT = 0,
	KEY_DOWN,
	KEY_UP,
	KEY_SPACE,
	KEY_N
};


typedef struct piece {
	tile *tiles[TILES_BY_PIECE];

	short direction;
	enum piece_type type;
} piece;

typedef struct game_data {
	tile *field[FIELD_ROWS][FIELD_COLS];
	piece *falling;
	piece *next;

	int field_cols, field_rows;

	long ticks;

	long points;
	int speed;

	// last time key was pressed
	unsigned long key_timestamp[KEY_N];
} game_data;

// ===== globals
//
ALLEGRO_FONT *font;
ALLEGRO_TIMER *timer;
ALLEGRO_EVENT_QUEUE *queue;
ALLEGRO_DISPLAY *disp;
unsigned char key[ALLEGRO_KEY_MAX];

ALLEGRO_COLOR COLORS[PC_N];
//
// =====

long t() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);

    long ms = lround(spec.tv_nsec / 1.0e6);
    if (ms > 999) ms = 0;
    return spec.tv_sec*1000 + ms; // Convert nanoseconds to milliseconds
}

int allegro_to_key(int keycode) {
	if (keycode == ALLEGRO_KEY_LEFT || keycode == ALLEGRO_KEY_RIGHT)
		return KEY_LEFT_RIGHT;
	if (keycode == ALLEGRO_KEY_UP)
		return KEY_UP;
	if (keycode == ALLEGRO_KEY_DOWN)
		return KEY_DOWN;
	return -1;
}

void must_init(bool test) {
	if (test)
		return;

	exit(1);
}

void init_piece_colors() {
	COLORS[PC_Z]             = al_map_rgb(0x00, 0xe1, 0x42);
	COLORS[PC_INV_Z]         = al_map_rgb(0x00, 0xe1, 0x42);
	COLORS[PC_MIDDLE_FINGER] = al_map_rgb(0xbb, 0x00, 0xe0);
	COLORS[PC_L]             = al_map_rgb(0x00, 0x84, 0xeb);
	COLORS[PC_INV_L]         = al_map_rgb(0x00, 0x84, 0xeb);
	COLORS[PC_SQUARE]        = al_map_rgb(0xf1, 0xde, 0x00);
	COLORS[PC_LONG]          = al_map_rgb(0x00, 0xde, 0xf6);
}

void init_allegro() {
	time_t t;
	srand((unsigned) time(&t));

	must_init(al_init());
	must_init(al_install_keyboard());
	must_init(al_install_mouse());
	must_init(al_init_image_addon());
	must_init(al_init_primitives_addon());
	must_init(al_install_audio());
	must_init(al_init_acodec_addon());
	must_init(al_reserve_samples(16));

	timer = al_create_timer(GAME_TIMER_SPEED);
	queue = al_create_event_queue();
	disp = al_create_display(SCREEN_W, SCREEN_H);
	font = al_create_builtin_font();

	must_init(timer);
	must_init(queue);
	must_init(disp);
	must_init(font);

	al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
	al_set_new_display_option(ALLEGRO_SAMPLES, 8, ALLEGRO_SUGGEST);
	al_set_new_bitmap_flags(ALLEGRO_MIN_LINEAR | ALLEGRO_MAG_LINEAR);

	al_register_event_source(queue, al_get_mouse_event_source());
	al_register_event_source(queue, al_get_timer_event_source(timer));
	al_register_event_source(queue, al_get_display_event_source(disp));
	al_register_event_source(queue, al_get_keyboard_event_source());
	memset(key, 0, sizeof(key));

	init_piece_colors();
}

tile *create_tile(int row, int col, ALLEGRO_COLOR color) {
	tile *new = (tile *) malloc(sizeof (tile));
	new->color = color;
	new->col = col;
	new->row = row;
	return new;
}

piece *pc_create(short type, int top_r, int top_c) {
	piece *p = (piece *) malloc(sizeof(piece));
	p->direction = D_NORTH;
	p->type = type;

	int inv = (type == PC_INV_Z || type == PC_INV_L) ? -1 : 1;

	switch(type) {
	// 0       0
	// 123   321
	case PC_INV_L:
	case PC_L:
		p->tiles[0] = create_tile(top_r,     top_c,           COLORS[PC_L]);
		p->tiles[1] = create_tile(top_r + 1, top_c,           COLORS[PC_L]);
		p->tiles[2] = create_tile(top_r + 1, top_c + inv * 1, COLORS[PC_L]);
		p->tiles[3] = create_tile(top_r + 1, top_c + inv * 2, COLORS[PC_L]);
		break;

	//  0
	// 123
	case PC_MIDDLE_FINGER:
		p->tiles[0] = create_tile(top_r,     top_c,     COLORS[PC_MIDDLE_FINGER]);
		p->tiles[1] = create_tile(top_r + 1, top_c - 1, COLORS[PC_MIDDLE_FINGER]);
		p->tiles[2] = create_tile(top_r + 1, top_c,     COLORS[PC_MIDDLE_FINGER]);
		p->tiles[3] = create_tile(top_r + 1, top_c + 1, COLORS[PC_MIDDLE_FINGER]);
		break;

	//  01    10
    // 32      23
	case PC_INV_Z:
	case PC_Z:
		p->tiles[0] = create_tile(top_r,     top_c,           COLORS[PC_Z]);
		p->tiles[1] = create_tile(top_r,     top_c + inv * 1, COLORS[PC_Z]);
		p->tiles[2] = create_tile(top_r + 1, top_c,           COLORS[PC_Z]);
		p->tiles[3] = create_tile(top_r + 1, top_c - inv * 1, COLORS[PC_Z]);
		break;

	// 01
	// 23
	case PC_SQUARE:
		p->tiles[3] = create_tile(top_r,     top_c,     COLORS[PC_SQUARE]);
		p->tiles[0] = create_tile(top_r,     top_c + 1, COLORS[PC_SQUARE]);
		p->tiles[1] = create_tile(top_r + 1, top_c,     COLORS[PC_SQUARE]);
		p->tiles[2] = create_tile(top_r + 1, top_c + 1, COLORS[PC_SQUARE]);
		break;

	// 0
	// 1
	// 2
	// 3
	case PC_LONG:
		p->tiles[0] = create_tile(top_r,     top_c, COLORS[PC_LONG]);
		p->tiles[1] = create_tile(top_r + 1, top_c, COLORS[PC_LONG]);
		p->tiles[2] = create_tile(top_r + 2, top_c, COLORS[PC_LONG]);
		p->tiles[3] = create_tile(top_r + 3, top_c, COLORS[PC_LONG]);
		break;

	default:
		printf ("unknown piece!!");
		exit(-1);

	}

	return p;
}

piece *pc_create_random() {
	return pc_create(rand() % PC_N, 0, tile_center());
}

// returns the leftmost tile col
int pc_min_col(piece *p) {
	int min = 99999;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		min = p->tiles[i]->col < min ? p->tiles[i]->col : min;
	}

	return min;
}

// returns the rightmost tile col
int pc_max_col(piece *p) {
	int max = -999;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		max = p->tiles[i]->col > max ? p->tiles[i]->col : max;
	}

	return max;
}

// returns the leftmost tile col
int pc_min_row(piece *p) {
	int min = 99999;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		min = p->tiles[i]->row < min ? p->tiles[i]->row : min;
	}

	return min;
}

int pc_max_row(piece *p) {
	int max = -999;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		max = p->tiles[i]->row > max ? p->tiles[i]->row : max;
	}

	return max;
}



void pc_place(game_data *g) {
	piece *p = g->falling;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		// TODO: maybe allocating and copying piece's tiles to field
		g->field[p->tiles[i]->row][p->tiles[i]->col] = p->tiles[i];
	}
}

void pc_move_delta(piece *p, int d_row, int d_col) {
	// TODO: can move?

	for (int i = 0; i < TILES_BY_PIECE; i++) {
		p->tiles[i]->col += d_col;
		p->tiles[i]->row += d_row;
	}
}

void grab_next_piece(game_data *g) {
	if (!g->next) {
		g->next = pc_create(rand() % PC_N, 0, 0);
	}

	g->falling = g->next;
	pc_move_delta(g->falling, 0, FIELD_COLS / 2);
	g->next = pc_create(rand() % PC_N, 0, 0);
}

void init_objects(game_data *g) {
	g->field_cols = FIELD_COLS;
	g->field_rows = FIELD_ROWS;
	for (int i = 0; i < g->field_rows; i++) {
		for (int j = 0; j < g->field_cols; j++) {
			g->field[i][j] = NULL;
		}
	}

	g->next = g->falling = NULL;
	grab_next_piece(g);

	g->points = 0;
	g->speed = INITIAL_SPEED;
	for (int i = 0; i < KEY_N; i++) {
		g->key_timestamp[i] = 0;
	}
}

// calculate field offset
int field_x(int x) {
	return x + SCREEN_W / 2 - TILE_SIZE_PX * FIELD_COLS / 2;
}
int toy(int y) {
	return y + SCREEN_H / 2 - TILE_SIZE_PX * FIELD_ROWS / 2;
}
//

// conver piece row into coordinate relative to the field top
int col2x(int field_col) {
	return field_x(field_col * TILE_SIZE_PX);
}

int row2y(int field_row) {
	return toy(field_row * TILE_SIZE_PX);
}
//

void draw_tilexy(tile *p, int x, int y) {
	int pad = TILE_PADDING;
	al_draw_filled_rectangle(x + pad, y + pad, x + TILE_SIZE_PX - pad, y + TILE_SIZE_PX - pad, p->color);
}

void draw_tile(tile *p) {
   int x = col2x(p->col);
   int y = row2y(p->row);

   draw_tilexy(p, x, y);
}

void draw_piecexy(piece *p, int x, int y) {
	if (!p)
		return;

	for (int i = 0; i < TILES_BY_PIECE; i++) {
		draw_tilexy(p->tiles[i], x + TILE_SIZE_PX * p->tiles[i]->col, y + TILE_SIZE_PX * p->tiles[i]->row);
	}
}

void draw_piece(piece *p) {
	if (!p)
		return;

	for (int i = 0; i < TILES_BY_PIECE; i++) {
		draw_tile(p->tiles[i]);
	}
}

void draw_field(game_data *g) {
	al_draw_rectangle(col2x(0), row2y(0), col2x(FIELD_COLS), row2y(FIELD_ROWS), al_map_rgb_f(0.3, 0.4, 0.8), 4.4);

	for (int i = 0; i < g->field_rows; i++) {
		for (int j = 0; j < g->field_cols; j++) {
			if (g->field[i][j] != NULL) {
				draw_tile(g->field[i][j]);
			}
		}
	}
}

void draw_next_piece(game_data *g) {
	int x = col2x(FIELD_COLS) + 100;
	int y = row2y(0);
	int w = TILE_SIZE_PX * 6;

	al_draw_rectangle(x, y, x + w, y + w, al_map_rgb_f(0.3, 0.8, 0.8), 4.4);
	al_draw_text(font, al_map_rgb_f(1, 1, 1), x + w/2, y - 10, ALLEGRO_ALIGN_CENTER, "Proxima");

	draw_piecexy(g->next, x + w/2, y + w/2 - TILE_SIZE_PX);
}

void draw_screen(game_data *g) {
	al_clear_to_color(COLOR_BG);
	al_draw_text(font, al_map_rgb_f(1, 1, 1), SCREEN_W / 2, 10, ALLEGRO_ALIGN_CENTER, "TRETIS");

	draw_piece(g->falling);
	draw_field(g);
	draw_next_piece(g);

	al_flip_display();
}

bool pc_can_move(game_data *g, piece *falling, int dcol, int drow) {
	if (falling == NULL)
		return false;

	bool move_allowed = true;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		tile *tile = falling->tiles[i];

		// place to go is free
		move_allowed &= g->field[tile->row + drow][tile->col + dcol] == NULL;
		move_allowed &= tile->col + dcol >= 0 && tile->col + dcol < g->field_cols;
		move_allowed &= tile->row + drow >= 0 && tile->row + drow < g->field_rows;
	}

	return move_allowed;
}

void swap(int *a, int *b) {
	int tmp = *a;
	*a = *b;
	*b = tmp;
}

void pc_rotate(piece *pc) {
	if (pc == NULL || pc->type == PC_SQUARE)
		return;

	// Uses tile[2] as reference point, that's where the middle
	// of each piece is
	tile ref = *pc->tiles[2];

	int row_offset = pc_min_row(pc);
	int col_offset = pc_min_col(pc);
	for (int i = 0; i < TILES_BY_PIECE; i++){
		tile *t = pc->tiles[i];
		t->row = t->row - row_offset;
		t->col = t->col - col_offset;
	}

	// do the rotation by transposing...
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		swap(&(pc->tiles[i]->row), &(pc->tiles[i]->col));
	}

	// then inverting y
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		pc->tiles[i]->row = TILES_BY_PIECE - pc->tiles[i]->row;
	}

	// copy back from temp field back to piece. Offset it relative to the
	// reference (center) tile original position
	int ref_dcol = pc->tiles[2]->col + col_offset - ref.col;
	int ref_drow = pc->tiles[2]->row + row_offset - ref.row;

	// Now avoid going beyond field boundaires
	// TODO
	int field_offset_col = 0, field_offset_row = 0;
//	int field_offset_col = pc_min_col(pc) < 0 ? ABS(pc_min_col(pc)) : (
//			pc_max_col(pc) >= FIELD_COLS ? pc_max_col(pc) - FIELD_COLS : 0
//	);
//	int field_offset_row = pc_min_row(pc) < 0 ? ABS(pc_min_row(pc)) : (
//			pc_max_row(pc) >= FIELD_ROWS ? pc_max_row(pc) - FIELD_ROWS : 0
//	);

	// translate piece by center offset and field boundaries
	for (int i = 0; i < TILES_BY_PIECE; i++){
		tile *t = pc->tiles[i];
		t->row = t->row + row_offset - ref_drow + field_offset_row;
		t->col = t->col + col_offset - ref_dcol + field_offset_col;
	}

}

bool pc_can_fall(game_data *g, piece *falling) {
	return pc_can_move (g, falling, 0, 1);
}

void drop_tiles(game_data *game, int row) {
	for (int c = 0; c < game->field_cols; c++) {
		if (game->field[row][c] != NULL) {
			game->field[row][c] = NULL;
		}
	}

	for (int r = row; r > 0; r--) {
		for (int c = 0; c < game->field_cols; c++) {
			tile *upper_tile = game->field[r-1][c];
			game->field[r][c] = upper_tile;

			if (upper_tile) {
				game->field[r][c]->col = c;
				game->field[r][c]->row = r;
			}
		}
	}
}

// check all rows comprised by tiles from *placed
// disappear any of the lines that have been completed and
// drop the field content down
bool disappear_line(game_data *game, piece *placed) {
	bool line_removed = false;
	for (int i = 0; i < TILES_BY_PIECE; i++) {
		int row = placed->tiles[i]->row;

		bool row_complete = true;
		for (int i = 0; row_complete && i < game->field_cols; i++) {
			row_complete = game->field[row][i] != NULL;
		}

		if (row_complete) {
			drop_tiles(game, row);
			line_removed = true;
		}
	}

	return line_removed;
}

// allow key to be processed if last press was after the key rate
// limiting interval
bool should_process_key(game_data *g, int k) {
	return t() - g->key_timestamp[k] > KEY_INTERVAL_MS[k];
}

bool game_logic(game_data *game) {
	if (game->falling == NULL) {
		exit(1); // should never reach here :/
	}

	// only move pieces every FPS ticks or when to override
	bool process_piece_logic = false;
	if (game->ticks == OVERRIDE_TICKS || game->ticks % FPS == 0) {
		game->ticks = 0;
		process_piece_logic = true;
	}
	game->ticks += game->speed;

	if (process_piece_logic) {
		if (pc_can_fall(game, game->falling)) {
			// don't move down is player is forcing piece down
			if (should_process_key(game, KEY_DOWN))
				pc_move_delta(game->falling, 1, 0);
		} else {
			pc_place(game);
			disappear_line(game, game->falling);
			grab_next_piece(game);
		}
	}

	return process_piece_logic;
}

bool process_kbd(game_data *game, bool *done) {
	if (key[ALLEGRO_KEY_ESCAPE])
		*done = true;

	bool changed_state = false;
	if (game->falling) {
		if (should_process_key(game, KEY_DOWN) &&
				key[ALLEGRO_KEY_DOWN] &&
				pc_can_move(game, game->falling, 0, 1)) {
			pc_move_delta(game->falling, 1, 0);

			changed_state = true;
			game->key_timestamp[KEY_DOWN] = t();
		}
		if (should_process_key(game, KEY_UP) &&
				key[ALLEGRO_KEY_UP]) {
			pc_rotate(game->falling);

			changed_state = true;
			game->key_timestamp[KEY_UP] = t();
		}

		if (should_process_key(game, KEY_LEFT_RIGHT) &&
				key[ALLEGRO_KEY_LEFT] &&
				pc_min_col(game->falling) > 0 &&
				pc_can_move(game, game->falling, -1, 0)) {
			pc_move_delta(game->falling, 0, -1);

			changed_state = true;
			game->key_timestamp[KEY_LEFT_RIGHT] = t();
		}

		if (should_process_key(game, KEY_LEFT_RIGHT) &&
				key[ALLEGRO_KEY_RIGHT] &&
				pc_max_col(game->falling) + 1 < game->field_cols &&
				pc_can_move(game, game->falling, 1, 0)) {
			pc_move_delta(game->falling, 0, 1);

			changed_state = true;
			game->key_timestamp[KEY_LEFT_RIGHT] = t();
		}

		if (should_process_key(game, KEY_SPACE) && key[ALLEGRO_KEY_SPACE]) {
			while (pc_can_move(game, game->falling, 0, 1)) {
				pc_move_delta(game->falling, 1, 0);
			}
			changed_state = true;
			game->ticks = OVERRIDE_TICKS;
			game->key_timestamp[KEY_SPACE] = t();
		}
	}

	// remove the release flag
	for (int i = 0; i < ALLEGRO_KEY_MAX; i++) {
		key[i] &= KEY_SEEN;
	}

	return changed_state;
}

void draw_menu() {
	al_clear_to_color(COLOR_BG);
	al_draw_text(font, al_map_rgb_f(1, 1, 1), SCREEN_W / 2, SCREEN_H / 2, ALLEGRO_ALIGN_CENTER, "TRETIS");
	al_draw_text(font, al_map_rgb_f(1, 1, 1), SCREEN_W / 2, SCREEN_H / 2 + 25, ALLEGRO_ALIGN_CENTER, "Press Enter to Start, ESC to quit");
	al_flip_display();
}

bool screen_start_quit() {
	ALLEGRO_EVENT event;
	ALLEGRO_KEYBOARD_STATE ks;

	while (1) {
		al_wait_for_event(queue, &event);

		switch (event.type) {
		case ALLEGRO_EVENT_TIMER:
			al_get_keyboard_state(&ks);

			if(al_key_down(&ks, ALLEGRO_KEY_ENTER))
				return false;
			if(al_key_down(&ks, ALLEGRO_KEY_ESCAPE))\
				return true;

			draw_menu();
		}
	}

	return true;
}

int main() {
	ALLEGRO_EVENT event;
	game_data game_data;

	init_allegro();
	init_objects(&game_data);

	bool done = false;
	bool redraw = true;

	al_start_timer(timer);

	done = screen_start_quit();

	if (!done) {
		draw_screen(&game_data);
	}

	while (!done) {
		al_wait_for_event(queue, &event);

		switch (event.type) {
		case ALLEGRO_EVENT_TIMER:
			redraw = process_kbd(&game_data, &done);
			redraw |= game_logic(&game_data);
			break;

		case ALLEGRO_EVENT_KEY_DOWN:
			key[event.keyboard.keycode] = KEY_SEEN | KEY_RELEASED;
			break;

		case ALLEGRO_EVENT_KEY_UP:
			// remove flag of key seen
			key[event.keyboard.keycode] &= KEY_RELEASED;

			// key up, reset timer to process the same key in less than the interval
			// just to feel natural and avoiding missing keystrokes in between keys intervals
			int k = allegro_to_key(event.keyboard.keycode);
			if (k != -1) game_data.key_timestamp[k] = 0;
			break;


		case ALLEGRO_EVENT_DISPLAY_CLOSE:
			done = true;
			break;
		}

		if (done) {
			break;
		}

		if (redraw && al_is_event_queue_empty(queue)) {
			draw_screen(&game_data);
			redraw = false;
		}
	}

	al_destroy_font(font);
	al_destroy_display(disp);
	al_destroy_timer(timer);
	al_destroy_event_queue(queue);

	return 0;
}
