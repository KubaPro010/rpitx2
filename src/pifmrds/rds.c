/*
    PiFmRds - FM/RDS transmitter for the Raspberry Pi
    Copyright (C) 2014 Christophe Jacquet, F8FTK
    
    See https://github.com/ChristopheJacquet/PiFmRds

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
*/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "waveforms.h"

float carrier_57[] = {0.0, 1.0, 1.2246467991473532e-16, -1.0}; // sine wave at 57 kHz, 228 kHz sample rate, 4 samples because 57 kHz is 4 times the sample rate

#define RT_LENGTH 64
#define PS_LENGTH 8
#define GROUP_LENGTH 4

struct {
    uint16_t pi;
    int ta;
    int pty;
    int tp;
    int ms;
    int ab;
    char ps[PS_LENGTH];
    char rt[RT_LENGTH];
    int af[100];
    int di;
} rds_params = { 0 };
/* Here, the first member of the struct must be a scalar to avoid a
   warning on -Wmissing-braces with GCC < 4.8.3 
   (bug: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53119)
*/

/* The RDS error-detection code generator polynomial is
   x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + x^0
*/
#define POLY 0x1B9
#define POLY_DEG 10
#define MSB_BIT 0x8000
#define BLOCK_SIZE 16

#define BITS_PER_GROUP (GROUP_LENGTH * (BLOCK_SIZE+POLY_DEG))
#define SAMPLES_PER_BIT 192
#define FILTER_SIZE (sizeof(waveform_biphase)/sizeof(float))
#define SAMPLE_BUFFER_SIZE (SAMPLES_PER_BIT + FILTER_SIZE)


uint16_t offset_words[] = {0x0FC, 0x198, 0x168, 0x1B4};
// We don't handle offset word C' here for the sake of simplicity

/* Classical CRC computation */
uint16_t crc(uint16_t block) {
    uint16_t crc = 0;

    for(int j=0; j<BLOCK_SIZE; j++) {
        int bit = (block & MSB_BIT) != 0;
        block <<= 1;

        int msb = (crc >> (POLY_DEG-1)) & 1;
        crc <<= 1;
        if((msb ^ bit) != 0) {
            crc = crc ^ POLY;
        }
    }
    return crc;
}

/* Possibly generates a CT (clock time) group if the minute has just changed
   Returns 1 if the CT group was generated, 0 otherwise
*/
int get_rds_ct_group(uint16_t *blocks, int enabled) {
    static int latest_minutes = -1;

    // Check time
    time_t now;
    struct tm *utc;
    
    now = time (NULL);
    utc = gmtime (&now);

    if(!enabled) {
        latest_minutes = utc->tm_min; //oh wait it won't anyway
        return 0;
    }
    if(utc->tm_min != latest_minutes) {
        // Generate CT group
        latest_minutes = utc->tm_min;
        
        int l = utc->tm_mon <= 1 ? 1 : 0;
        int mjd = 14956 + utc->tm_mday + 
                        (int)((utc->tm_year - l) * 365.25) +
                        (int)((utc->tm_mon + 2 + l*12) * 30.6001);
        
        blocks[1] = 0x4400 | rds_params.tp << 10 | rds_params.pty << 5 | (mjd>>15);
        blocks[2] = (mjd<<1) | (utc->tm_hour>>4);
        blocks[3] = (utc->tm_hour & 0xF)<<12 | utc->tm_min<<6;
        
        utc = localtime(&now);
        
        int offset = utc->tm_gmtoff / (30 * 60);
        blocks[3] |= abs(offset);
        if(offset < 0) blocks[3] |= 0x20;
        
        //printf("Generated CT: %04X %04X %04X\n", blocks[1], blocks[2], blocks[3]);
        return 1;
    } else return 0;
}

