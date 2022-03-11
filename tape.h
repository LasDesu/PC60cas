#ifndef PC60_TAPE_H
#define PC60_TAPE_H

#include "formats/tape.h"

struct audio_render;
struct audio_render
{
	void (*output)( struct audio_render *render, short sample );
	float (*gen)( float x );
	
	float qerr;
};

extern int invert_sound;
extern int waveform;

int produce_wav( const char *outfile, struct emu_tape_block *blocks );

#endif /*  PC60_TAPE_H */
