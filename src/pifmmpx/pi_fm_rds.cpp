#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sndfile.h>
extern "C"
{
#include "fm_mpx.h"
#include "control_pipe.h"
}
#include <librpitx/librpitx.h>
ngfmdmasync *fmmod;
#define DATA_SIZE 5000
static void terminate(int num)
{
    delete fmmod;
    fm_mpx_close();
    close_control_pipe();
    exit(num);
}

static void fatal(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    terminate(0);
}

typedef struct tx_data {
    uint32_t carrier_freq;
    char *audio_file;
    char *control_pipe;
    int power;
    int deviation;
    float audio_gain;
    int log;
} tx_data;

int tx(tx_data *data) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = terminate;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL); //https://www.gnu.org/software/libc/manual/html_node/Termination-Signals.html
    sigaction(SIGPWR, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL); //seg fault
    
    // Data structures for baseband data
    float data[DATA_SIZE];
	float devfreq[DATA_SIZE];
    int data_len = 0;
    int data_index = 0;

    int generate_multiplex = 1;
    float audio_gain = data->audio_gain;

    //set the power
    padgpio gpiopad;
    gpiopad.setlevel(data->power);

    // Initialize the baseband generator
    if(fm_mpx_open(data->audio_file, DATA_SIZE) < 0) return 1;
    uint16_t count = 0;
    uint16_t count2 = 0;

    // Initialize the control pipe reader
    if(data->control_pipe) {
        if(open_control_pipe(data->control_pipe) == 0) {
            if(data->log) printf("Reading control commands on %s.\n", data->control_pipe);
        } else {
            if(data->log) printf("Failed to open control pipe: %s.\n", data->control_pipe);
            data->control_pipe = NULL;
        }
    }
    if(data->log) printf("Starting to transmit on %3.1f MHz.\n", carrier_freq/1e6);
    float deviation_scale_factor;
    // The deviation specifies how wide the signal is (from its lowest bandwidht to its highest, but not including sub-carriers). 
    // Use 75kHz for WFM (broadcast radio, or 50khz can be used)
    // and about 2.5kHz for NFM (walkie-talkie style radio)
    deviation_scale_factor=  0.1 * (data->deviation);
    int paused = 0;
    for (;;)
	{
        if(data->control_pipe) {
            ResultAndArg pollResult = poll_control_pipe(log);
            if(pollResult.res == CONTROL_PIPE_PWR_SET) {
                padgpio gpiopad;
                gpiopad.setlevel(pollResult.arg_int);
            } else if(pollResult.res == CONTROL_PIPE_DEVIATION_SET) {
                data->deviation = std::stoi(pollResult.arg);
                deviation_scale_factor=  0.1 * (data->deviation);
            } else if(pollResult.res == CONTROL_PIPE_GAIN_SET) {
                audio_gain = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_PAUSE_SET) {
                paused = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_MPXGEN_SET) {
                generate_multiplex = pollResult.arg_int;
            }
        }
        fm_mpx_data *data2;
        data2 = (fm_mpx_data *)malloc(sizeof(fm_mpx_data));
        data2->gain = audio_gain;
        data2->paused = paused;
        data2->generate_multiplex = generate_multiplex;
        if(fm_mpx_get_samples(data, data2) < 0 ) terminate(0);
        data_len = DATA_SIZE;
        for(int i=0;i< data_len;i++) {
            devfreq[i] = data[i]*deviation_scale_factor;
        }
        fmmod->SetFrequencySamples(devfreq,data_len);
	}
    return 0;
}


int main(int argc, char **argv) {
    tx_data data = {
        .audio_file = NULL,
        .control_pipe = NULL,
        .carrier_freq = 100000000,
    };

    int af_size = 0;
    int gpiopin = 4;

    int alternative_freq[100] = {};
    int bypassfreqrange = 0;
    // Parse command-line arguments
    for(int i=1; i<argc; i++) {
        char *arg = argv[i];
        char *param = NULL;
        if(arg[0] == '-' && i+1 < argc) param = argv[i+1];
        if((strcmp("-audio", arg)==0) && param != NULL) {
            i++;
            data.audio_file = param;
        } else if(strcmp("-freq", arg)==0 && param != NULL) {
            i++;
            data.carrier_freq = (uint32_t)(atof(param)*1e6);
            if(data.carrier_freq < 64e6 || data.carrier_freq > 108e6) fatal("Incorrect frequency specification. Must be in megahertz, of the form 107.9, between 64 and 108.\n");
        } else if(strcmp("-gpiopin", arg)==0 && param != NULL) {
            i++;
            printf("GPIO pin setting disabled, mod librpitx and pifmsa (pifm simply advanced) for this\n");
            // int pinnum = atoi(param);
            // if (!(pinnum == 4 || pinnum == 20 || pinnum == 32 || pinnum == 34 || pinnum == 6)) {
            //     fatal("Invalid gpio pin, allowed: 4, 20, 32, 34, 6");
            // } else {
            //     gpiopin = pinnum;
            // }
        } else if(strcmp("-disablelogging", arg)==0) {
            i++;
            data.log = 0;
        } else if(strcmp("-bfr", arg)==0) {
            i++;
            bypassfreqrange = 1;
        } else if(strcmp("-ctl", arg)==0 && param != NULL) {
            i++;
            data.control_pipe = param;
        } else if(strcmp("-deviation", arg)==0 && param != NULL) {
            i++;
            if(strcmp("mono", param)==0) {
                data.deviation = 50000;
            } else if(strcmp("nfm", param)==0) {
                data.deviation = 2500;
            }
            else {
                data.deviation = atoi(param);
            }
        } else if(strcmp("-audiogain", arg)==0 && param != NULL) {
            i++;
            data.audio_gain = atof(param);
        } else if(strcmp("-power", arg)==0 && param != NULL) {
            i++;
            int tpower = atoi(param);
            if(tpower > 7 || tpower < 0) fatal("Power can be between 0 and 7");
            data.power = tpower;
        } else {
            fatal("Unrecognised argument: %s.\n"
            "Syntax: pi_fm_rds [-freq freq] [-audio file] [-pi pi_code]\n"
            "                  [-ps ps_text] [-rt rt_text] [-ctl control_pipe] [-pty program_type] [-raw play raw audio from stdin] [-disablerds] [-af alt freq] [-preemphasis us] [-rawchannels when using the raw option you can change this] [-rawsamplerate same business] [-deviation the deviation, default is 75000] [-tp] [-ta]\n", arg);
        }
    }

    alternative_freq[0] = af_size;
    int FifoSize=DATA_SIZE*2;
    //fmmod=new ngfmdmasync(carrier_freq,228000,14,FifoSize, false, gpiopin); //you can mod
    fmmod=new ngfmdmasync(data.carrier_freq,228000,14,FifoSize, false);
    int errcode = tx(&data);
    terminate(errcode);
}
