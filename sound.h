#ifndef EMU_SOUND
#define EMU_SOUND

#include "emul.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	int (*init)();
	int (*uninit)();
	int (*flush)();
	int (*pause)();
	int (*resume)();
} emu_sound_out_t;

typedef signed short SNDSAMPLE;

typedef struct
{
	SNDSAMPLE l, r;
} SNDFRAME;

//#define SNDFRAME_LEN		 20
//#define SNDFRAME_LEN		 (1000.0 / zx_frame_rate)

#define MONO(v) (v), (v)

typedef struct
{
	void *filter;
	unsigned measures, measure;
	unsigned frame, next_frame;
} sound_state_t;

extern unsigned sample_rate;
extern SNDFRAME *sound_buffer;
extern unsigned long bufferFrames;

extern void (*init_sound)( sound_state_t *state, unsigned measures );
extern void (*add_sound)( unsigned begin, unsigned end, unsigned measures, int l, int r );
extern void (*add_sound_new)( sound_state_t *state, unsigned end, int l, int r );
extern void (*add_sound_hp)( unsigned begin, unsigned end, unsigned measures, int l, int r, sound_state_t *state );
extern void (*add_sound_hp_new)( sound_state_t *state, unsigned end, int l, int r );
extern void flush_sound( sound_state_t *state );

void reinit_sound( sound_state_t *state, unsigned measures );
void disable_sound();
void enable_sound();

extern emu_sound_out_t sound_oss;
extern emu_sound_out_t sound_alsa;
extern emu_sound_out_t sound_pulse;
extern emu_sound_out_t sound_qsa;
extern emu_sound_out_t sound_coreaudio;
extern emu_sound_out_t sound_waveout;
extern emu_sound_out_t sound_sdl;

#ifdef __cplusplus
}
#endif

#endif /* EMU_SOUND */
