#include <stdio.h>
#include <sys/asoundlib.h>
#include <errno.h>

#include "sound.h"

static snd_pcm_t *sndh;
#include <gulliver.h>

int sound_qsa_init()
{
	int ret;
	snd_pcm_channel_params_t ch_params;
	snd_pcm_channel_setup_t setup;
	unsigned rate = 48000;

	ret = snd_pcm_open_preferred( &sndh, NULL, NULL, SND_PCM_OPEN_PLAYBACK );
	if ( ret < 0 )
	{
		fprintf (stderr, "cannot open	audio device (%s)\n",
				 snd_strerror (ret));
		return -1;
	}

	/*if ((ret = snd_pcm_plugin_set_disable ( sndh, PLUGIN_DISABLE_BUFFER_PARTIAL_BLOCKS))	< 0)
	{
		fprintf (stderr, "snd_pcm_plugin_set_disable failed: %s\n", snd_strerror (ret));
		return -1;
	}*/

	memset( &ch_params, 0, sizeof(ch_params) );

	ch_params.mode = SND_PCM_MODE_BLOCK;
	ch_params.channel = SND_PCM_CHANNEL_PLAYBACK;
	ch_params.start_mode = SND_PCM_START_FULL;
	ch_params.stop_mode = SND_PCM_STOP_STOP;

    bufferFrames = rate / emu_frame_rate;

	ch_params.buf.block.frag_size = bufferFrames * sizeof(SNDFRAME);
	ch_params.buf.block.frags_max = 8;
	ch_params.buf.block.frags_min = 1;

	ch_params.format.interleave = 1;
	ch_params.format.rate = rate;
	ch_params.format.voices = 2;

	ch_params.format.format = SND_PCM_SFMT_S16;

	ret = snd_pcm_plugin_params( sndh, &ch_params );

	memset( &setup, 0, sizeof(setup) );
	setup.channel = SND_PCM_CHANNEL_PLAYBACK;
	ret = snd_pcm_plugin_setup( sndh, &setup );

	rate = setup.format.rate;
    bufferFrames = rate / emu_frame_rate;
	sound_buffer = calloc( bufferFrames, sizeof(SNDFRAME) );

	ret = snd_pcm_plugin_prepare( sndh, SND_PCM_CHANNEL_PLAYBACK );

	return 0;
}

int sound_qsa_uninit()
{
	snd_pcm_close( sndh );
	free( sound_buffer );

	return 0;
}

int sound_qsa_flush()
{
	int res;
	unsigned long towr = bufferFrames * sizeof(SNDFRAME);
	SNDFRAME *sndb = sound_buffer;

	while ( towr )
	{
		if ( ( res = snd_pcm_plugin_write( sndh, sndb, towr ) )	< towr )
		{
			snd_pcm_plugin_prepare( sndh, SND_PCM_CHANNEL_PLAYBACK );
			continue;
		}
		towr -= res;
		sndb += res;
	}

	memset( sound_buffer, 0, bufferFrames * sizeof( SNDFRAME ) );

	return 0;
}

emu_sound_out_t sound_qsa =
{
	sound_qsa_init,
	sound_qsa_uninit,
	sound_qsa_flush,
	NULL,
	NULL
};
