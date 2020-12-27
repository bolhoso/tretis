all: tetris

tetris: tetris.c
	gcc -lm $$(pkg-config allegro-5 allegro_acodec-5 allegro_font-5 allegro_image-5 allegro_primitives-5 allegro_audio-5 --libs --cflags) tetris.c -o tetris 
#allegro-5                      allegro - Allegro game programming library
#allegro_acodec-5               allegro_acodec - Allegro game programming library, audio codec addon
#allegro_audio-5                allegro_audio - Allegro game programming library, audio addon
#allegro_color-5                allegro_color - Allegro game programming library, colors addon
#allegro_dialog-5               allegro_dialog - Allegro game programming library, native dialog addon
#allegro_font-5                 allegro_font - Allegro game programming library, font addon
#allegro_image-5                allegro_image - Allegro game programming library, image I/O addon
#allegro_main-5                 allegro_main - Allegro game programming library, magic main addon
#allegro_memfile-5              allegro_memfile - Allegro game programming library, memory files addon
#allegro_physfs-5               allegro_physfs - Allegro game programming library, PhysicsFS addon
#allegro_primitives-5           allegro_primitives - Allegro game programming library, primitives addon
#allegro_ttf-5                  allegro_ttf - Allegro game programming library, TrueType fonts addon
#allegro_video-5                allegro_video - Allegro game programming library, video player addon

