#include <stdio.h>
#include <windows.h>
#include <mmsystem.h>

#include "os_support\os.h"
#include "sound.h"

static HWAVEOUT hWaveOut;
static WAVEHDR *WaveHdr;
static PWAVEHDR	curBuffer;
static int buffers = 4;
static sys_semaphore_t sbuf_free;

static void CALLBACK waveOutProc( HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2 )
{
	PWAVEHDR pBuffer;
	switch ( uMsg )
	{
		case WOM_DONE:
			sys_semaphore_post( &sbuf_free );
			break;
	};
}

static WAVEHDR *find_free_buffer( void )
{
	int i;
	sys_semaphore_wait( &sbuf_free );
	for ( i = 0; i < buffers; i ++ )
	{
		if ( WaveHdr[i].dwFlags & WHDR_DONE )
		{
			return ( &WaveHdr[i] );
		}
	}
	printf( "cannot	find free audio	buffer, prepare	for	crash!\n" );
	return ( NULL );
}

static int sound_waveout_init()
{
	UINT wResult;
	WAVEFORMATEX Format;
	int rate = 48000;
	int i;

	bufferFrames = rate / zx_frame_rate;
	sys_semaphore_init( &sbuf_free, 0 );

	Format.wFormatTag = WAVE_FORMAT_PCM;
	Format.nChannels = 2;
	Format.wBitsPerSample = 16;
	Format.nBlockAlign = (WORD)(2 * 2);
	Format.nSamplesPerSec = rate;
	Format.nAvgBytesPerSec = Format.nSamplesPerSec	* Format.nBlockAlign;
	Format.cbSize = 0;

	if ( waveOutOpen( (LPHWAVEOUT)&hWaveOut, WAVE_MAPPER, &Format, waveOutProc, 0, CALLBACK_FUNCTION) )
		return -1;

	WaveHdr = mem_calloc( buffers, sizeof(*WaveHdr) );
	for ( i = 0; i < buffers; i ++ )
	{
		WaveHdr[i].lpData = mem_calloc( bufferFrames, sizeof(SNDFRAME) );
		WaveHdr[i].dwBufferLength = bufferFrames * sizeof(SNDFRAME);
		WaveHdr[i].dwFlags = 0L;
		WaveHdr[i].dwLoops = 0L;
		waveOutPrepareHeader( hWaveOut, &WaveHdr[i], sizeof(WAVEHDR) );
		if ( i < buffers - 1 )
			waveOutWrite( hWaveOut, &WaveHdr[i], sizeof(WAVEHDR) );
	}

	curBuffer = &WaveHdr[buffers - 1];
	sound_buffer = curBuffer->lpData;

	return 0;
}

static int sound_waveout_uninit()
{
	//mmioClose(hmmio, 0);

	return 0;
}

static int sound_waveout_flush()
{
	PWAVEHDR pWaveHdr;
	waveOutWrite( hWaveOut, curBuffer, sizeof(WAVEHDR) );

	pWaveHdr = find_free_buffer();
	curBuffer = pWaveHdr;
	sound_buffer = curBuffer->lpData;
	memset( sound_buffer, 0, bufferFrames * sizeof(SNDFRAME) );

	return 0;
}

pzx_sound_out_t	sound_waveout =
	{
		sound_waveout_init,
		sound_waveout_uninit,
		sound_waveout_flush,
		sound_waveout_pause,
		sound_waveout_resume
	};
