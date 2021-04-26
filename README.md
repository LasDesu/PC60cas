# PC60cas
PC-6001 tape image (cas/p6) to WAV converter

## Usage
`pc60cas [options] <input file> [output file]`

### Options:

* **-t** *type*

Force file type. Possible values are:

    0 - raw data file    
    1 - BASIC file    
    2 - P6T file (not supported yet)

* **-b** *baud*

Set baud rate. Default - 1200.

* **-i**

Invert phase.

* **-s**

Square wave.

* **-p**

Play tape, do not convert to wav (not supperted yet)
