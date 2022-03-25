#ifndef FORMATS_TAPE_H
#define FORMATS_TAPE_H

typedef struct
{
	void *file;
	void (*close)();
	int (*process)();
	int (*signal)();
} emu_format_tape_t;

struct emu_tape_block;
struct emu_tape_block
{
	char *name;
	struct emu_tape_block *next;
	void (*prepare)( struct emu_tape_block *blk );
	void (*fini)( struct emu_tape_block *blk );
	int (*step)( struct emu_tape_block *blk );
	int (*signal)( struct emu_tape_block *blk );
};

extern int (*tape_process)( unsigned long cycles );
extern int (*tape_signal)( void );

void emu_tape_clear();
void emu_tape_add( struct emu_tape_block *blk );

int load_tap( const char *flname );
int load_tzx( const char *flname );
int load_wav( const char *flname );
int load_pc60cas( const char *flname );

#endif /* FORMATS_TAPE_H */