/* Creates an RDS group. This generates sequences of the form 0A, 0A, 0A, 0A, 2A, etc.
   The pattern is of length 8, the variable 'state' keeps track of where we are in the
   pattern. 'ps_state' and 'rt_state' keep track of where we are in the PS (0A) sequence
   or RT (2A) sequence, respectively.
*/
void get_rds_group(int *buffer, int stereo, int ct_clock_enabled) { //ptyn?
    static int state = 0;
    static int ps_state = 0;
    static int rt_state = 0;
    static int af_state = 0;
    uint16_t blocks[GROUP_LENGTH] = {rds_params.pi, 0, 0, 0};
    
    // Generate block content
    if(!get_rds_ct_group(blocks, ct_clock_enabled)) { // CT (clock time) has priority on other group types (when its on)
        if(state < 4) {
            blocks[1] = 0x0000 | rds_params.tp << 10 | rds_params.pty << 5 | rds_params.ta << 4 | rds_params.ms << 3 | ps_state;
            blocks[1] |= ((rds_params.di >> (3 - ps_state)) & 0x01) << 2;
            if(rds_params.af[0]) { // AF
                if(af_state == 0) { 
			        blocks[2] = (rds_params.af[0] + 224) << 8 | rds_params.af[1]; // Send number of AFs and the first AF
		        } else {
                    if(rds_params.af[af_state+1]) { // If we have something next
				        blocks[2] = rds_params.af[af_state] << 8 | rds_params.af[af_state+1]; // We send this and the 2nd one
			        } else {
				        blocks[2] = rds_params.af[af_state] << 8 | 0xCD; // No? then we just send this one
			        }
		        }
                af_state = af_state + 2;
		        if(af_state > rds_params.af[0]) af_state = 0;
            }
            blocks[3] = rds_params.ps[ps_state*2] << 8 | rds_params.ps[ps_state*2+1];
            ps_state++;
            if(ps_state >= 4) ps_state = 0;
        } else { // Type 2A groups
            blocks[1] = 0x2000 | rds_params.tp << 10 | rds_params.pty << 5 | rds_params.ab << 4 | rt_state;
            blocks[2] = rds_params.rt[rt_state*4+0] << 8 | rds_params.rt[rt_state*4+1];
            blocks[3] = rds_params.rt[rt_state*4+2] << 8 | rds_params.rt[rt_state*4+3];
            rt_state++;
            if(rt_state >= 16) rt_state = 0;
        }
    
        state++;
        if(state >= 8) state = 0;
    }
    
    // Calculate the checkword for each block and emit the bits
    for(int i=0; i<GROUP_LENGTH; i++) {
        uint16_t block = blocks[i];
        uint16_t check = crc(block) ^ offset_words[i];
        for(int j=0; j<BLOCK_SIZE; j++) {
            *buffer++ = ((block & (1<<(BLOCK_SIZE-1))) != 0);
            block <<= 1;
        }
        for(int j=0; j<POLY_DEG; j++) {
            *buffer++= ((check & (1<<(POLY_DEG-1))) != 0);
            check <<= 1;
        }
    }
}

/* Get a number of RDS samples. This generates the envelope of the waveform using
   pre-generated elementary waveform samples, and then it amplitude-modulates the 
   envelope with a 57 kHz carrier, which is very efficient as 57 kHz is 4 times the
   sample frequency we are working at (228 kHz).
 */
void get_rds_samples(float *buffer, int count, int stereo, int ct_clock_enabled, float sample_volume) {
    static int bit_buffer[BITS_PER_GROUP];
    static int bit_pos = BITS_PER_GROUP;
    static float sample_buffer[SAMPLE_BUFFER_SIZE] = {0};
    
    static int prev_output = 0;
    static int cur_output = 0;
    static int cur_bit = 0;
    static int sample_count = SAMPLES_PER_BIT;
    static int inverting = 0;
    static int phase = 0;

    static int in_sample_index = 0;
    static int out_sample_index = SAMPLE_BUFFER_SIZE-1;
        
    for(int i=0; i<count; i++) {
        if(sample_count >= SAMPLES_PER_BIT) {
            if(bit_pos >= BITS_PER_GROUP) {
                get_rds_group(bit_buffer, stereo, ct_clock_enabled);
                bit_pos = 0;
            }
            
            // do differential encoding
            cur_bit = bit_buffer[bit_pos];
            prev_output = cur_output;
            cur_output = prev_output ^ cur_bit;
            
            inverting = (cur_output == 1);

            float *src = waveform_biphase;
            int idx = in_sample_index;

            for(int j=0; j<FILTER_SIZE; j++) {
                double val = (*src++);
                if(inverting) val = -val;
                sample_buffer[idx++] += val;
                if(idx >= SAMPLE_BUFFER_SIZE) idx = 0;
            }

            in_sample_index += SAMPLES_PER_BIT;
            if(in_sample_index >= SAMPLE_BUFFER_SIZE) in_sample_index -= SAMPLE_BUFFER_SIZE;
            
            bit_pos++;
            sample_count = 0;
        }
        
        double sample = sample_buffer[out_sample_index];
        sample_buffer[out_sample_index] = 0;
        out_sample_index++;
        if(out_sample_index >= SAMPLE_BUFFER_SIZE) out_sample_index = 0;
        
        // modulate at 57 kHz
        sample = sample * carrier_57[phase];
        phase++;
        if(phase >= 4) phase = 0;
        
        *buffer++ = (sample * sample_volume);
        sample_count++;
    }
}

void set_rds_pi(uint16_t pi_code) {
    rds_params.pi = pi_code;
}

void set_rds_rt(char *rt) {
    strncpy(rds_params.rt, rt, 64);
    for(int i=0; i<64; i++) {
        if(rds_params.rt[i] == 0) rds_params.rt[i] = 32;
    }
}

void set_rds_ps(char *ps) {
    strncpy(rds_params.ps, ps, 8);
    for(int i=0; i<8; i++) {
        if(rds_params.ps[i] == 0) rds_params.ps[i] = 32;
    }
}

void set_rds_af(int *af_array) {
	rds_params.af[0] = af_array[0];
	int f;
	for(f = 1; f < af_array[0]+1; f++) {
		rds_params.af[f] = af_array[f];
	}
}

void set_rds_pty(int pty) {
	rds_params.pty = pty;
}

void set_rds_di(int di) {
    rds_params.di = di;
}

void set_rds_ta(int ta) {
	rds_params.ta = ta;
}

void set_rds_tp(int tp) {
	rds_params.tp = tp;
}

void set_rds_ms(int ms) {
	rds_params.ms = ms;
}

void set_rds_ab(int ab) {
	rds_params.ab = ab;
}
