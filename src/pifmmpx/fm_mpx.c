/*
    PiFmRds - FM/RDS transmitter for the Raspberry Pi
    Copyright (C) 2014 Christophe Jacquet, F8FTK
    
    See https://github.com/ChristopheJacquet/PiFmRds
    
    rds_wav.c is a test program that writes a RDS baseband signal to a WAV
    file. It requires libsndfile.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    fm_mpx.c: generates an FM multiplex signal containing RDS plus possibly
    monaural or stereo audio.
*/

#include "fm_mpx.h"
#include <sndfile.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>

#define PI 3.14159265359

#define FIR_PHASES    (32)
#define FIR_TAPS      (32) // MUST be a power of 2 for the circular buffer

size_t length;

// coefficients of the low-pass FIR filter
float low_pass_fir[FIR_PHASES][FIR_TAPS];

float carrier_38[] = {0.0, 0.8660254037844386, 0.8660254037844388, 1.2246467991473532e-16, -0.8660254037844384, -0.8660254037844386};

float carrier_19[] = {0.0, 0.5, 0.8660254037844386, 1.0, 0.8660254037844388, 0.5, 1.2246467991473532e-16, -0.5, -0.8660254037844384, -1.0, -0.8660254037844386, -0.5};

float carrier_3125[] = {0.0, 0.7586133425663026, 0.9885355334735083, 0.5295297022607088, -0.29851481100169425, -0.918519035014914, -0.898390981891979, -0.2521582503964708}; // sine wave

int phase_38 = 0;
int phase_3125 = 0;
int phase_19 = 0;

float downsample_factor;

float *audio_buffer;
int audio_index = 0;
int audio_len = 0;
float audio_pos;

SNDFILE *inf;

float clip(float input, float threshold) {
    if (input > threshold) {
        return threshold;
    } else if (input < -threshold) {
        return -threshold;
    }
    return input;
}

float limiter(float input, float threshold, int enable) {
    if(enable == 1) {
        if (fabsf(input) > threshold) {
            return (input > 0) ? threshold : -threshold;
        }
        return input;
    } else {
        return input;
    }
}

float *alloc_empty_buffer(size_t length) {
    float *p =(float *) malloc(length * sizeof(float));
    if(p == NULL) return NULL;
    
    bzero(p, length * sizeof(float));
    
    return p;
}


int fm_mpx_open(char *filename, size_t len) {
    length = len;

    if(filename != NULL) {
        // Open the input file
        SF_INFO sfinfo;

        sfinfo.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16;
        sfinfo.samplerate = 192000;
        sfinfo.channels = 1;
 
        // stdin or file on the filesystem?
        if(filename[0] == '-') {
            if(! (inf = sf_open_fd(fileno(stdin), SFM_READ, &sfinfo, 0))) {
                fprintf(stderr, "Error: could not open stdin for audio input.\n") ;
                return -1;
            } else {
                printf("Using stdin for audio input.\n");
            }
        } else {
            if(! (inf = sf_open(filename, SFM_READ, &sfinfo))) {
                fprintf(stderr, "Error: could not open input file %s.\n", filename) ;
                return -1;
            } else {
                printf("Using audio file: %s\n", filename);
            }
        }
            
        int in_samplerate = sfinfo.samplerate;
        downsample_factor = 228000. / in_samplerate;
    
        printf("Input: %d Hz, upsampling factor: %.2f\n", in_samplerate, downsample_factor);
        
        audio_pos = downsample_factor;
        audio_buffer = alloc_empty_buffer(length);
        if(audio_buffer == NULL) return -1;

    } // end if(filename != NULL)
    else {
        inf = NULL;
        // inf == NULL indicates that there is no audio
    }
    
    return 0;
}

// samples provided by this function are in 0..10: they need to be divided by
// 10 after.
int fm_mpx_get_samples(float *mpx_buffer, fm_mpx_data *data) {
    *audio_buffer = 0.0;
    if(inf == NULL) return 0; // if there is no audio, stop here
    
    for(int i=0; i<length; i++) {
        if(audio_pos >= downsample_factor) {
            audio_pos -= downsample_factor;
            
            if(audio_len <=1 ) {
                for(int j=0; j<2; j++) { // one retry
                    audio_len = sf_read_float(inf, audio_buffer, length);
                    if (audio_len < 0) {
                        fprintf(stderr, "Error reading audio\n");
                        return -1;
                    }
                    if(audio_len == 0) {
                        if( sf_seek(inf, 0, SEEK_SET) < 0 ) {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                audio_index = 0;
            } else {
                audio_index++;
                audio_len -= 1;
            }
        } // if need new sample

        float out = audio_buffer[audio_index];

        // Multiply by the gain
        out *= data->gain;

        if(data->paused) {
            out = 0;
        }

        if(!data->generate_multiplex) {
            mpx_buffer[i] = 10*out; // Accomodate for the 7 khz deviation, 10*7500 = 75000
        }
            
        audio_pos++;   
        
    }
    return 0;
}


int fm_mpx_close() {
    if(sf_close(inf) ) {
        fprintf(stderr, "Error closing audio file");
    }
    
    if(audio_buffer != NULL) free(audio_buffer);
    
    return 0;
}
