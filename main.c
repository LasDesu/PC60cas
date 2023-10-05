#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libgen.h>

#include "tape.h"

static char *infile = NULL;
static char *outfile= NULL;

static int play = 0;

static int filetype = -1;

struct emu_tape_block *emu_tape_blocks = NULL;

void emu_tape_clear()
{
	struct emu_tape_block *b = NULL;

	b = emu_tape_blocks;
	while ( b )
	{
		struct emu_tape_block *tmp = b;

		if ( b->name )
			free( b->name );
		b = b->next;

		if ( tmp->fini )
			tmp->fini( tmp );
		else
			free( tmp );
	}
	emu_tape_blocks = NULL;
}

void emu_tape_add( struct emu_tape_block *blk )
{
	struct emu_tape_block *b = NULL;

	blk->next = NULL;

	if ( !emu_tape_blocks )
	{
		emu_tape_blocks = blk;
		return;
	}

	b = emu_tape_blocks;
	while ( b->next )
	{
		b = b->next;
	}
	b->next = blk;
}

static const char *get_file_ext( char *name )
{
	const char *p;

	name = basename( name );

	p = strrchr( name, '.' );
	if ( !p )
		return NULL;

	/* ignore hidden files */
	if ( p == name )
		return NULL;

	return p + 1;
}

static int process_options( int argc, char *argv[] )
{
	int c;

	while( ( c = getopt( argc, argv, "t:ips" ) ) != -1 )
	{
		switch( c )
		{
			case 't':
				filetype = atoi( optarg );
				break;
			case 'i':
				invert_sound = 1;
				break;
			case 'p':
				play = 1;
				break;
			case 's':
				waveform = 1;
				break;
			/*case 'b':
				add_block( strtol( optarg, NULL, 0 ) );
				break;*/
		}
	}

	/* check for input file */
	if ( optind >= argc )
	{
		fprintf( stderr, "No input file specified!\n" );
		return -1;
	}
	infile = argv[ optind ++ ];

	/* check if file type is valid */
	if ( (filetype > 1) || (filetype < -1) )
	{
		fprintf( stderr, "Invalid file type (%d)!\n", filetype );
		return -1;
	}

	/* check for output file */
	if ( !play )
	{
		if ( optind < argc )
			outfile = argv[ optind ++ ];
		else
		{
			const char *ext = get_file_ext( infile );

			outfile = calloc( 1, strlen(infile) + 5 );
			if ( ext )
				strncpy( outfile, infile, ext - infile - 1 );
			else
				strcpy( outfile, infile );
			strcat( outfile, ".wav" );
		}
	}

	/* check for extra arguments */
	if ( optind < argc )
	{
		fprintf( stderr, "Excessive command line options\n" );
		return -1;
	}

	return 0;
}

int main( int argc, char *argv[] )
{
	int rc = 0;

	waveform = 0;
	invert_sound = 0;

	if ( process_options( argc, argv ) )
		return -1;

	/* process file */
	if ( filetype < 0 )
	{
		/* determine type based on name */
		const char *ext = get_file_ext( infile );

		if ( ext )
		{
			if ( strcasecmp( ext, "tap" ) == 0 )
				filetype = 0;
			else if ( strcasecmp( ext, "tzx" ) == 0 )
				filetype = 1;
			else if ( strcasecmp( ext, "cas" ) == 0 )
				filetype = 2;
			else if ( strcasecmp( ext, "bin" ) == 0 )
				filetype = 3;
		}
		
		if ( filetype < 0 )
		{
			fprintf( stderr, "Could not detect file type. Specify type using -t option.\n" );
			rc = -1;
			goto out;
		}
	}

	switch ( filetype )
	{
		case 0:
			printf( "Processing TAP file...\n" );
			load_zxtap( infile );
			break;
		case 1:
			printf( "Processing TZX file...\n" );
			load_zxtzx( infile );
			break;
		case 2:
			printf( "Processing PC60 cas file...\n" );
			load_pc60cas( infile );
			break;
		case 3:
			printf( "Processing BK-001x bin file...\n" );
			load_bkbin( infile );
			break;
		default:
			fprintf( stderr, "File type not yet supported.\n" );
			break;
	}

	if ( !emu_tape_blocks )
	{
		fprintf( stderr, "No blocks processed.\n" );
		rc = -1;
		goto out;
	}

	if ( !play )
		produce_wav( outfile, emu_tape_blocks );
	else
		play_tape( emu_tape_blocks );

out:

	return rc;
}
