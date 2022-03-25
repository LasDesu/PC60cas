#include <stdio.h>
#include <mach/task.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <errno.h>
#include <AudioToolbox/AudioToolbox.h>

#include "sound.h"

static AudioQueueRef queue;
static AudioQueueBufferRef *aq_buffers;
static AudioQueueBufferRef cur_buffer;
static int buffers = 4;
static semaphore_t sbuf_free;

static void HandleOutputBuffer( void *aqData, AudioQueueRef	queue, AudioQueueBufferRef buffer )
{
	buffer->mAudioDataByteSize = 0;
	memset( buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity );
	semaphore_signal( sbuf_free );
}

static AudioQueueBufferRef find_free_buffer( void )
{
	int i;

	semaphore_wait( sbuf_free );
	for ( i = 0; i < buffers; i ++ )
	{
		if ( aq_buffers[i]->mAudioDataByteSize == 0 )
		{
			aq_buffers[i]->mAudioDataByteSize = aq_buffers[i]->mAudioDataBytesCapacity;
			return ( aq_buffers[i] );
		}
	}
	
	printf( "cannot find free audio buffer, prepare for crash!\n" );
	return NULL; /* nonsense */
}

static int sound_coreaudio_init()
{
	int rate = 48000;
	int i;
	OSStatus stat;
	mach_port_t self = mach_task_self();
	
	bufferFrames = rate / emu_frame_rate;
	semaphore_create( self, &sbuf_free, SYNC_POLICY_FIFO, buffers );
	
	AudioStreamBasicDescription	dataFormat = { 0 };
	dataFormat.mSampleRate = rate;
	dataFormat.mFormatID = kAudioFormatLinearPCM;
	dataFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger;
	dataFormat.mFramesPerPacket = 1;
	dataFormat.mBytesPerFrame = 4;
	dataFormat.mChannelsPerFrame = 2;
	dataFormat.mBitsPerChannel = 16;
	dataFormat.mBytesPerPacket = dataFormat.mFramesPerPacket * dataFormat.mBytesPerFrame;
	
	stat = AudioQueueNewOutput( &dataFormat, HandleOutputBuffer, NULL,
		CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &queue );
	
	aq_buffers = mem_calloc( buffers, sizeof(*aq_buffers) );
	bufferFrames = dataFormat.mSampleRate /	emu_frame_rate;
	for ( i = 0; i < buffers; i ++ )
	{
		AudioQueueAllocateBuffer( queue, bufferFrames * sizeof(SNDFRAME), &aq_buffers[i] );
		memset( aq_buffers[i]->mAudioData, 0, aq_buffers[i]->mAudioDataBytesCapacity );
	}
	
	cur_buffer = find_free_buffer();
	sound_buffer = cur_buffer->mAudioData;
	
	stat = AudioQueueStart( queue, NULL );
	
	return 0;
}

static int sound_coreaudio_pause()
{
	AudioQueuePause( queue );
	
	return 0;
}

static int sound_coreaudio_resume()
{
	AudioQueueStart( queue, NULL );
	
	return 0;
}

static int sound_coreaudio_uninit()
{
	AudioQueueStop( queue, 1 );
	
	return 0;
}
static int sound_coreaudio_flush()
{
	AudioQueueEnqueueBuffer( queue, cur_buffer, 0, NULL );
	cur_buffer = find_free_buffer();
	sound_buffer = cur_buffer->mAudioData;
	
	return 0;
}

emu_sound_out_t	sound_coreaudio =
{
	sound_coreaudio_init,
	sound_coreaudio_uninit,
	sound_coreaudio_flush,
	sound_coreaudio_pause,
	sound_coreaudio_resume
};
