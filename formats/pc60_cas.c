#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "emul.h"

#include "tape.h"

struct pc60_block
{
	struct emu_tape_block base;

	const unsigned char *data;
	unsigned size;
	char name[7];

	unsigned baud;
	unsigned silence;
	unsigned pilot;
	unsigned tail;
	unsigned stop;

	unsigned ts_zero, ts_one;

	int pos;
	int state;
	long state_pos;
	int signal;
	unsigned char buf;
};

static void pc60_prepare( struct emu_tape_block *blk )
{
	struct pc60_block *b = (struct pc60_block *)blk;

	b->pos = 0;
	b->state = 0;
	b->state_pos = 0;
	b->signal = 0;

	b->ts_zero = cpu_tstates_frame * emu_frame_rate / b->baud / 2;
	b->ts_one = cpu_tstates_frame * emu_frame_rate / b->baud / 4;
}

static int pc60_step( struct emu_tape_block *blk )
{
	struct pc60_block *b = (struct pc60_block *)blk;

	switch ( b->state )
	{
		case 0:
			// start block
			if ( b->pos >= b->size )
				return -1;

			b->state_pos = (long)(b->pilot * b->baud / 500) * 2 - 1;
			b->signal = 0;
			b->state ++;
			return cpu_tstates_frame * 50 * b->silence / 1000;
		case 1:
			b->signal = !b->signal;
			if ( --b->state_pos == 0 )
				b->state ++;
			return b->ts_one;
		
		case 2:
			if ( b->pos >= b->size )
			{
				b->state = 0;
				b->signal = 1;
				return -1;
			}
			b->signal = 0;
			b->state ++;
			return b->ts_zero;
		case 3:
			b->signal = 1;
			b->state ++;
			b->state_pos = 0;
			b->buf = b->data[b->pos ++];
			return b->ts_zero;

		case 4:
			b->signal = 0;
			if ( b->buf & 1 )
			{
				b->state = 6;
				return b->ts_one;
			}
			b->state = 5;
			return b->ts_zero;

		case 5:
			b->signal = 1;
			b->state_pos ++;
			b->buf >>= 1;
			if ( b->state_pos < 8 )
				b->state = 4;
			else
				b->state = 9;
			return b->ts_zero;
		case 6:
		case 7:
			b->signal = !b->signal;
			b->state ++;
			return b->ts_one;
		case 8:
			b->signal = 1;
			b->state_pos ++;
			b->buf >>= 1;
			if ( b->state_pos < 8 )
				b->state = 4;
			else
				b->state = 9;
			return b->ts_one;

		case 9:
			b->signal = 0;
			b->state ++;
			b->state_pos = b->stop * 4 - 1;
			return b->ts_one;
		case 10:
			b->signal = !b->signal;
			if ( --b->state_pos == 0 )
			{
				if ( (b->pos >= b->size) && b->tail )
				{
					b->state = 1;
					b->state_pos = (long)(b->tail * b->baud / 500) * 2 - 1;
				}
				else
					b->state = 2;
			}
			return b->ts_one;
	}

	return 0;
}

static int pc60_signal( struct emu_tape_block *blk )
{
	struct pc60_block *b = (struct pc60_block *)blk;

	return b->signal;
}

static struct pc60_block *tape_std_block()
{
	struct pc60_block *blk;

	blk = calloc( 1, sizeof(*blk) );
	if ( !blk )
		return NULL;
	
	blk->base.prepare = pc60_prepare;
	blk->base.step = pc60_step;
	blk->base.signal = pc60_signal;

	blk->baud = 1200;

	blk->silence = 500;
	blk->pilot = 4000;
	blk->tail = 10;
	blk->stop = 3;

	return blk;
}

static int parse_basic_block( const unsigned char *data, long size )
{
	struct pc60_block *block;
	long pos = 0;
	int i;
	const unsigned char basichdr[10] =
		{ 0xD3, 0xD3, 0xD3, 0xD3, 0xD3, 0xD3, 0xD3, 0xD3, 0xD3, 0xD3 };

	/* find BASIC header */
	i = 0;
	while ( pos < size )
	{
		if ( data[pos ++] == 0xD3 )
			i ++;
		else
			i = 0;

		if ( i == 10 )
			break;
	}
	if ( (i != 10) || (pos + 6 > size) )
	{
		fprintf( stderr, "Failed to find BASIC block\n" );
		return -1;
	}

	block = tape_std_block();
	if ( !block )
		return -1;

	block->data = &data[pos - 10];
	block->size = 10 + 6;	/* 10 bytes header + 6 bytes name */
	strncpy( block->name, &data[pos], 6 );
	emu_tape_add( &block->base );

	/* move to data */
	pos += 6;
	if ( pos >= size )
	{
		fprintf( stderr, "No data after BASIC header\n" );
		return 0;
	}

	/* prepare block */
	block = tape_std_block();
	if ( !block )
		return -1;
	block->data = &data[pos];
	block->silence = 0;
	block->pilot = 500;
	block->tail = 50;

	/* find block end */
	i = 0;
	while ( pos < size )
	{
		if ( data[pos ++] == 0x00 )
			i ++;
		else
		{
			/* got non-zero symbol, analyze */
			if ( i >= 12 )
			{
				/* got sequence of minimum 12 zero bytes */
				if ( pos + 9 < size )
				{
					/* enough data to contain another block */
					if ( memcmp( &data[pos - 1], basichdr, 10 ) == 0 )
					{
						/* got another BASIC block following, save position */
						pos --;
						break;
					}
				}
				else
				{
					/* no real need to analyze these bytes */
					pos = size;
					i = 0;
					break;
				}
			}

			i = 0;
		}
	}

	if ( i < 12 )
		fprintf( stderr, "Bad BASIC block ending\n" );

	block->size = &data[pos] - block->data;
	emu_tape_add( &block->base );

	if ( pos < size )
		return pos;

	return 0;
}

int process_basic( const unsigned char *data, long size )
{
	int pos;

	while ( size )
	{
		pos = parse_basic_block( data, size );
		if ( pos < 0 )
			return -1;
		else if ( pos == 0 )
			break;

		data += pos;
		size -= pos;
	}

	return 0;
}

int process_raw( const unsigned char *data, long size )
{
	struct pc60_block *block;
	
	block = tape_std_block();
	if ( !block )
		return -1;

	block->data = data;
	block->size = size;
	emu_tape_add( &block->base );
	
	return 0;
}

int load_pc60cas( const char *flname )
{
	FILE *fp;
	unsigned char *data = NULL;
	long datasize;

	fp = fopen( flname, "rb" );
	if ( !fp )
		return ( -1 );

	fseek( fp, 0, SEEK_END );
	datasize = ftell( fp );

	fseek( fp, 0, SEEK_SET );
	//if ( data )
		free( data );

	data = mem_alloc( datasize );
	fread( data, datasize, 1, fp );

	fclose( fp );

	process_basic( data, datasize );

	return 0;
}