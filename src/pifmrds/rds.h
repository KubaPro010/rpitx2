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

#ifndef RDS_H
#define RDS_H

//taken from bartek's MicroRDS https://github.com/barteqcz/MicroRDS/blob/main/src/rds.h
#define DI_STEREO	(1 << 0) /* 1 - Stereo */
#define DI_AH		(1 << 1) /* 2 - Artificial Head */
#define DI_COMPRESSED	(1 << 2) /* 4 - Compressed */
#define DI_DPTY		(1 << 3) /* 8 - Dynamic PTY */

#define AF_CODE_FILLER		205
#define AF_CODE_NO_AF		224
#define AF_CODE_NUM_AFS_BASE	AF_CODE_NO_AF
#define AF_CODE_LFMF_FOLLOWS	250

#include <stdint.h>

extern void get_rds_samples(float *buffer, int count, int stereo, int ct_clock_enabled, float sample_volume);
extern void set_rds_pi(uint16_t pi_code);
extern void set_rds_rt(char *rt);
extern void set_rds_ps(char *ps);
extern void set_rds_ta(int ta);
extern void set_rds_pty(int pty);
extern void set_rds_af(int *af_array);
extern void set_rds_tp(int tp);
extern void set_rds_ms(int ms);
extern void set_rds_ab(int ab);
extern void set_rds_di(int di);


#endif /* RDS_H */
