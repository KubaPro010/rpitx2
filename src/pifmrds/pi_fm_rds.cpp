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

int tx(uint32_t carrier_freq, char *audio_file, uint16_t pi, char *ps, char *rt, char *control_pipe, int pty, int *af_array, int raw, int drds, double preemp, int power, int rawSampleRate, int rawChannels, int deviation, int ta, int tp, float cutoff_freq, float gaim, float compressor_decay, float compressor_attack, float compressor_max_gain_recip, int enablecompressor, int rds_ct_enabled, float rds_volume, float pilot_volume, int disablestereo) {
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
    int dstereo = disablestereo;

    //set the power
    padgpio gpiopad;
    gpiopad.setlevel(power);

    // Initialize the baseband generator
    if(fm_mpx_open(audio_file, DATA_SIZE, raw, preemp, rawSampleRate, rawChannels, cutoff_freq) < 0) return 1;

    // Initialize the RDS modulator
    char myps[9] = {0};
    set_rds_pi(pi);
    set_rds_ps(ps);
    set_rds_rt(rt);
    set_rds_pty(pty);
    set_rds_ab(0);
    set_rds_ms(1);
    set_rds_tp(tp);
    set_rds_ta(ta);
    uint16_t count = 0;
    uint16_t count2 = 0;
    if(drds == 1) {
        printf("RDS Disabled (you can enable with control fifo with the RDS command)\n");
    } else {
        printf("PI: %04X, PS: \"%s\".\n", pi, ps);
        printf("RT: \"%s\"\n", rt);

        if(af_array[0]) {
            set_rds_af(af_array);
            printf("AF: ");
            int f;
            for(f = 1; f < af_array[0]+1; f++) {
                printf("%f Mhz ", (float)(af_array[f]+875)/10);
            }
            printf("\n");
        }
    }

    // Initialize the control pipe reader
    if(control_pipe) {
        if(open_control_pipe(control_pipe) == 0) {
            printf("Reading control commands on %s.\n", control_pipe);
        } else {
            printf("Failed to open control pipe: %s.\n", control_pipe);
            control_pipe = NULL;
        }
    }
    printf("Starting to transmit on %3.1f MHz.\n", carrier_freq/1e6);
    float deviation_scale_factor;
    // The deviation specifies how wide the signal is (from its lowest bandwidht to its highest, but not including sub-carriers). 
    // Use 75kHz for WFM (broadcast radio, or 50khz can be used)
    // and about 2.5kHz for NFM (walkie-talkie style radio)
    deviation_scale_factor=  0.1 * (deviation) ; // todo PPM
    int paused = 0;
    for (;;)
	{
        if(control_pipe) {
            ResultAndArg pollResult = poll_control_pipe();
            if(pollResult.res == CONTROL_PIPE_RDS_SET) {
                drds = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_PWR_SET) {
                padgpio gpiopad;
                gpiopad.setlevel(pollResult.arg_int);
            } else if(pollResult.res == CONTROL_PIPE_DEVIATION_SET) {
                deviation = std::stoi(pollResult.arg);
                deviation_scale_factor=  0.1 * (deviation );
            } else if(pollResult.res == CONTROL_PIPE_STEREO_SET) {
                dstereo = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_GAIN_SET) {
                gaim = std::stof(pollResult.arg);
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
            } else if(pollResult.res == CONTROL_PIPE_PILVOL_SET) {
                pilot_volume = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_MPXGEN_SET) {
                generate_multiplex = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSOR_SET) {
                enablecompressor = pollResult.arg_int;
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORMAXGAINRECIP_SET) {
                compressor_max_gain_recip = std::stof(pollResult.arg);
            }
        }
        if(fm_mpx_get_samples(data, drds, compressor_decay, compressor_attack, compressor_max_gain_recip, dstereo, gaim, enablecompressor, rds_ct_enabled, rds_volume, paused, pilot_volume, generate_multiplex) < 0 ) terminate(0);
        data_len = DATA_SIZE;
        for(int i=0;i< data_len;i++) {
            devfreq[i] = data[i]*deviation_scale_factor;
        }
        fmmod->SetFrequencySamples(devfreq,data_len);
	}
    return 0;
}


