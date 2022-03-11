
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "emul.h"

#include "tape.h"

#pragma	pack(push, 1)
union tzx_block
{
	uint8_t id;
	struct
	{
		uint8_t id;
		uint16_t pause;
		uint16_t data_len;
		uint8_t data[0];
	} b10;
	struct
	{
		uint8_t id;
		uint16_t pilot;
		uint16_t sync1;
		uint16_t sync2;
		uint16_t zero;
		uint16_t one;
		uint16_t pilot_len;
		uint8_t last_bits;
		uint16_t pause;
		uint8_t data_len[3];
		uint8_t data[0];
	} b11;
	struct
	{
		uint8_t id;
		uint16_t length;
		uint16_t num;
	} b12;
	struct
	{
		uint8_t id;
		uint8_t pulses;
		uint16_t length[0];
	} b13;
	struct
	{
		uint8_t id;
		uint16_t zero;
		uint16_t one;
		uint8_t last_bits;
		uint16_t pause;
		uint8_t data_len[3];
		uint8_t data[0];
	} b14;
	struct
	{
		uint8_t id;
		uint16_t pause;
	} b20;
	struct
	{
		uint8_t id;
		uint8_t length;
		char name[0];
	} b21;
	struct
	{
		uint8_t id;
		int16_t jump;
	} b23;
	struct
	{
		uint8_t id;
		uint16_t num;
	} b24;
	struct
	{
		uint8_t id;
		uint8_t length;
		char text[0];
	} b30;
	struct
	{
		uint8_t id;
		uint8_t time;
		uint8_t length;
		char text[0];
	} b31;
	struct
	{
		uint8_t id;
		uint16_t length;
		uint8_t num;
	} b32;
	struct {
		uint8_t id;
		uint8_t num;
		struct
		{
			uint8_t type;
			uint8_t id;
			uint8_t info;
		} hwinfo[0];
	} b33;
};
#pragma	pack(pop)

struct tzx_pause_block
{
	struct emu_tape_block base;

	unsigned pause;
};

struct tzx_std_block
{
	struct emu_tape_block base;

	unsigned size;
	uint8_t *data;
	unsigned start_state;

	unsigned pilot; /* 2168 */
	unsigned sync1; /* 667 */
	unsigned sync2; /* 735 */
	unsigned zero; /* 855 */
	unsigned one; /* 1710 */
	unsigned pilot_len; /* 8063 header, 3223 data */
	unsigned last_bits; /* 8 */

	unsigned pause;
};

struct tzx_generator_block
{
	struct emu_tape_block base;

	unsigned length;
	unsigned num;
};

struct tzx_pulse_block
{
	struct emu_tape_block base;

	unsigned pulses;
	uint16_t *data;
};

static struct tzx_file
{
	int signal;
	unsigned next;
	union tzx_block *block;

	int next_change;
	unsigned pos;
	int state;
	int state_pos;
	struct
	{
		unsigned start;
		unsigned num;
	} loop;

	int size;
	//char data[0];
	char *data;
} tape_state;

static int signal_tzx( struct emu_tape_block *blk )
{
	return tape_state.signal;
}

static void prepare_block( struct emu_tape_block *blk )
{printf("std block: %p\n",blk);
	struct tzx_std_block *b = (struct tzx_std_block *)blk;

	tape_state.state = b->start_state;
	tape_state.pos = 0;
	tape_state.state_pos = 0;
}

static void prepare_pause( struct emu_tape_block *blk )
{
	struct tzx_pause_block *b = (struct tzx_pause_block *)blk;
	printf("pause block: %p (%d ticks)\n",b,b->pause);
	tape_state.state = 0;
}

static int process_stdblk( struct emu_tape_block *blk )
{
	struct tzx_std_block *b = (struct tzx_std_block *)blk;
	int next_change = 0;

	if ( tape_state.state < 0 )
		tape_state.state = b->start_state;

	switch ( tape_state.state )
	{
		case 0:
			// start block
			if ( tape_state.pos >= b->size )
				return -1;

			tape_state.state_pos = b->pilot_len;
			tape_state.state ++;
		case 1:
			// pilot tone
			tape_state.signal = !tape_state.signal;
			next_change = b->pilot;
			if ( --tape_state.state_pos == 0 )
				tape_state.state ++;
			break;
		case 2:
			tape_state.signal = !tape_state.signal;
			// sync 1
			next_change = b->sync1;
			tape_state.state ++;
			break;
		case 3:
			tape_state.signal = !tape_state.signal;
			// sync 2
			next_change = b->sync2;
			tape_state.state ++;
			break;

		case 4:
			if ( tape_state.pos >= b->size )
			{
				tape_state.signal = !tape_state.signal;
				next_change = cpu_tstates_frame * 50 * b->pause / 1000;
				tape_state.state = 9;
				printf( "tape pause: %g sec\n", b->pause / 1000.0 );
				break;
			}

			tape_state.state_pos = 0;
			tape_state.state ++;

		case 5:
		case 6:
			tape_state.signal = !tape_state.signal;
			if ( b->data[tape_state.pos] & (1 << (7-tape_state.state_pos)) )
				next_change = b->one;
			else
				next_change = b->zero;

			tape_state.state ++;
			break;

		case 7:
			tape_state.state_pos ++;
			if ( (tape_state.state_pos >= 8) ||
				 ((tape_state.pos == (b->size - 1)) &&
				 (tape_state.state_pos >= b->last_bits)) )
			{
				tape_state.state = 4;
				tape_state.pos ++;
			}
			else
				tape_state.state = 5;
			break;

		case 9:
			return -1;
	}

	return next_change;
}


