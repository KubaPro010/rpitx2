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
#define CONTROL_PIPE_DRDS_SET 10

extern int open_control_pipe(char *filename, volatile uint32_t *padreg, int drds);
extern int close_control_pipe();
extern int poll_control_pipe();
