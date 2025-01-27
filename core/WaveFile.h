#ifndef _WAV_FILE_H_
#define _WAV_FILE_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "File.h"

typedef struct wavefile_header_s{
	char	riff_tag[4];
	int		riff_length;
	char	wave_tag[4];
	char	fmt_tag[4];
	int		fmt_length;
	int16_t	audio_format;
	int16_t	num_channels;
	int		sample_rate;
	int		byte_rate;
	int16_t	block_align;
	int16_t	bits_per_sample;
	int8_t	data_tag[4];
	int32_t	data_length;
}wavefile_header_t;

// Loads and parses a wave file through the File operation.
class WaveFile{
public:
    WaveFile();
    ~WaveFile();

    uint8_t* file_data = NULL;
    uint8_t* wav_data = NULL;
    size_t file_size = 0;
    bool LoadWaveFile(const char* filename);    //Load file into memory.

    long GetDataLength();

};

#endif