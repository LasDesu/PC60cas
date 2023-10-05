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
	unsigned stopbits;

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
			b->state_pos = b->stopbits * 4 - 1;
			if ( (b->pos & 15) == 0 )
				b->state_pos += 30;
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
	blk->stopbits = 3;

	return blk;
}

enum
{
	TYPE_UNKNOWN = -1,
	TYPE_NULL = 0,
	TYPE_RAW,
	TYPE_BASIC,
	TYPE_HEX
};

static int parse_guess_type( const unsigned char *data, long size )
{
	long pos;
	int i;
	
	/* skip zeroes */
	for ( pos = 0; pos < size; pos ++ )
	{
		if ( data[pos] != 0x00 )
			break;
	}
	
	if ( pos >= size )
		return TYPE_NULL;
	
	i = 0;
	if ( data[pos] == 0xD3 )
	{
		/* probably basic, read 10 bytes to confirm */
		while ( pos < size )
		{
			if ( data[pos ++] == 0xD3 )
				i ++;
			else
				i = 0;

			if ( i == 10 )
				return TYPE_BASIC;	/* got it */
		}
	}
	else if ( data[pos] == 0x9C )
	{
		/* probably hex, read 6 bytes to confirm */
		while ( pos < size )
		{
			if ( data[pos ++] == 0x9C )
				i ++;
			else
				i = 0;

			if ( i == 6 )
				return TYPE_HEX;	/* got it */
		}
	}
	
	/* fallback: unknown raw data */
	return TYPE_RAW;
}


static int parse_basic_block( const unsigned char *data, long size )
{
	struct pc60_block *block;
	long pos = 0;
	int i;
	
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
	block->tail = 10;
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
	strcpy( block->name, "<data>" );
	block->silence = 0;
	block->pilot = 500;

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
					int t = parse_guess_type( &data[pos - 1], size - pos + 1 );
					if ( (t == TYPE_BASIC) || (t == TYPE_HEX) )
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

static int parse_hex_block( const unsigned char *data, long size )
{
	struct pc60_block *block;
	long pos = 0;
	int i;
	
	/* find HEX header */
	i = 0;
	while ( pos < size )
	{
		if ( data[pos ++] == 0x9C )
			i ++;
		else
			i = 0;

		if ( i == 6 )
			break;
	}
	if ( (i != 6) )
	{
		fprintf( stderr, "Failed to find HEX block\n" );
		return -1;
	}

	block = tape_std_block();
	if ( !block )
		return -1;
	block->data = &data[pos - 6];
	block->size = 6;
	block->tail = 10;
	strcpy( block->name, "HEX" );
	emu_tape_add( &block->base );
	
	block = tape_std_block();
	if ( !block )
		return -1;
	block->data = &data[pos];
	strcpy( block->name, "<data>" );
	block->silence = 0;
	block->pilot = 5;

	while ( pos < size )
	{
		if ( data[pos] == 0x00 )
			break;
		/*if ( data[pos] == 0xFF )
			break;*/
		pos ++;
	}
	block->size = pos - 6;
	emu_tape_add( &block->base );

	if ( pos < size )
		return pos;

	return 0;
}

static int parse_raw_block( const unsigned char *data, long size )
{
	struct pc60_block *block;
	long pos = 0;
	int i;
	
	block = tape_std_block();
	if ( !block )
		return -1;
	
	block->data = data;
	block->size = size;
	strcpy( block->name, "RAW" );
	//block->stopbits = 10;
	
	/* try to find a lot of zeroes, possibly we'll find known block */
	i = 0;
	for ( pos = 0; pos < size; pos ++ )
	{
		if ( data[pos] == 0x00 )
			i ++;
		else
		{
			if ( i >= 12 )
			{
				int t = parse_guess_type( data + pos, size - pos );
				if ( (t == TYPE_BASIC) || (t == TYPE_HEX) )
				{
					block->size = pos;
					emu_tape_add( &block->base );
					return pos;
				}
			}
			i = 0;
		}
	}
	
	/* just use all data */
	emu_tape_add( &block->base );

	return 0;
}

static int process_basic( const unsigned char *data, long size )
{
	long pos;
	int type;

	while ( size )
	{
		type = parse_guess_type( data, size );

		switch ( type )
		{
			case TYPE_BASIC:
				pos = parse_basic_block( data, size );
				break;
			case TYPE_HEX:
				pos = parse_hex_block( data, size );
				break;
			case TYPE_RAW:
				pos = parse_raw_block( data, size );
				break;
			case TYPE_NULL:
				pos = 0;
				break;
			default:
				/* shouldn't happen */
				fprintf( stderr, "Fault: guessed unknown block type, shouldn't happen\n" );
				return -1;
		}

		if ( pos < 0 )
			return -1;
		else if ( pos == 0 )
			break;

		data += pos;
		size -= pos;
	}

	return 0;
}

static int process_raw( const unsigned char *data, long size )
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

	data = mem_alloc( datasize );
	fread( data, datasize, 1, fp );

	fclose( fp );

	process_basic( data, datasize );

	return 0;
}
