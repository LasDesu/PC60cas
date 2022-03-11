#include <SDL.h>

#include "sound.h"

static int pos = 0;
static SDL_sem *frame_ready;
static SDL_sem *frame_free;

static void sdl_audiocallback( void *userdata, Uint8 *stream, int len )
{
	const unsigned long	bufsize = bufferFrames * sizeof( SNDFRAME );
	void *sndb = sound_buffer;

	while ( len > 0 )
	{
		int towr = len;

		if ( pos < 1 )
			SDL_SemWait( frame_ready );

		if ( towr > bufsize - pos )
			towr = bufsize - pos;

		memcpy( stream, sndb + pos, towr );

		len -= towr;
		pos += towr;
		stream += towr;
		if ( pos >= bufsize )
		{
			pos = 0;
			SDL_SemPost( frame_free );
		}
	}
}

static int sound_sdl_init()
{
	SDL_AudioSpec spec, obtained;

	SDL_Init( SDL_INIT_AUDIO );

	SDL_zero( spec );
	spec.format = AUDIO_S16;
	spec.freq = 48000;
	spec.channels = 2;
	spec.samples = 1024;
	spec.callback = sdl_audiocallback;

	SDL_OpenAudio( &spec, &obtained );

	bufferFrames = obtained.freq / emu_frame_rate;
	sound_buffer = calloc( bufferFrames, sizeof(SNDFRAME) );

	frame_ready = SDL_CreateSemaphore( 0 );
	frame_free = SDL_CreateSemaphore( 0 );

	SDL_PauseAudio( 0 );

	return ( 0 );
}

static int sound_sdl_uninit()
{
	SDL_CloseAudio();
	SDL_DestroySemaphore( frame_ready );
	SDL_DestroySemaphore( frame_free );

	free( sound_buffer );

	return ( 0 );
}

static int sound_sdl_pause()
{
	SDL_PauseAudio( 1 );

	return ( 0 );
}

static int sound_sdl_resume()
{
	SDL_PauseAudio( 0 );

	return ( 0 );
}

static int sound_sdl_flush()
{
	SDL_SemPost( frame_ready );
	SDL_SemWait( frame_free );

	memset( sound_buffer, 0, bufferFrames * sizeof( SNDFRAME ) );

	return ( 0 );
}

emu_sound_out_t sound_sdl =
{
	sound_sdl_init,
	sound_sdl_uninit,
	sound_sdl_flush,
	sound_sdl_pause,
	sound_sdl_resume
};
