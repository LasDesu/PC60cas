#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "tape.h"

#define DEBUG_WAVEWORM 1500

/* common sound stuff */

static unsigned sample_rate = 44100;
int invert_sound = 0;

static float gen_square( float x )
{
	x = fmodf( x, M_PI*2 );
	if ( x < 0 )
		x = M_PI*2 - x;
	
	return (x < M_PI) ? 1.0 : -1.0;
}

static void generate_silence( struct audio_render *render, unsigned length )
{
	signed short sample = 0;
	unsigned samples = sample_rate * length / 1000;
	unsigned i;

	while ( samples -- )
		render->output( render, sample );
	
	render->qerr = 0.0;
}

static void generate_tone( struct audio_render *render, float length, unsigned freq )
{
	unsigned samples;
	float period = sample_rate / (float)freq;
	int i;

	//length -= render->qerr;
	samples = sample_rate * length / 1000;
	for ( i = 0; i < samples; i ++ )
	{
		signed short sample;
		float val;

		val = render->gen( M_PI*2 * (i / period) );
#ifdef DEBUG_WAVEWORM
		if ( freq > DEBUG_WAVEWORM ) val *= 0.9;
#endif

		sample = val * 32765 * (invert_sound ? -1 : 1);
		render->output( render, sample );
	}
	
	//render->qerr = length - samples * 1000.0 / sample_rate;
}

#define TONE_ZERO	(baud)
#define TONE_ONE	((baud) * 2)

static void generate_byte( struct audio_render *render, unsigned char data, unsigned baud, unsigned stopbits )
{
	float period = 1000.0 / baud;
	int i;

	/* start bit */
	generate_tone( render, period, TONE_ZERO );

	for ( i = 0; i < 8; i ++ )
	{
		generate_tone( render, period, (data & 1) ? TONE_ONE : TONE_ZERO );
		data >>= 1;
	}

	/* stop bits */
	generate_tone( render, period * stopbits, TONE_ONE );
}

static void process_block( struct audio_render *render, struct tape_block *block )
{
	int i;

	generate_silence( render, block->silence );

	/* pilot */
	generate_tone( render, block->pilot, block->baud * 2 );

	/* data */
	for ( i = 0; i < block->size; i ++ )
		generate_byte( render, block->data[i], block->baud, block->stopbits + (i & 1 ? 1 : 0) );

	/* tail */
	generate_tone( render, block->tail, block->baud * 2 );
}

/* WAVE stuff */

struct audio_render_wav
{
	struct audio_render base;
	FILE *fp;
	unsigned long samples;
};

#define WAVE_FORMAT_PCM 0x0001

#pragma pack(push,1)
struct wavheader
{
	char chunkId[4];
	uint32_t chunkSize;
	char format[4];
	struct
	{
		char subchunk1Id[4];
		uint32_t subchunk1Size;
		uint16_t audioFormat;
		uint16_t numChannels;
		uint32_t sampleRate;
		uint32_t byteRate;
		uint16_t blockAlign;
		uint16_t bitsPerSample;
	};
	struct
	{
		char subchunk2Id[4];
		uint32_t subchunk2Size;
		char data[0];
	};
};
#pragma pack(pop)

static const struct wavheader wav_template =
	{
		"RIFF", 44 - 8, "WAVE",
		{
			"fmt ", 16,
			WAVE_FORMAT_PCM, 1, 44100,
			2 * 1 * 44100, 2 * 1, 16
		},
		{
			"data", 0
		}
	};

static void file_write_sample( struct audio_render *_r, short sample )
{
	struct audio_render_wav *render = (struct audio_render_wav *)_r;

	fwrite( &sample, sizeof(sample), 1, render->fp );
	render->samples ++;
}

int produce_wav( const char *outfile, struct tape_block *block, int square )
{
	FILE *fp;
	struct wavheader header = wav_template;
	struct audio_render_wav render;
	int ret;

	fp = fopen( outfile, "wb" );
	if ( !fp )
	{
		fprintf( stderr, "Failed to open '%s' for writing\n", outfile );
		return -1;
	}

	/* prepare header */
	header.sampleRate = sample_rate;
	header.numChannels = 1;
	header.bitsPerSample = 16;
	header.blockAlign = header.bitsPerSample / 8 * header.numChannels;
	header.byteRate = header.blockAlign * header.sampleRate;

	/* write dummy header for now */
	fwrite( &header, sizeof(header), 1, fp );

	/* prepare renderer */
	render.base.output = file_write_sample;
	if ( square )
		render.base.gen = gen_square;
	else
		render.base.gen = sinf;	/* sine wave really is overkill, but looks pretty */
	render.fp = fp;
	render.samples = 0;

	while ( block )
	{
		process_block( &render.base, block );
		block = block->next;
	}

	/* update header */
	header.subchunk2Size += render.samples * 2;
	header.chunkSize += header.subchunk2Size;

	/* TODO: host endianess */

	fseek( fp, 0, SEEK_SET );
	fwrite( &header, sizeof(header), 1, fp );

	fclose( fp );
	printf( "Wave file '%s' produced\n", outfile );

	return 0;
}
