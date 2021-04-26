#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tape.h"

static int parse_basic_block( const unsigned char *data, long size )
{
	struct tape_block *block;
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
	tape_add_block( block );

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
	struct tape_block *block;
	
	block = tape_std_block();
	if ( !block )
		return -1;

	block->data = data;
	block->size = size;
	tape_add_block( block );
	
	return 0;
}
