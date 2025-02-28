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
#include "rds.h"
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
    uint16_t pi;
    char *ps;
    char *rt;
    char *control_pipe;
    int pty;
    int af_array[100];
    int raw;
    int drds;
    double preemp;
    int power;
    int rawSampleRate;
    int rawChannels;
    int deviation;
    int ta;
    int tp;
    float cutoff_freq;
    float audio_gain;
    float compressor_decay;
    float compressor_attack;
    float compressor_max_gain_recip;
    int enablecompressor;
    int rds_ct_enabled;
    float rds_volume;
    int disablestereo;
    int log;
    float limiter_threshold;
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
    float audio_data[DATA_SIZE];
	float devfreq[DATA_SIZE];
    int data_len = 0;
    int data_index = 0;

    int generate_multiplex = 1;
    int dstereo = data->disablestereo;
    int drds = data->drds;
    float audio_gain = data->audio_gain;
    float compressor_decay = data->compressor_decay;
    float compressor_attack = data->compressor_attack;
    float compressor_max_gain_recip = data->compressor_max_gain_recip;
    int enablecompressor = data->enablecompressor;
    int rds_ct_enabled = data->rds_ct_enabled;
    float rds_volume = data->rds_volume;
    float limiter_threshold = data->limiter_threshold;

    //set the power
    padgpio gpiopad;
    gpiopad.setlevel(data->power);

    // Initialize the baseband generator
    if(fm_mpx_open(data->audio_file, DATA_SIZE, data->raw, data->preemp, data->rawSampleRate, data->rawChannels, data->cutoff_freq) < 0) return 1;

    // Initialize the RDS modulator
    char myps[9] = {0};
    set_rds_pi(data->pi);
    set_rds_ps(data->ps);
    set_rds_rt(data->rt);
    set_rds_pty(data->pty);
    set_rds_ab(0);
    set_rds_ms(1); // yes
    set_rds_tp(data->tp);
    set_rds_ta(data->ta);
    if(dstereo == 1) {
        set_rds_di(0);
    } else {
        set_rds_di(1);
    }
    uint16_t count = 0;
    uint16_t count2 = 0;
    if(data->log) {
        if(drds == 1) {
            printf("RDS Disabled (you can enable with control fifo with the RDS command)\n");
        } else {
            printf("PI: %04X, PS: \"%s\".\n", data->pi, data->ps);
            printf("RT: \"%s\"\n", data->rt);

            if(data->af_array[0]) {
                set_rds_af(data->af_array);
                printf("AF: ");
                int f;
                for(f = 1; f < data->af_array[0]+1; f++) {
                    printf("%f Mhz ", (float)(data->af_array[f]+875)/10);
                }
                printf("\n");
            }
        }
    }

    // Initialize the control pipe reader
    if(data->control_pipe) {
        if(open_control_pipe(data->control_pipe) == 0) {
            if(data->log) printf("Reading control commands on %s.\n", data->control_pipe);
        } else {
            if(data->log) printf("Failed to open control pipe: %s.\n", data->control_pipe);
            data->control_pipe = NULL;
        }
    }
    if(data->log) printf("Starting to transmit on %3.1f MHz.\n", data->carrier_freq/1e6);
    float deviation_scale_factor;
    // The deviation specifies how wide the signal is (from its lowest bandwidht to its highest, but not including sub-carriers). 
    // Use 75kHz for WFM (broadcast radio, or 50khz can be used)
    // and about 2.5kHz for NFM (walkie-talkie style radio)
    deviation_scale_factor=  0.1 * (data->deviation);
    int paused = 0;
    for (;;)
	{
        if(data->control_pipe) {
            ResultAndArg pollResult = poll_control_pipe(data->log);
            if(pollResult.res == CONTROL_PIPE_RDS_SET) {
                drds = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_PWR_SET) {
                padgpio gpiopad;
                gpiopad.setlevel(pollResult.arg_int);
            } else if(pollResult.res == CONTROL_PIPE_DEVIATION_SET) {
                data->deviation = std::stoi(pollResult.arg);
                deviation_scale_factor=  0.1 * (data->deviation);
            } else if(pollResult.res == CONTROL_PIPE_STEREO_SET) {
                dstereo = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_GAIN_SET) {
                audio_gain = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORDECAY_SET) {
                compressor_decay = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORATTACK_SET) {
                compressor_attack = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_CT_SET) {
                rds_ct_enabled = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_RDSVOL_SET) {
                rds_volume = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_PAUSE_SET) {
                paused = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_MPXGEN_SET) {
                generate_multiplex = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSOR_SET) {
                enablecompressor = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORMAXGAINRECIP_SET) {
                compressor_max_gain_recip = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_LIMITERTHRESHOLD_SET) {
                limiter_threshold = std::stof(pollResult.arg);
            }
        }
        fm_mpx_data *data2;
        data2 = (fm_mpx_data *)malloc(sizeof(fm_mpx_data));
        data2->drds = drds;
        data2->compressor_decay = compressor_decay;
        data2->compressor_attack = compressor_attack;
        data2->compressor_max_gain_recip = compressor_max_gain_recip;
        data2->dstereo = dstereo;
        data2->audio_gain = audio_gain;
        data2->enablecompressor = enablecompressor;
        data2->rds_ct_enabled = rds_ct_enabled;
        data2->rds_volume = rds_volume;
        data2->paused = paused;
        data2->generate_multiplex = generate_multiplex;
        data2->limiter_threshold = limiter_threshold;
        if(fm_mpx_get_samples(audio_data, data2) < 0 ) terminate(0);
        data_len = DATA_SIZE;
        for(int i=0;i< data_len;i++) {
            devfreq[i] = audio_data[i]*deviation_scale_factor;
        }
        fmmod->SetFrequencySamples(devfreq,data_len);
	}
    return 0;
}


