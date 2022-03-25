#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tape.h"

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
	struct tape_block *block;
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
	tape_add_block( block );

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
	tape_add_block( block );

	if ( pos < size )
		return pos;

	return 0;
}

static int parse_hex_block( const unsigned char *data, long size )
{
	struct tape_block *block;
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
	tape_add_block( block );
	
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
	tape_add_block( block );

	if ( pos < size )
		return pos;

	return 0;
}

static int parse_raw_block( const unsigned char *data, long size )
{
	struct tape_block *block;
	long pos = 0;
	int i;
	
	block = tape_std_block();
	if ( !block )
		return -1;
	
	block->data = data;
	block->size = size;
	strcpy( block->name, "RAW" );
	//block->stopbits = 6;
	
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
					tape_add_block( block );
					return pos;
				}
			}
			i = 0;
		}
	}
	
	/* just use all data */
	tape_add_block( block );

	return 0;
}

int process_basic( const unsigned char *data, long size )
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

int process_raw( const unsigned char *data, long size )
{
	struct tape_block *block;
	
	block = tape_std_block();
	if ( !block )
		return -1;

	block->data = data;
	block->size = size;
	tape_add_block( block );
	
	return 0;
}
