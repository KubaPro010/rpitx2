/*
    PiFmAdv - Advanced FM transmitter for the Raspberry Pi
    Copyright (C) 2017 Miegl

    See https://github.com/Miegl/PiFmAdv
*/
#include <fcntl.h>
#define CONTROL_PIPE_PS_SET 1
#define CONTROL_PIPE_RT_SET 2
#define CONTROL_PIPE_TA_SET 3
#define CONTROL_PIPE_PTY_SET 4
#define CONTROL_PIPE_TP_SET 5
#define CONTROL_PIPE_MS_SET 6
#define CONTROL_PIPE_AB_SET 7
#define CONTROL_PIPE_PI_SET 8
#define CONTROL_PIPE_PWR_SET 9
#define CONTROL_PIPE_RDS_SET 10
#define CONTROL_PIPE_DEVIATION_SET 11
#define CONTROL_PIPE_STEREO_SET 12
#define CONTROL_PIPE_GAIN_SET 13
#define CONTROL_PIPE_COMPRESSORDECAY_SET 14
#define CONTROL_PIPE_COMPRESSORATTACK_SET 15
#define CONTROL_PIPE_CT_SET 16

typedef struct {
    int res;
    char *arg;
} ResultAndArg;

extern int open_control_pipe(char *filename);
extern int close_control_pipe();
extern ResultAndArg poll_control_pipe();
