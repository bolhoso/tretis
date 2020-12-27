all: tetris

tetris: tetris.c
	gcc -lm $$(pkg-config allegro-5 allegro_acodec-5 allegro_font-5 allegro_image-5 allegro_primitives-5 allegro_audio-5 --libs --cflags) tetris.c -o tetris 
