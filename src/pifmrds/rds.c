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
        
        blocks[1] = 0x4400 | (mjd>>15);
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
void get_rds_group(int *buffer, int stereo, int ct_clock_enabled) { //no idea how to do ptyn and decoder id
    static int state = 0;
    static int ps_state = 0;
    static int rt_state = 0;
    static int af_state = 0;
    uint16_t blocks[GROUP_LENGTH] = {rds_params.pi, 0, 0, 0};
    
    // Generate block content
    if(! get_rds_ct_group(blocks, ct_clock_enabled)) { // CT (clock time) has priority on other group types (when its on)
        if(state < 4) {
            blocks[1] = 0x0000 | rds_params.tp << 10 | rds_params.pty << 5 | rds_params.ta << 4 | rds_params.ms << 3 | ps_state;
            if((ps_state == 3) && stereo) blocks[1] |= 0x0004; // DI = 1 - Stereo
            if(rds_params.af[0]) { // AF
                if(af_state == 0) { 
			        blocks[2] = (rds_params.af[0] + 224) << 8 | rds_params.af[1];
		        } else {
                    if(rds_params.af[af_state+1]) {
				        blocks[2] = rds_params.af[af_state] << 8 | rds_params.af[af_state+1];
			        } else {
				        blocks[2] = rds_params.af[af_state] << 8 | 0xCD;
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
        if(state >= 6) state = 0;
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
        // use phase for this
        switch(phase) {
            case 0:
            case 2: sample = 0; break;
            case 1: break;
            case 3: sample = -sample; break;
        }
        phase++;
        if(phase >= 4) phase = 0;
        
        *buffer++ = (sample * sample_volume);
        sample_count++;
    }
}

void set_rds_pi(uint16_t pi_code) {
    rds_params.pi = pi_code;
}

char rds_char_converter(uint16_t src) { // if theres a bug then blame andimik (no offense)
  switch (src)
  { 
    case 0xc2a1: return 0x8E; break; // INVERTED EXCLAMATION MARK
    case 0xc2a3: return 0xAA; break; // POUND SIGN
    case 0xc2a7: return 0xBF; break; // SECTION SIGN
    case 0xc2a9: return 0xA2; break; // COPYRIGHT SIGN
    case 0xc2aa: return 0xA0; break; // FEMININE ORDINAL INDICATOR
    case 0xc2b0: return 0xBB; break; // DEGREE SIGN
    case 0xc2b1: return 0xB4; break; // PLUS-MINUS SIGN
    case 0xc2b2: return 0xB2; break; // SUPERSCRIPT TWO
    case 0xc2b3: return 0xB3; break; // SUPERSCRIPT THREE
    case 0xc2b5: return 0xB8; break; // MICRO SIGN
    case 0xc2b9: return 0xB1; break; // SUPERSCRIPT ONE
    case 0xc2ba: return 0xB0; break; // MASCULINE ORDINAL INDICATOR
    case 0xc2bc: return 0xBC; break; // VULGAR FRACTION ONE QUARTER
    case 0xc2bd: return 0xBD; break; // VULGAR FRACTION ONE HALF
    case 0xc2be: return 0xBE; break; // VULGAR FRACTION THREE QUARTERS
    case 0xc2bf: return 0xB9; break; // INVERTED QUESTION MARK
    case 0xc380: return 0xC1; break; // LATIN CAPITAL LETTER A WITH GRAVE
    case 0xc381: return 0xC0; break; // LATIN CAPITAL LETTER A WITH ACUTE
    case 0xc382: return 0xD0; break; // LATIN CAPITAL LETTER A WITH CIRCUMFLEX
    case 0xc383: return 0xE0; break; // LATIN CAPITAL LETTER A WITH TILDE
    case 0xc384: return 0xD1; break; // LATIN CAPITAL LETTER A WITH DIAERESIS
    case 0xc385: return 0xE1; break; // LATIN CAPITAL LETTER A WITH RING ABOVE
    case 0xc386: return 0xE2; break; // LATIN CAPITAL LETTER AE
    case 0xc387: return 0x8B; break; // LATIN CAPITAL LETTER C WITH CEDILLA
    case 0xc388: return 0xC3; break; // LATIN CAPITAL LETTER E WITH GRAVE
    case 0xc389: return 0xC2; break; // LATIN CAPITAL LETTER E WITH ACUTE
    case 0xc38a: return 0xD2; break; // LATIN CAPITAL LETTER E WITH CIRCUMFLEX
    case 0xc38b: return 0xD3; break; // LATIN CAPITAL LETTER E WITH DIAERESIS
    case 0xc38c: return 0xC5; break; // LATIN CAPITAL LETTER I WITH GRAVE
    case 0xc38d: return 0xC4; break; // LATIN CAPITAL LETTER I WITH ACUTE
    case 0xc38e: return 0xD4; break; // LATIN CAPITAL LETTER I WITH CIRCUMFLEX
    case 0xc38f: return 0xD5; break; // LATIN CAPITAL LETTER I WITH DIAERESIS
    case 0xc390: return 0xCE; break; // LATIN CAPITAL LETTER ETH
    case 0xc391: return 0x8A; break; // LATIN CAPITAL LETTER N WITH TILDE
    case 0xc392: return 0xC7; break; // LATIN CAPITAL LETTER O WITH GRAVE
    case 0xc393: return 0xC6; break; // LATIN CAPITAL LETTER O WITH ACUTE
    case 0xc394: return 0xD6; break; // LATIN CAPITAL LETTER O WITH CIRCUMFLEX
    case 0xc395: return 0xE6; break; // LATIN CAPITAL LETTER O WITH TILDE
    case 0xc396: return 0xD7; break; // LATIN CAPITAL LETTER O WITH DIAERESIS
    case 0xc398: return 0xE7; break; // LATIN CAPITAL LETTER O WITH STROKE
    case 0xc399: return 0xC9; break; // LATIN CAPITAL LETTER U WITH GRAVE
    case 0xc39a: return 0xC8; break; // LATIN CAPITAL LETTER U WITH ACUTE
    case 0xc39b: return 0xD8; break; // LATIN CAPITAL LETTER U WITH CIRCUMFLEX
    case 0xc39c: return 0xD9; break; // LATIN CAPITAL LETTER U WITH DIAERESIS
    case 0xc39d: return 0xE5; break; // LATIN CAPITAL LETTER Y WITH ACUTE
    case 0xc39e: return 0xE8; break; // LATIN CAPITAL LETTER THORN
    case 0xc3a0: return 0x81; break; // LATIN SMALL LETTER A WITH GRAVE
    case 0xc3a1: return 0x80; break; // LATIN SMALL LETTER A WITH ACUTE
    case 0xc3a2: return 0x90; break; // LATIN SMALL LETTER A WITH CIRCUMFLEX
    case 0xc3a3: return 0xF0; break; // LATIN SMALL LETTER A WITH TILDE
    case 0xc3a4: return 0x91; break; // LATIN SMALL LETTER A WITH DIAERESIS
    case 0xc3a5: return 0xF1; break; // LATIN SMALL LETTER A WITH RING ABOVE
    case 0xc3a6: return 0xF2; break; // LATIN SMALL LETTER AE
    case 0xc3a7: return 0x9B; break; // LATIN SMALL LETTER C WITH CEDILLA
    case 0xc3a8: return 0x83; break; // LATIN SMALL LETTER E WITH GRAVE
    case 0xc3a9: return 0x82; break; // LATIN SMALL LETTER E WITH ACUTE
    case 0xc3aa: return 0x92; break; // LATIN SMALL LETTER E WITH CIRCUMFLEX
    case 0xc3ab: return 0x93; break; // LATIN SMALL LETTER E WITH DIAERESIS
    case 0xc3ac: return 0x85; break; // LATIN SMALL LETTER I WITH GRAVE
    case 0xc3ad: return 0x84; break; // LATIN SMALL LETTER I WITH ACUTE
    case 0xc3ae: return 0x94; break; // LATIN SMALL LETTER I WITH CIRCUMFLEX
    case 0xc3af: return 0x95; break; // LATIN SMALL LETTER I WITH DIAERESIS
    case 0xc3b0: return 0xEF; break; // LATIN SMALL LETTER ETH
    case 0xc3b1: return 0x9A; break; // LATIN SMALL LETTER N WITH TILDE
    case 0xc3b2: return 0x87; break; // LATIN SMALL LETTER O WITH GRAVE
    case 0xc3b3: return 0x86; break; // LATIN SMALL LETTER O WITH ACUTE
    case 0xc3b4: return 0x96; break; // LATIN SMALL LETTER O WITH CIRCUMFLEX
    case 0xc3b5: return 0xF6; break; // LATIN SMALL LETTER O WITH TILDE
    case 0xc3b6: return 0x97; break; // LATIN SMALL LETTER O WITH DIAERESIS
    case 0xc3b7: return 0xBA; break; // DIVISION SIGN
    case 0xc3b8: return 0xF7; break; // LATIN SMALL LETTER O WITH STROKE
    case 0xc3b9: return 0x89; break; // LATIN SMALL LETTER U WITH GRAVE
    case 0xc3ba: return 0x88; break; // LATIN SMALL LETTER U WITH ACUTE
    case 0xc3bb: return 0x98; break; // LATIN SMALL LETTER U WITH CIRCUMFLEX
    case 0xc3bc: return 0x99; break; // LATIN SMALL LETTER U WITH DIAERESIS
    case 0xc3bd: return 0xF5; break; // LATIN SMALL LETTER Y WITH ACUTE
    case 0xc3be: return 0xF8; break; // LATIN SMALL LETTER THORN
    case 0xc486: return 0xEB; break; // LATIN CAPITAL LETTER C WITH ACUTE
    case 0xc487: return 0xFB; break; // LATIN SMALL LETTER C WITH ACUTE
    case 0xc48c: return 0xCB; break; // LATIN CAPITAL LETTER C WITH CARON
    case 0xc48d: return 0xDB; break; // LATIN SMALL LETTER C WITH CARON
    case 0xc491: return 0xDE; break; // LATIN SMALL LETTER D WITH STROKE
    case 0xc49b: return 0xA5; break; // LATIN SMALL LETTER E WITH CARON
    case 0xc4b0: return 0xB5; break; // LATIN CAPITAL LETTER I WITH DOT ABOVE
    case 0xc4b1: return 0x9E; break; // LATIN SMALL LETTER DOTLESS I
    case 0xc4b2: return 0x8F; break; // LATIN CAPITAL LIGATURE IJ
    case 0xc4b3: return 0x9F; break; // LATIN SMALL LIGATURE IJ
    case 0xc4bf: return 0xCF; break; // LATIN CAPITAL LETTER L WITH MIDDLE DOT
    case 0xc580: return 0xDF; break; // LATIN SMALL LETTER L WITH MIDDLE DOT
    case 0xc584: return 0xB6; break; // LATIN SMALL LETTER N WITH ACUTE
    case 0xc588: return 0xA6; break; // LATIN SMALL LETTER N WITH CARON
    case 0xc58a: return 0xE9; break; // LATIN CAPITAL LETTER ENG
    case 0xc58b: return 0xF9; break; // LATIN SMALL LETTER ENG
    case 0xc591: return 0xA7; break; // LATIN SMALL LETTER O WITH DOUBLE ACUTE
    case 0xc592: return 0xE3; break; // LATIN CAPITAL LIGATURE OE
    case 0xc593: return 0xF3; break; // LATIN SMALL LIGATURE OE
    case 0xc594: return 0xEA; break; // LATIN CAPITAL LETTER R WITH ACUTE
    case 0xc595: return 0xFA; break; // LATIN SMALL LETTER R WITH ACUTE
    case 0xc598: return 0xCA; break; // LATIN CAPITAL LETTER R WITH CARON
    case 0xc599: return 0xDA; break; // LATIN SMALL LETTER R WITH CARON
    case 0xc59a: return 0xEC; break; // LATIN CAPITAL LETTER S WITH ACUTE
    case 0xc59b: return 0xFC; break; // LATIN SMALL LETTER S WITH ACUTE
    case 0xc59e: return 0x8C; break; // LATIN CAPITAL LETTER S WITH CEDILLA
    case 0xc59f: return 0x9C; break; // LATIN SMALL LETTER S WITH CEDILLA
    case 0xc5a0: return 0xCC; break; // LATIN CAPITAL LETTER S WITH CARON
    case 0xc5a1: return 0xDC; break; // LATIN SMALL LETTER S WITH CARON
    case 0xc5a6: return 0xEE; break; // LATIN CAPITAL LETTER T WITH STROKE
    case 0xc5a7: return 0xFE; break; // LATIN SMALL LETTER T WITH STROKE
    case 0xc5b1: return 0xB7; break; // LATIN SMALL LETTER U WITH DOUBLE ACUTE
    case 0xc5b5: return 0xF4; break; // LATIN SMALL LETTER W WITH CIRCUMFLEX
    case 0xc5b7: return 0xE4; break; // LATIN SMALL LETTER Y WITH CIRCUMFLEX
    case 0xc5b9: return 0xED; break; // LATIN CAPITAL LETTER Z WITH ACUTE
    case 0xc5ba: return 0xFD; break; // LATIN SMALL LETTER Z WITH ACUTE
    case 0xc5bd: return 0xCD; break; // LATIN CAPITAL LETTER Z WITH CARON
    case 0xc5be: return 0xDD; break; // LATIN SMALL LETTER Z WITH CARON
    case 0xc7a6: return 0xA4; break; // LATIN CAPITAL LETTER G WITH CARON
    case 0xc7a7: return 0x9D; break; // LATIN SMALL LETTER G WITH CARON
    case 0xceb1: return 0xA1; break; // GREEK SMALL LETTER ALPHA
    case 0xceb2: return 0x8D; break; // GREEK SMALL LETTER BETA
    case 0xcf80: return 0xA8; break; // GREEK SMALL LETTER PI
    case 0x30 ... 0x7d: return src; break;
    default: return 0x20; break;
  }
}

void set_rds_rt(char *rt) {
    strncpy(rds_params.rt, rt, 64);
    for(int i=0; i<64; i++) {
        if(rds_params.rt[i] == 0) rds_params.rt[i] = 32;
        rds_params.rt[i] = rds_char_converter(rds_params.rt[i]);
    }
}

void set_rds_ps(char *ps) {
    strncpy(rds_params.ps, ps, 8);
    for(int i=0; i<8; i++) {
        rds_params.ps[i] = rds_char_converter(rds_params.ps[i]);
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