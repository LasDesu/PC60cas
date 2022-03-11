#include <stdio.h>
#include <alsa/asoundlib.h>

#include "sound.h"

static snd_pcm_t *sndh;
static snd_rawmidi_t* midiout = NULL;

static int sound_alsa_init()
{
	int ret;
	snd_pcm_hw_params_t *hw_params;
	unsigned rate = 48000;

	ret = snd_pcm_open( &sndh, "default", SND_PCM_STREAM_PLAYBACK, 0 );
	if ( ret < 0 )
	{
		fprintf (stderr, "cannot open audio device (%s)\n",
				 snd_strerror (ret));
		return ( -1 );
	}

	snd_pcm_hw_params_malloc( &hw_params );
	snd_pcm_hw_params_any( sndh, hw_params );

	snd_pcm_hw_params_set_access( sndh, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED );
	snd_pcm_hw_params_set_format( sndh, hw_params, SND_PCM_FORMAT_S16_LE );
	snd_pcm_hw_params_set_rate_near( sndh, hw_params, &rate, 0 );
	snd_pcm_hw_params_set_channels( sndh, hw_params, 2 );

	bufferFrames = rate / emu_frame_rate;
	sound_buffer = calloc( bufferFrames, sizeof(SNDFRAME) );

	snd_pcm_hw_params_set_periods( sndh, hw_params, 8, 0 );

	snd_pcm_hw_params_set_buffer_size( sndh, hw_params, (1024 * 8) / 2 );

	snd_pcm_hw_params( sndh, hw_params );

	snd_pcm_hw_params_free( hw_params );
	snd_pcm_prepare( sndh );

	snd_pcm_start( sndh );

	return ( 0 );
}

static int sound_alsa_uninit()
{
	snd_pcm_close( sndh );
	snd_rawmidi_close(midiout);
	free( sound_buffer );

	return ( 0 );
}

static int sound_alsa_flush()
{
	int res;
	unsigned long towr = bufferFrames;
	SNDFRAME *sndb = sound_buffer;

	while ( towr )
	{
		if ( ( res = snd_pcm_writei( sndh, sndb, towr ) ) < 0 )
		{
			snd_pcm_prepare( sndh );
			continue;
		}
		towr -= res;
		sndb += res;
	}

	memset( sound_buffer, 0, bufferFrames * sizeof( SNDFRAME ) );

	return ( 0 );
}

int midi_alsa_init()
{
	int mode = SND_RAWMIDI_SYNC;
	const char* portname = "hw:3,0,0";
	//const char* portname = "hw:2,0";
	int ret;

	ret = snd_rawmidi_open( NULL, &midiout, portname, mode );
	if ( ret < 0 )
	{
		fprintf( stderr, "Problem opening MIDI output: %s\n", snd_strerror(ret) );
		return ( -1 );
	}

	return ( 0 );
}

void midi_alsa_write( char value )
{
	int ret;

	ret = snd_rawmidi_write( midiout, &value, 1 );
	if ( ret < 0 )
		fprintf(stderr,"Problem writing to MIDI output: %s", snd_strerror(ret) );
}

emu_sound_out_t sound_alsa =
{
	sound_alsa_init,
	sound_alsa_uninit,
	sound_alsa_flush,
	NULL,
	NULL
};