int main(int argc, char **argv) {
    tx_data data = {
        .carrier_freq = 100000000,
        .audio_file = NULL,
        .pi = 0x00ff,
        .ps = "Pi-FmSa",
        .rt = "Broadcasting on a Raspberry Pi: Simply Advanced",
        .control_pipe = NULL,
        .pty = 0,
        .af_array = {0},
        .raw = 0,
        .drds = 0,
        .preemp = 50e-6,
        .power = 7,
        .rawSampleRate = 44100,
        .rawChannels = 2,
        .deviation = 75000,
        .ta = 0,
        .tp = 0,
        .cutoff_freq = 15000,
        .audio_gain = 1,
        .compressor_decay = 0.999995,
        .compressor_attack = 1.0,
        .compressor_max_gain_recip = 0.01,
        .enablecompressor = 1,
        .rds_ct_enabled = 1,
        .rds_volume = 1.0,
        .disablestereo = 0,
        .log = 1,
        .limiter_threshold = 0.9,
    };

    int af_size = 0;
    int gpiopin = 4;
    int compressorchanges = 0;
    int limiterchanges = 0;

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
        } else if(strcmp("-pi", arg)==0 && param != NULL) {
            i++;
            data.pi = atoi(param);
        } else if(strcmp("-ps", arg)==0 && param != NULL) {
            i++;
            data.ps = param;
        } else if(strcmp("-rt", arg)==0 && param != NULL) {
            i++;
            data.rt = param;
        } else if(strcmp("-compressordecay", arg)==0 && param != NULL) {
            i++;
            data.compressor_decay = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-compressorattack", arg)==0 && param != NULL) {
            i++;
            data.compressor_attack = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-compressormaxgainrecip", arg)==0 && param != NULL) {
            i++;
            data.compressor_max_gain_recip = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-limiterthreshold", arg)==0 && param != NULL) {
            i++;
            data.limiter_threshold = atof(param);
            limiterchanges = 1;
            if(data.limiter_threshold > 3.5) {
                fatal("Limiter threshold too high!\n");
            }
        } else if(strcmp("-rdsvolume", arg)==0 && param != NULL) {
            i++;
            data.rds_volume = atof(param);
        } else if(strcmp("-pty", arg)==0 && param != NULL) {
            i++;
            data.pty = atoi(param);
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
        } else if(strcmp("-ta", arg)==0) {
            i++;
            data.ta = 1;
        } else if(strcmp("-bfr", arg)==0) {
            i++;
            bypassfreqrange = 1;
        } else if(strcmp("-tp", arg)==0) {
            i++;
            data.tp = 1;
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
        } else if(strcmp("-rawchannels", arg)==0 && param != NULL) {
            i++;
            data.rawChannels = atoi(param);
        } else if(strcmp("-rawsamplerate", arg)==0 && param != NULL) {
            i++;
            data.rawSampleRate = atoi(param);
        } else if(strcmp("-cutofffreq", arg)==0 && param != NULL) {
            i++;
            data.cutoff_freq = atof(param);
        } else if(strcmp("-audiogain", arg)==0 && param != NULL) {
            i++;
            data.audio_gain = atof(param);
        } else if(strcmp("-power", arg)==0 && param != NULL) {
            i++;
            int tpower = atoi(param);
            if(tpower > 7 || tpower < 0) fatal("Power can be between 0 and 7");
            data.power = tpower;
        } else if(strcmp("-raw", arg)==0) {
            i++;
            data.raw = 1;
        } else if(strcmp("-disablerds", arg)==0) {
            i++;
            data.drds = 1;
        } else if(strcmp("-disablestereo", arg)==0) {
            i++;
            data.disablestereo = 1;
        } else if(strcmp("-disablecompressor", arg)==0) {
            i++;
            data.enablecompressor = 0;
        } else if(strcmp("-disablect", arg)==0) {
            i++;
            data.rds_ct_enabled = 0;
        } else if(strcmp("-preemphasis", arg)==0 && param != NULL) {
            i++;
            if(strcmp("us", param)==0) {
                data.preemp = 75e-6;
            } else if(strcmp("eu", param)==0) {
                printf("premp eu default but ok\n");
                data.preemp = 50e-6;
            } else if(strcmp("off", param)==0 || strcmp("0", param)==0) {
                data.preemp = 0;
            } else {
                data.preemp = atof(param) * 1e-6;
            }
        } else if(strcmp("-af", arg)==0 && param != NULL) {
            i++;
            af_size++;
            alternative_freq[af_size] = (int)(10*atof(param))-875;
            if(alternative_freq[af_size] < 1 || alternative_freq[af_size] > 204)
                fatal("Alternative Frequency has to be set in range of 87.6 Mhz - 107.9 Mhz\n");
            memcpy(data.af_array, alternative_freq, sizeof(alternative_freq));
        }
        else {
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
