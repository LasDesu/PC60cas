#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>

#include "sound.h"

static pa_simple *s;

static int sound_pulse_init()
{
	pa_sample_spec ss;
	pa_buffer_attr ba;

	ss.format = PA_SAMPLE_S16NE;
	ss.channels = 2;
	ss.rate = 48000;

	setenv("PULSE_PROP_media.role", "game", 1);

	ba.minreq = -1;
	ba.maxlength = -1;
	ba.prebuf = -1;
	ba.tlength = ss.rate / emu_frame_rate;

	s = pa_simple_new(NULL,				// Use the default server.
				   "PerfectZX", 		// Our application's name.
				   PA_STREAM_PLAYBACK,
				   NULL,			 	// Use the default device.
				   "Sound",				// Description of our stream.
				   &ss,			 		// Our sample format.
				   NULL,			 	// Use default channel map
				   &ba,					// Use custom buffering attributes.
				   NULL					// Ignore error code.
				   );
	if ( !s )
		return -1;

	bufferFrames = ss.rate / emu_frame_rate;
	sound_buffer = calloc( bufferFrames, sizeof(SNDFRAME) );

	return 0;
}

static int sound_pulse_uninit()
{
	pa_simple_free( s );
	free( sound_buffer );

	return 0;
}

static int sound_pulse_flush()
{
	int res;

	res = pa_simple_write(s, sound_buffer, bufferFrames * sizeof( SNDFRAME ), NULL );

	memset( sound_buffer, 0, bufferFrames * sizeof( SNDFRAME ) );

	return 0;
}

emu_sound_out_t sound_pulse =
{
	sound_pulse_init,
	sound_pulse_uninit,
	sound_pulse_flush,
	NULL,
	NULL
};
