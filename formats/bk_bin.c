#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include "emul.h"

#include "tape.h"

#define SAMPLE_RATE_10 21428
#define SAMPLE_RATE_11 25000

#define TUNE_COUNT 4096
#define TUNE_COUNT_SECOND 10
#define TUNE_COUNT_END 200

#define ADDRESS_MIN 0320
#define ADDRESS_MAX 0177600

struct bk_block
{
	struct emu_tape_block base;

	const unsigned char *data;
	unsigned size;
	unsigned num;
	int end;
	
	int pos;
	int state;
	long state_pos;
	int signal;

	unsigned baud;
	unsigned ts_zero, ts_one, ts_sync;
};

static void bk_prepare( struct emu_tape_block *blk )
{
	struct bk_block *b = (struct bk_block *)blk;

	b->baud = SAMPLE_RATE_10;

	b->pos = 0;
	b->state = 0;
	b->state_pos = 0;
	b->signal = 0;

	b->ts_zero = cpu_tstates_frame * emu_frame_rate / (b->baud / 2) * 1;
	b->ts_one = cpu_tstates_frame * emu_frame_rate / (b->baud / 2) * 2;
	b->ts_sync = cpu_tstates_frame * emu_frame_rate / (b->baud / 2) * 3;
}

static int bk_pilot_step( struct emu_tape_block *blk )
{
	struct bk_block *b = (struct bk_block *)blk;

	switch ( b->state )
	{
		case 0:
			b->state_pos = b->num - 1;
			b->signal = 0;
			b->state ++;
			return b->ts_zero;
		case 1:
			b->signal = !b->signal;
			if ( --b->state_pos == 0 )
				b->state ++;
			return b->ts_zero;

		case 2:
			if ( b->end )
				return -1;
		case 3:
			b->signal = !b->signal;
			b->state ++;
			return b->ts_sync;

		case 4:
		case 5:
			b->signal = !b->signal;
			b->state ++;
			return b->ts_one;

		case 6:
		case 7:
			b->signal = !b->signal;
			b->state ++;
			return b->ts_zero;
	}

	return -1;
}

static int bk_data_step( struct emu_tape_block *blk )
{
	struct bk_block *b = (struct bk_block *)blk;

	switch ( b->state )
	{
		case 0:
			// start block
			if ( b->pos >= b->size )
				return -1;
			
			b->state ++;
			b->signal = 1;
			b->state_pos = 0;

		case 1:
		case 2:
			b->signal = !b->signal;
			b->state ++;
			if ( b->data[b->pos] & (1 << b->state_pos) )
				return b->ts_one;
			return b->ts_zero;

		case 3:
			b->signal = 0;
			b->state ++;
			return b->ts_zero;
		case 4:
			b->signal = !b->signal;
			
			b->state_pos ++;
			if ( b->state_pos < 8 )
			{
				b->state = 1;
			}
			else
			{
				b->state = 0;
				b->pos ++;
			}
			return b->ts_zero;
	}

	return 0;
}

static int bk_signal( struct emu_tape_block *blk )
{
	struct bk_block *b = (struct bk_block *)blk;

	return b->signal;
}

static struct bk_block *tape_data_block( const unsigned char *data, unsigned size )
{
	struct bk_block *blk;

	blk = calloc( 1, sizeof(*blk) );
	if ( !blk )
		return NULL;
	
	blk->base.prepare = bk_prepare;
	blk->base.step = bk_data_step;
	blk->base.signal = bk_signal;
	
	blk->data = data;
	blk->size = size;

	return blk;
}

static struct bk_block *tape_pilot_block( int num )
{
	struct bk_block *blk;

	blk = calloc( 1, sizeof(*blk) );
	if ( !blk )
		return NULL;
	
	blk->base.prepare = bk_prepare;
	blk->base.step = bk_pilot_step;
	blk->base.signal = bk_signal;
	
	blk->num = num * 2;
	blk->end = 0;

	return blk;
}

int process_bin( const unsigned char *data, long size, const char *flname )
{
	struct bk_block *block;
	char *hdr, *cksumbuf;
	unsigned dsize, cksum;
	unsigned i, drop;
	
	/* check file here */
	if ( size < 6 )
		return -1;

	dsize = data[2] | (data[3] << 8);
	if ( dsize != size - 4 )
		return -1;

	hdr = calloc( 1, 20 );
	cksumbuf = calloc( 1, 2 );
	
	/* produce name */
	memcpy( hdr, data, 4 );
	strncpy( hdr + 4, flname, 16 );
	drop = 0;
	for ( i = 4; i < 20; i ++ )
	{
		if ( hdr[i] == '\0' )
			drop = 1;
		else if ( hdr[i] == '.' )
			drop = 1;
		
		if ( drop || hdr[i] < ' ' )
			hdr[i] = ' ';
		else
			hdr[i] = toupper( hdr[i] );
	}

	data += 4;

	/* calculate checksum */
	cksum = 0;
	for ( i = 0; i < dsize; i ++ )
	{
		cksum += data[i];
		if ( cksum > 0xFFFF )
			cksum -= 0xFFFF; /* handle overflow and add carry */
	}
	cksumbuf[0] = cksum;
	cksumbuf[1] = cksum >> 8;

	/* pilot */
	block = tape_pilot_block( TUNE_COUNT );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	/* start marker */
	block = tape_pilot_block( TUNE_COUNT_SECOND );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	/* header */
	block = tape_data_block( hdr, 20 );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	/* sync */
	block = tape_pilot_block( TUNE_COUNT_SECOND );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	/* data */
	block = tape_data_block( data, dsize );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	/* crc */
	block = tape_data_block( cksumbuf, 2 );
	if ( !block )
		return -1;
	emu_tape_add( &block->base );

	block = tape_pilot_block( TUNE_COUNT_END );
	if ( !block )
		return -1;
	block->end = 1;
	emu_tape_add( &block->base );

	return 0;
}

int load_bkbin( const char *flname )
{
	FILE *fp;
	unsigned char *data = NULL;
	char *p;
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

	p = strrchr( flname, '/' );
	process_bin( data, datasize, p ? p + 1 : flname );

	return 0;
}
