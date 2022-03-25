#ifndef PC60_TAPE_H
#define PC60_TAPE_H

struct tape_block;

struct tape_block
{
	struct tape_block *next;

	const unsigned char *data;
	unsigned size;
	char name[7];

	unsigned baud;
	unsigned silence;
	unsigned pilot;
	unsigned tail;
	unsigned stopbits;
};

struct audio_render
{
	void (*output)( struct audio_render *render, short sample );
	float (*gen)( float x );
	
	float qerr;
};

extern int invert_sound;

struct tape_block *tape_std_block();
void tape_add_block( struct tape_block *block );

int process_basic( const unsigned char *data, long size );
int process_raw( const unsigned char *data, long size );

int produce_wav( const char *outfile, struct tape_block *blocks, int square );

#endif /*  PC60_TAPE_H */
