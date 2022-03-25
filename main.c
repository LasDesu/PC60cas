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
static int square = 0;

static int filetype = -1;
static int baud = 1200;

static struct tape_block *blocks;
static struct tape_block **last_block = &blocks;

struct tape_block *tape_std_block()
{
	struct tape_block *block;

	block = calloc( 1, sizeof(*block) );
	if ( !block )
		return NULL;

	block->baud = baud;

	block->silence = 500;
	block->pilot = 4808;
	block->tail = 100;
	block->stopbits = 3;

	return block;
}

void tape_add_block( struct tape_block *block )
{
	*last_block = block;
	block->next = NULL;
	last_block = &block->next;
}

static void print_blocks( const unsigned char *origin, struct tape_block *b )
{
	int i;

	for ( i = 0; b; b = b->next, i ++ )
	{
		printf( "Block #%d:", i + 1 );
		if ( b->name[0] )
			printf( " \"%s\"\n", b->name );
		else
			printf( "\n" );
		printf( "  offset: 0x%X\n", (unsigned)(b->data - origin) );
		printf( "  size:   %d bytes\n", b->size );
	}
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

	while( ( c = getopt( argc, argv, "t:ipb:s" ) ) != -1 )
	{
		switch( c )
		{
			case 't':
				filetype = atoi( optarg );
				break;
			case 'b':
				baud = atoi( optarg );
				break;
			case 'i':
				invert_sound = 1;
				break;
			case 'p':
				play = 1;
				break;
			case 's':
				square = 1;
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
	if ( (filetype > 2) || (filetype < -1) )
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

	if ( play )
	{
		fprintf( stderr, "Playing is not supported yet!\n" );
		return -1;
	}

	return 0;
}

int main( int argc, char *argv[] )
{
	FILE *fp = NULL;
	unsigned char *data = NULL;
	long datasize;
	int rc = 0;

	if ( process_options( argc, argv ) )
		return -1;

	fp = fopen( infile, "rb" );
	if ( !fp )
	{
		fprintf( stderr, "Failed to open '%s'!\n", infile );
		return -1;
	}

	/* get file size */
	fseek( fp, 0, SEEK_END );
	datasize = ftell( fp );

	if ( datasize > 64*1024*1024 )
	{
		fprintf( stderr, "Input file too big! Can't be P6 cassette.\n" );
		rc = -1;
		goto out;
	}

	/* read data */
	data = malloc( datasize );
	if ( !data )
	{
		fprintf( stderr, "Failed to allocate memory for data (%d).\n", errno );
		rc = -1;
		goto out;
	}

	fseek( fp, 0, SEEK_SET );
	if ( fread( data, 1, datasize, fp ) != datasize )
	{
		fprintf( stderr, "Failed to read all data from file (%d).\n", errno );
		rc = -1;
		goto out;
	}

	fclose( fp );
	fp = NULL;

	/* process file */
	if ( filetype < 0 )
	{
		/* determine type based on name */
		const char *ext = get_file_ext( infile );

		if ( ext )
		{
			if ( strcasecmp( ext, "p6" ) == 0 )
				filetype = 1;
			else if ( strcasecmp( ext, "cas" ) == 0 )
				filetype = 1;
			else if ( strcasecmp( ext, "p6t" ) == 0 )
				filetype = 2;
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
			printf( "Processing raw data file...\n" );
			process_raw( data, datasize );
			break;
		case 1:
			printf( "Processing BASIC file...\n" );
			process_basic( data, datasize );
			break;
		default:
			fprintf( stderr, "File type not yet supported.\n" );
			break;
	}

	if ( !blocks )
	{
		fprintf( stderr, "No blocks processed.\n" );
		rc = -1;
		goto out;
	}
	else
		print_blocks( data, blocks );

	if ( !play )
		produce_wav( outfile, blocks, square );
	/*else
		play_tape( blocks );*/

out:
	if ( data )
		free( data );
	if ( fp )
		fclose( fp );

	return rc;
}
