#include <stdio.h>
#include <stdint.h>
#include "emul.h"

#include "tape.h"

struct tap_file
{
	uint8_t *data;
	unsigned size;
	int signal;
};

struct tap_block
{
	struct emu_tape_block base;

	struct tap_file *file;
	unsigned size;
	uint8_t *data;

	unsigned pilot; /* 2168 */
	unsigned sync1; /* 667 */
	unsigned sync2; /* 735 */
	unsigned zero; /* 855 */
	unsigned one; /* 1710 */
	unsigned pilot_len; /* 8063 header, 3223 data */

	unsigned pause;

	int pos;
	int state;
	int state_pos;
};

static struct tap_file tap_f;

static void tap_prepare( struct emu_tape_block *blk )
{
	struct tap_block *b = (struct tap_block *)blk;

	b->pos = 0;
	b->state = 0;
	b->state_pos = 0;
}

static int tap_step( struct emu_tape_block *blk )
{
	struct tap_block *b = (struct tap_block *)blk;
	struct tap_file *f = b->file;
	int next_change = 0;

	switch ( b->state )
	{
		case 0:
			// start block
			if ( b->pos >= b->size )
				return -1;

			//tape_sig = 0;
			b->state_pos = (b->data[b->pos] & 0x80) ? 3223 : 8063;
			next_change = 2168;
			b->state ++;
			break;
		case 1:
			// pilot tone
			next_change = 2168;
			f->signal = !f->signal;
			if ( --b->state_pos == 0 )
				b->state ++;
			break;
		case 2:
			// sync 1
			next_change = 667;
			f->signal = !f->signal;
			b->state ++;
			break;
		case 3:
			// sync 2
			next_change = 735;
			f->signal = !f->signal;
			b->state ++;
			break;
		case 4:
			// new byte
			if ( b->pos < b->size )
			{
				b->state_pos = 7;
				b->state ++;
			}
			else
			{
				b->state = 0;
				f->signal = !f->signal;
				next_change = cpu_tstates_frame * 50 * 1;	// pause
			}
			break;
		case 5:
		case 6:
			// bit
			next_change = (b->data[b->pos] & (1 << b->state_pos)) ? 1710 : 855;
			f->signal = !f->signal;
			b->state ++;
			break;
		case 7:
			if ( b->state_pos -- )
				b->state = 5;
			else
			{
				b->pos ++;
				b->state = 4;
			}
			break;
	}

	return next_change;
}

static int tap_signal( struct emu_tape_block *blk )
{
	struct tap_block *b = (struct tap_block *)blk;
	struct tap_file *f = b->file;

	return f->signal;
}

/*static int process_tap( unsigned long cycles )
{
	static int next_change;

	if ( next_change < 0 )
		next_change = 0;

	while ( cycles >= next_change )
	{
		add_sound_new( &tape_out, tape_out.measure + next_change, MONO(tape_sig ? tape_vol : -tape_vol) );

		cycles -= next_change;
		//next_change = tap_step();
		if ( next_change < 0 )
			return -1;
	}
	next_change -= cycles;
	add_sound_new( &tape_out, tape_out.measure + cycles, MONO(tape_sig ? tape_vol : -tape_vol) );

	return 0;
}*/

static int process_tap_blocks( struct tap_file *file )
{
	unsigned pos = 0;

	//emu_tape_clear();

	while ( pos < file->size )
	{
		struct tap_block *blk;

		blk = calloc( 1, sizeof(*blk) );
		if ( !blk )
			return -1;

		blk->file = file;
		blk->base.prepare = tap_prepare;
		blk->base.step = tap_step;
		blk->base.signal = tap_signal;

		blk->size = file->data[pos ++];
		blk->size |= file->data[pos ++] << 8;

		blk->pilot = 2168;
		blk->sync1 = 667;
		blk->sync2 = 735;
		blk->zero = 855;
		blk->one = 1710;
		blk->pilot_len = file->data[pos] & 0x80 ? 3223 : 8063;
		//blk->last_bits = 8;
		blk->pause = cpu_tstates_frame * 50 * 2;

		blk->data = &file->data[pos];

		pos += blk->size;

		emu_tape_add( &blk->base );
	}

	return 0;
}

int load_zxtap( const char *flname )
{
	FILE *fp;

	fp = fopen( flname, "rb" );
	if ( !fp )
		return ( -1 );

	fseek( fp, 0, SEEK_END );
	tap_f.size = ftell( fp );

	fseek( fp, 0, SEEK_SET );
	if ( tap_f.data )
		free( tap_f.data );


	tap_f.data = mem_alloc( tap_f.size );
	fread( tap_f.data, tap_f.size, 1, fp );

	fclose( fp );

	/*tape_state = 0;
	tape_pos = 0;*/

	process_tap_blocks( &tap_f );
	//tape_process = process_tap;
	//tape_signal = signal_tap;

	return 0;
}
