#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "emul.h"
#include "sound.h"
#include "tape.h"

#define DEBUG_WAVEWORM 1500

/* common sound stuff */
unsigned sample_rate = 44100;
int invert_sound = 0;
int waveform = 0;

SNDFRAME *sound_buffer;
unsigned long bufferFrames;

static float gen_square( float x )
{
	x = fmodf( x, M_PI*2 );
	if ( x < 0 )
		x = M_PI*2 - x;
	
	return (x < M_PI) ? 1.0 : -1.0;
}

static void generate_silence( struct audio_render *render, float length )
{
	signed short sample = 0;
	unsigned samples = sample_rate * length / 1000;
	unsigned i;

	while ( samples -- )
		render->output( render, sample );
	
	render->qerr = 0.0;
}

static void generate_tone( struct audio_render *render, float length, unsigned freq, float phase )
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

		val = render->gen( M_PI*2 * (i / period) + phase );
#ifdef DEBUG_WAVEWORM
		if ( freq > DEBUG_WAVEWORM ) val *= 0.9;
#endif

		sample = val * 32765 * (invert_sound ? -1 : 1);
		render->output( render, sample );
	}
	
	//render->qerr = length - samples * 1000.0 / sample_rate;
}


static void process_block( struct audio_render *render, struct emu_tape_block *block )
{
	int tstates;
	float period;

	if ( !block )
		return;

	if ( block->prepare )
		block->prepare( block );

	while ( block->step )
	{
		tstates = block->step( block );
		if ( tstates < 0 )
			return;

		period = tstates * 1000.0 / cpu_tstates_frame / emu_frame_rate;
		if ( !block->signal || (period >= 20.0) )
			generate_silence( render, period );
		else
			generate_tone( render, period, 500.0 / period, block->signal( block ) ? 0.0 : M_PI );
	}
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

int produce_wav( const char *outfile, struct emu_tape_block *block )
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
	if ( waveform == 0 )
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

struct audio_render_play
{
	struct audio_render base;
	emu_sound_out_t *out;
	unsigned pos;
};

static void audio_write_sample( struct audio_render *_r, short sample )
{
	struct audio_render_play *render = (struct audio_render_play *)_r;
	SNDFRAME *fr = &sound_buffer[render->pos];
	
	fr->l = fr->r = sample;

	if ( (++ render->pos) >= bufferFrames )
	{
		render->out->flush();
		render->pos = 0;
	}
}

void play_tape( struct emu_tape_block *block )
{
	struct audio_render_play render;

	/* prepare renderer */
	render.base.output = audio_write_sample;
	if ( waveform == 0 )
		render.base.gen = gen_square;
	else
		render.base.gen = sinf;	/* sine wave really is overkill, but looks pretty */
	render.pos = 0;

	render.out = &sound_pulse;

	if ( render.out->init )
		render.out->init();	

	while ( block )
	{
		process_block( &render.base, block );
		block = block->next;
	}

	if ( render.out->flush )
		render.out->flush();	
	if ( render.out->uninit )
		render.out->uninit();
}
