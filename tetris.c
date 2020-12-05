#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#include <allegro5/allegro5.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>

#define SCREEN_W 640
#define SCREEN_H 480

#define FIELD_ROWS 22
#define FIELD_COLS 10

#define PIECE_SIZE_PX 20

#define COLOR_BG   al_map_rgb_f(0, 0, 0)

#define FPS 30
#define GAME_TIMER_SPEED ((float)1/FPS)
#define INITIAL_SPEED 1;

#define KEY_SEEN     1
#define KEY_RELEASED 2

typedef struct object {
	// this is discrete x,y in the field, not in the screen, relative to piece top
	int col, row;

	ALLEGRO_COLOR color;
	short type;
} tile;

typedef struct game_data {
	tile *field[FIELD_ROWS][FIELD_COLS];
	tile *falling;

	int field_cols, field_rows;

	int ticks;

	long points;
	int speed;
} game_data;

// ===== globals
//
ALLEGRO_FONT *font;
ALLEGRO_TIMER *timer;
ALLEGRO_EVENT_QUEUE *queue;
ALLEGRO_DISPLAY *disp;
unsigned char key[ALLEGRO_KEY_MAX];
//
// =====

void must_init(bool test) {
	if (test)
		return;

	exit(1);
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
}

tile *create_piece() {
	tile *new = (tile *) malloc(sizeof (tile));
	new->color = al_map_rgb_f(0.5, 0.5, 0.8);
	new->type = 0; // TODO: random type
	new->col = piece_center(new);
	new->row = 0;
	return new;
}

void init_objects(game_data *data) {
	data->field_cols = FIELD_COLS;
	data->field_rows = FIELD_ROWS;
	for (int i = 0; i < data->field_rows; i++) {
		for (int j = 0; j < data->field_cols; j++) {
			data->field[i][j] = NULL;
		}
	}
	data->falling = create_piece();

	data->points = 0;
	data->speed = INITIAL_SPEED;
}

int to_scrx(int field_x) {
	return field_x * PIECE_SIZE_PX;
}

int to_scry(int field_y) {
	return field_y * PIECE_SIZE_PX;
}

void draw_piece(tile *p) {
	if (!p)
		return;

	int x = to_scrx(p->col);
	int y = to_scry(p->row);

	al_draw_filled_rectangle(x, y, x + PIECE_SIZE_PX, y + PIECE_SIZE_PX, p->color);
}

void draw_field(game_data *g) {
	for (int i = 0; i < g->field_rows; i++) {
		for (int j = 0; j < g->field_cols; j++) {
			if (g->field[i][j] != NULL) {
				draw_piece(g->field[i][j]);
			}
		}
	}
}

void draw_screen(game_data *g) {
	al_clear_to_color(COLOR_BG);
	al_draw_text(font, al_map_rgb_f(1, 1, 1), SCREEN_W / 2, 0, 0, "GAME!"); // TODO: how to center text?

	draw_piece(g->falling);
	draw_field(g);

	al_flip_display();
}

int piece_center (tile *p) {
	// for now, just return field center
	return FIELD_COLS / 2;
}

bool can_move(game_data *g, tile *free, int dcol, int drow) {
	if (free == NULL)
		return false;

	bool within_field = g->falling->row + drow < g->field_rows;
//			g->falling->col + dcol > 0 && g->falling->col + dcol < g->field_cols;
	bool free_below = within_field && g->field[free->row + drow][free->col + dcol] != NULL;

	return within_field && !free_below;
}

void drop_pieces(game_data *game, int row) {
	for (int r = row; r > 0; r--) {
		for (int c = 0; c < game->field_cols; c++) {
			tile *upper_piece = game->field[r-1][c];
			game->field[r][c] = upper_piece;

			if (upper_piece) {
				game->field[r][c]->col = c;
				game->field[r][c]->row = r;
			}
		}
	}
}

bool disappear_line(game_data *game, int row) {
	bool row_complete = true;
	for (int i = 0; row_complete && i < game->field_cols; i++) {
		row_complete = game->field[row][i] != NULL;
	}

	if (row_complete) {
		drop_pieces(game, row);
	}

	return row_complete;
}

bool game_logic(game_data *game) {
	if (game->falling == NULL) {
		exit(1); // should never reach here :/
		game->falling = create_piece();
	}

	// only move pieces every FPS ticks
	game->ticks += game->speed;

	bool process_piece_logic = false;
	if (game->ticks % FPS == 0) {
		game->ticks = 0;
		process_piece_logic = true;
	}

	if (process_piece_logic) {
		int px = game->falling->col;
		int py = game->falling->row;
		
		if (can_move(game, game->falling, 0, 1)) {
			game->falling->row++;
		} else {
			game->field[py][px] = game->falling;
			game->falling = create_piece();

			disappear_line(game, py);
		}
	}

	return process_piece_logic;
}

// TODO: rate limit keyboard
bool process_kbd(game_data *game, bool *done) {
	if (key[ALLEGRO_KEY_ESCAPE])
		*done = true;

	bool changed_state = false;
	if (game->falling) {
		if (key[ALLEGRO_KEY_DOWN] && can_move(game, game->falling, 0, 1)) {
			changed_state = true;
			game->falling->row++;
		}

		if (key[ALLEGRO_KEY_LEFT] && game->falling->col > 0) {
			changed_state = true;
			game->falling->col--;
		}

		if (key[ALLEGRO_KEY_RIGHT] && game->falling->col + 1 < game->field_cols) {
			changed_state = true;
			game->falling->col++;
		}
	}

	// remove the release flag
	for (int i = 0; i < ALLEGRO_KEY_MAX; i++) {
		key[i] &= KEY_SEEN;
	}

	return changed_state;
}

int main() {
	ALLEGRO_EVENT event;
	game_data game_data;

	init_allegro();
	init_objects(&game_data);

	// TODO: debug
	for (int i = 0; i < game_data.field_cols - 1; i++) {
		tile *p = create_piece();
		p->row = game_data.field_rows - 1;
		p->col = i;
		game_data.field[game_data.field_rows - 1][i] = p;
	}
	// TODO: debug

	bool done = false;
	bool redraw = true;
	draw_screen(&game_data);
	al_start_timer(timer);
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