static int process_generator( struct emu_tape_block *blk )
{
	struct tzx_generator_block *b = (struct tzx_generator_block *)blk;

	tape_state.signal = !tape_state.signal;
	if ( tape_state.pos < b->num )
		return b->length;

	return -1;
}

static int process_pulses( struct emu_tape_block *blk )
{
	struct tzx_pulse_block *b = (struct tzx_pulse_block *)blk;

	tape_state.signal = !tape_state.signal;
	if ( tape_state.pos < b->pulses )
		return EMU_LE16(b->data[tape_state.pos ++]);

	return -1;
}

static int process_pause( struct emu_tape_block *blk )
{
	struct tzx_pause_block *b = (struct tzx_pause_block *)blk;

	if ( tape_state.state )
		return -1;

	tape_state.state = 1;
	return b->pause;
}

static struct emu_tape_block *process_b10()
{
	struct tzx_std_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_stdblk;
	blk->base.signal = signal_tzx;

	blk->start_state = 0;
	blk->data = tape_state.block->b10.data;
	blk->size = EMU_LE16(tape_state.block->b10.data_len);

	blk->pilot = 2168;
	blk->sync1 = 667;
	blk->sync2 = 735;
	blk->zero = 855;
	blk->one = 1710;
	blk->pilot_len = tape_state.block->b10.data[0] & 0x80 ? 3223 : 8063;
	blk->last_bits = 8;
	blk->pause = EMU_LE16(tape_state.block->b10.pause);

	tape_state.next += sizeof( tape_state.block->b10 ) + EMU_LE16(tape_state.block->b10.data_len);

	return &blk->base;
}

static struct emu_tape_block *process_b11()
{
	struct tzx_std_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_stdblk;
	blk->base.signal = signal_tzx;

	blk->start_state = 0;
	blk->data = tape_state.block->b11.data;
	blk->size =
		(tape_state.block->b11.data_len[0]) |
		(tape_state.block->b11.data_len[1] << 8) |
		(tape_state.block->b11.data_len[2] << 16);

	blk->pilot = EMU_LE16(tape_state.block->b11.pilot);
	blk->sync1 = EMU_LE16(tape_state.block->b11.sync1);
	blk->sync2 = EMU_LE16(tape_state.block->b11.sync2);
	blk->zero = EMU_LE16(tape_state.block->b11.zero);
	blk->one = EMU_LE16(tape_state.block->b11.one);
	blk->pilot_len = EMU_LE16(tape_state.block->b11.pilot_len);
	blk->last_bits = tape_state.block->b11.last_bits;
	blk->pause = EMU_LE16(tape_state.block->b11.pause);

	tape_state.next += sizeof( tape_state.block->b11 ) + blk->size;

	return &blk->base;
}

static struct emu_tape_block *process_b12()
{
	struct tzx_generator_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_generator;
	//blk->base.signal = signal_tzx;

	tape_state.next += sizeof( tape_state.block->b12 );

	blk->length = EMU_LE16(tape_state.block->b12.length);
	blk->num = EMU_LE16(tape_state.block->b12.num);

	return &blk->base;
}

static struct emu_tape_block *process_b13()
{
	struct tzx_pulse_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_pulses;
	//blk->base.signal = signal_tzx;

	tape_state.next += sizeof( tape_state.block->b13 ) + tape_state.block->b13.pulses * 2;

	blk->pulses = tape_state.block->b13.pulses;
	blk->data = tape_state.block->b13.length;

	return &blk->base;
}

static struct emu_tape_block *process_b14()
{
	struct tzx_std_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_stdblk;
	blk->base.signal = signal_tzx;

	blk->start_state = 5; /* start from data */
	blk->data = tape_state.block->b14.data;
	blk->size =
		(tape_state.block->b14.data_len[0]) |
		(tape_state.block->b14.data_len[1] << 8) |
		(tape_state.block->b14.data_len[2] << 16);

	blk->zero = EMU_LE16(tape_state.block->b14.zero);
	blk->one = EMU_LE16(tape_state.block->b14.one);
	blk->last_bits = tape_state.block->b14.last_bits;
	blk->pause = EMU_LE16(tape_state.block->b14.pause);

	tape_state.next += sizeof( tape_state.block->b14 ) + blk->size;

	return &blk->base;
}


static struct emu_tape_block *process_b20()
{
	struct tzx_pause_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_pause;
	blk->base.step = process_pause;

	tape_state.next += sizeof( tape_state.block->b20 );

