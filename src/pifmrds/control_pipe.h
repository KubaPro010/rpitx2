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

typedef struct {
    int res;
    char arg[70];
} ResultAndArg;

extern int open_control_pipe(char *filename, volatile uint32_t *padreg);
extern int close_control_pipe();
extern ResultAndArg poll_control_pipe();