int main(int argc, char **argv) {
    char *audio_file = NULL;
    char *control_pipe = NULL;
    uint32_t carrier_freq = 100000000; //100 mhz
    char *ps = "Pi-FmSa";
    char *rt = "Broadcasting on a Raspberry Pi: Simply Advanced";
    uint16_t pi = 0x1234;
    int pty = 0;
    float compressor_decay = 0.999995;
    float compressor_attack = 1.0;
    float compressor_max_gain_recip = 0.01;
    int enable_compressor = 1;
    float rds_volume = 1.0;
    float pilot_volume = 0.9;
    int ta = 0;
    int tp = 0;
    int af_size = 0;
    int gpiopin = 4;
    int raw = 0;
    int drds = 0;
    int dstereo = 0;
    int power = 7;
    float gain = 1;
    int rawSampleRate = 44100;
    int rawChannels = 2;
    int compressorchanges = 0;
    double preemp = 50e-6; //eu
    int deviation = 75000;
    int alternative_freq[100] = {};
    int bypassfreqrange = 0;
    int ct = 1;
    float cutofffreq = 16200;
    // Parse command-line arguments
    for(int i=1; i<argc; i++) {
        char *arg = argv[i];
        char *param = NULL;
        if(arg[0] == '-' && i+1 < argc) param = argv[i+1];
        if((strcmp("-audio", arg)==0) && param != NULL) {
            i++;
            audio_file = param;
        } else if(strcmp("-freq", arg)==0 && param != NULL) {
            i++;
            carrier_freq = 1e6 * atof(param);
            if((carrier_freq < 64e6 || carrier_freq > 108e6) && !bypassfreqrange) fatal("Incorrect frequency specification. Must be in megahertz, of the form 107.9, between 64 and 108. (going that low for UKF radios, such as the UNITRA Jowita or other old band FM Radios)\n");
        } else if(strcmp("-pi", arg)==0 && param != NULL) {
            i++;
            pi = (uint16_t) strtol(param, NULL, 16);
        } else if(strcmp("-ps", arg)==0 && param != NULL) {
            i++;
            ps = param;
        } else if(strcmp("-rt", arg)==0 && param != NULL) {
            i++;
            rt = param;
        } else if(strcmp("-compressordecay", arg)==0 && param != NULL) {
            i++;
            compressor_decay = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-compressorattack", arg)==0 && param != NULL) {
            i++;
            compressor_attack = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-compressormaxgainrecip", arg)==0 && param != NULL) {
            i++;
            compressor_max_gain_recip = atof(param);
            compressorchanges = 1;
        } else if(strcmp("-rdsvolume", arg)==0 && param != NULL) {
            i++;
            rds_volume = atof(param);
        } else if(strcmp("-pilotvolume", arg)==0 && param != NULL) {
            i++;
            pilot_volume = atof(param);
        } else if(strcmp("-pty", arg)==0 && param != NULL) {
            i++;
            pty = atoi(param);
        } else if(strcmp("-gpiopin", arg)==0 && param != NULL) {
            i++;
            printf("GPIO pin setting disabled, mod librpitx and pifmsa (pifm simply advanced) for this");
            // int pinnum = atoi(param);
            // if (!(pinnum == 4 || pinnum == 20 || pinnum == 32 || pinnum == 34 || pinnum == 6)) {
            //     fatal("Invalid gpio pin, allowed: 4, 20, 32, 34, 6");
            // } else {
            //     gpiopin = pinnum;
            // }
        } else if(strcmp("-ta", arg)==0) {
            i++;
            ta = 1;
        } else if(strcmp("-bfr", arg)==0) {
            i++;
            bypassfreqrange = 1;
        } else if(strcmp("-tp", arg)==0) {
            i++;
            tp = 1;
        } else if(strcmp("-ctl", arg)==0 && param != NULL) {
            i++;
            control_pipe = param;
        } else if(strcmp("-deviation", arg)==0 && param != NULL) {
            i++;
            if(strcmp("small", param)==0) {
                deviation = 50000;
            } else if(strcmp("nfm", param)==0) {
                deviation = 2500;
            }
            else {
                deviation = atoi(param);
            }
        } else if(strcmp("-rawchannels", arg)==0 && param != NULL) {
            i++;
            rawChannels = atoi(param);
        } else if(strcmp("-rawsamplerate", arg)==0 && param != NULL) {
            i++;
            rawSampleRate = atoi(param);
        } else if(strcmp("-cutofffreq", arg)==0 && param != NULL) {
                i++;
                cutofffreq = atoi(param);
        } else if(strcmp("-audiogain", arg)==0 && param != NULL) {
            i++;
            gain = atoi(param);
        } else if(strcmp("-power", arg)==0 && param != NULL) {
            i++;
            int tpower = atoi(param);
            if(tpower > 7 || tpower < 0) fatal("Power can be between 0 and 7");
            else power = tpower; //OMG SUCH ONE LINER
        } else if(strcmp("-raw", arg)==0) {
            i++;
            raw = 1;
        } else if(strcmp("-disablerds", arg)==0) {
            i++;
            drds = 1;
        } else if(strcmp("-disablestereo", arg)==0) {
            i++;
            dstereo = 1;
        } else if(strcmp("-disablecompressor", arg)==0) {
            i++;
            enable_compressor = 0;
        } else if(strcmp("-disablect", arg)==0) {
            i++;
            ct = 0;
        } else if(strcmp("-preemphasis", arg)==0 && param != NULL) {
            i++;
            if(strcmp("us", param)==0) {
                preemp = 75e-6; //usa
            } else if(strcmp("eu", param)==0) {
                printf("eu default but ok\n");
                preemp = 50e-6;
            } else if(strcmp("off", param)==0 || strcmp("0", param)==0) {
                preemp = 0; //disabled
            } else {
                preemp = atof(param) * 1e-6;
            }
        } else if(strcmp("-af", arg)==0 && param != NULL) {
            i++;
            af_size++;
            alternative_freq[af_size] = (int)(10*atof(param))-875;
            if(alternative_freq[af_size] < 1 || alternative_freq[af_size] > 204)
                fatal("Alternative Frequency has to be set in range of 87.6 Mhz - 107.9 Mhz\n"); //honestly i have no idea why 87.5 and 108 isn't in here, i copied this code, okay?
        }
        else {
            fatal("Unrecognised argument: %s.\n"
            "Syntax: pi_fm_rds [-freq freq] [-audio file] [-pi pi_code]\n"
            "                  [-ps ps_text] [-rt rt_text] [-ctl control_pipe] [-pty program_type] [-raw play raw audio from stdin] [-disablerds] [-af alt freq] [-preemphasis us] [-rawchannels when using the raw option you can change this] [-rawsamplerate same business] [-deviation the deviation, default is 75000] [-tp] [-ta]\n", arg);
        }
    }
    if(compressorchanges) {
        printf("You've changed the compressor settings, just don't set it too low, so the deviation won't go crazy\n");
    }
    if(!enable_compressor) {
        printf("DUDE YOU ARE CRAZY?\n");
    }
    alternative_freq[0] = af_size;
    int FifoSize=DATA_SIZE*2;
    //fmmod=new ngfmdmasync(carrier_freq,228000,14,FifoSize, false, gpiopin); //you can mod
    fmmod=new ngfmdmasync(carrier_freq,228000,14,FifoSize, false);
    int errcode = tx(carrier_freq, audio_file, pi, ps, rt, control_pipe, pty, alternative_freq, raw, drds, preemp, power, rawSampleRate, rawChannels, deviation, ta, tp, cutofffreq, gain, compressor_decay, compressor_attack, compressor_max_gain_recip, enable_compressor, ct, rds_volume, pilot_volume, dstereo);
    terminate(errcode);
}