	printf( "tzx pause %g sec\n", EMU_LE16(tape_state.block->b20.pause) / 1000.0 );
	blk->pause = tape_state.block->b20.pause
		? cpu_tstates_frame * 50 * EMU_LE16(tape_state.block->b20.pause) / 1000
		: -1;

	return &blk->base;
}

static struct emu_tape_block *process_b21()
{
	tape_state.next += sizeof( tape_state.block->b21 ) + tape_state.block->b21.length;
	printf( "tzx group\n" );
	return NULL;
}

static struct emu_tape_block *process_b22()
{
	tape_state.next += 1;
	printf( "tzx group end\n" );
	return NULL;
}

static struct emu_tape_block *process_b23()
{
	tape_state.next += (int16_t)EMU_LE16(tape_state.block->b23.jump);
	printf( "tzx jump\n" );
	return NULL;
}

static struct emu_tape_block *process_b24()
{
	tape_state.next += sizeof( tape_state.block->b24 );
	tape_state.loop.start = tape_state.next;
	tape_state.loop.num = EMU_LE16(tape_state.block->b24.num);
	printf( "tzx loop start\n" );
	return NULL;
}

static struct emu_tape_block *process_b25()
{
	tape_state.next += 1;

	if ( tape_state.loop.start && (-- tape_state.loop.num) )
	{
		tape_state.next = tape_state.loop.start;
		printf( "tzx loop left=%d\n", tape_state.loop.num );
	}
	else
	{
		printf( "tzx loop end\n" );
		tape_state.loop.start = 0;
	}

	return NULL;
}

static struct emu_tape_block *process_b30()
{
	tape_state.next += sizeof( tape_state.block->b30 ) + tape_state.block->b30.length;

	return NULL;
}

static struct emu_tape_block *process_b31()
{
	struct tzx_pause_block *blk = calloc( 1, sizeof(*blk) );

	if ( !blk )
		return NULL;

	blk->base.prepare = prepare_block;
	blk->base.step = process_pause;
	//blk->base.signal = signal_tzx;

	tape_state.next += sizeof( tape_state.block->b31 ) + tape_state.block->b31.length;

	printf( "tzx message (%d sec): ", tape_state.block->b31.time );
	fwrite( tape_state.block->b31.text, 1, tape_state.block->b31.length, stdout );
	printf( "\n" );
	blk->pause = cpu_tstates_frame * 50 * tape_state.block->b31.time / 1000;

	return &blk->base;
}

static struct emu_tape_block *process_b32()
{
	tape_state.next += EMU_LE16(tape_state.block->b32.length) + 3;

	return NULL;
}

static struct emu_tape_block *process_b33()
{
	tape_state.next += sizeof(tape_state.block->b33);
	tape_state.next += tape_state.block->b33.num * sizeof(tape_state.block->b33.hwinfo[0]);

	return NULL;
}

static int process_tzx_blocks( struct tzx_file *file )
{
	struct emu_tape_block *blk;

	while ( tape_state.next < tape_state.size )
	{
		tape_state.block = (union tzx_block *)(tape_state.data + tape_state.next);
		printf("tzx block: %x (%d)\n", tape_state.block->id, tape_state.signal);

		tape_state.next_change = 0;
		switch ( tape_state.block->id )
		{
			case 0x10: blk = process_b10(); break;
			case 0x11: blk = process_b11(); break;
			case 0x12: blk = process_b12(); break;
			case 0x13: blk = process_b13(); break;
			case 0x14: blk = process_b14(); break;

			case 0x20: blk = process_b20(); break;
			case 0x21: blk = process_b21(); break;
			case 0x22: blk = process_b22(); break;
			//case 0x23: blk = process_b23(); break;
			case 0x24: blk = process_b24(); break;
			case 0x25: blk = process_b25(); break;

			case 0x30: blk = process_b30(); break;
			case 0x31: blk = process_b31(); break;
			case 0x32: blk = process_b32(); break;
			case 0x33: blk = process_b33(); break;

			default:
				fprintf( stderr, "unknown tzx block: %.2X\n", tape_state.block->id );
				return -1;
		}

		if ( blk )
			emu_tape_add( blk );
	}

	return 0;
}

int load_tzx( const char *flname )
{
	FILE *fp;
	char tmp[8] = { 0 };

	fp = fopen( flname, "rb" );
	if ( !fp )
		return -1;

	fread( tmp, 8, 1, fp );
	if ( strncmp( tmp, "ZXTape!\x1A", 8 ) )
	{
		fclose( fp );
		return -1;
	}

	memset( &tape_state, 0, sizeof(tape_state) );

	fseek( fp, 0, SEEK_END );
	tape_state.size = ftell( fp );

	fseek( fp, 0, SEEK_SET );
	if ( tape_state.data )
		free( tape_state.data );
	tape_state.data = mem_alloc( tape_state.size );
	fread( tape_state.data, tape_state.size, 1, fp );

	fclose( fp );

	tape_state.next = 0xA;
	tape_state.block = NULL;

	process_tzx_blocks( NULL );
	/*tape_process = process_tzx;
	tape_signal = signal_tzx;*/

	return 0;
}
