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
*/

#include <stddef.h>

typedef struct {
    int drds;
    float compressor_decay;
    float compressor_attack;
    float compressor_max_gain_recip;
    int dstereo;
    float audio_gain;
    int enablecompressor;
    int rds_ct_enabled;
    float rds_volume;
    int paused;
    int generate_multiplex;
    float limiter_threshold;
} fm_mpx_data;

int fm_mpx_open(char *filename, size_t len, int raw, double preemphasis, int rawSampleRate, int rawChannels, float cutoff_freq);
int fm_mpx_get_samples(float *mpx_buffer, fm_mpx_data *data);
int fm_mpx_close();
