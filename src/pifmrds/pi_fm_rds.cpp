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
static volatile uint32_t *pad_reg;
#define GPIO_PAD_0_27                   (0x2C/4)
#define GPIO_PAD_28_45                  (0x30/4)
#define PI 3

#if (PI) == 1 //Original
#define PERIPH_VIRT_BASE                0x20000000
#elif (PI) == 2
#define PERIPH_VIRT_BASE                0x3f000000
#elif (PI) == 3
#define PERIPH_VIRT_BASE                0x3f000000
#elif (PI) == 4
#define PERIPH_VIRT_BASE                0xfe000000
#else
#error Unknown Raspberry Pi version (variable PI)
#endif
//i dunno it for pi 5, but rp1 is gonna steer the io: https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf
#define SUBSIZE 512
#define DATA_SIZE 5000
#define PAD_LEN                         (0x40/4) //0x64
#define PAD_BASE_OFFSET                 0x00100000
#define PAD_VIRT_BASE                   (PERIPH_VIRT_BASE + PAD_BASE_OFFSET)

static void
terminate(int num)
{
    delete fmmod;
    pad_reg[GPIO_PAD_0_27] = 0x5a000018 + 1; //Set original power, just in case
    pad_reg[GPIO_PAD_28_45] = 0x5a000018 + 1;
    fm_mpx_close();
    close_control_pipe();
    exit(num);
}

static void
fatal(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    terminate(0);
}

static volatile void *map_peripheral(uint32_t base, uint32_t len)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    void * vaddr;
    if (fd < 0)
        fatal("Failed to open /dev/mem: %m.\n");
    vaddr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, base);
    if (vaddr == MAP_FAILED)
        fatal("Failed to map peripheral at 0x%08x: %m.\n", base);
    close(fd);
    return vaddr;
}

int tx(uint32_t carrier_freq, char *audio_file, uint16_t pi, char *ps, char *rt, float ppm, char *control_pipe, int pty, int *af_array, int raw, int drds, double preemp, int power, int rawSampleRate, int rawChannels, int deviation, int ta, int tp, float cutoff_freq, float gaim, float compressor_decay, float compressor_attack, float compressor_max_gain_recip, int enablecompressor, int rds_ct_enabled) {
    // Catch all signals possible - it is vital we kill the DMA engine
    // on process exit!
    // for (int i = 0; i < 64; i++) {
    //     struct sigaction sa;

    //     memset(&sa, 0, sizeof(sa));
    //     sa.sa_handler = terminate;
    //     sigaction(i, &sa, NULL);
    // }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = terminate;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGKILL, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL); //https://www.gnu.org/software/libc/manual/html_node/Termination-Signals.html

    //Set the power
    pad_reg = (volatile uint32_t *)map_peripheral(PAD_VIRT_BASE, PAD_LEN);
    pad_reg[GPIO_PAD_0_27] = 0x5a000018 + power;
    pad_reg[GPIO_PAD_28_45] = 0x5a000018 + power;
    
    // Data structures for baseband data
    float data[DATA_SIZE];
	float devfreq[DATA_SIZE];
    int data_len = 0;
    int data_index = 0;

    int disablestereo = 0;

    // Initialize the baseband generator
    if(fm_mpx_open(audio_file, DATA_SIZE, raw, preemp, rawSampleRate, rawChannels, cutoff_freq) < 0) return 1;

    // Initialize the RDS modulator
    char myps[9] = {0};
    set_rds_pi(pi);
    set_rds_rt(rt);
    set_rds_pty(pty);
    set_rds_ab(0);
    set_rds_ms(1);
    set_rds_tp(tp);
    set_rds_ta(ta);
    uint16_t count = 0;
    uint16_t count2 = 0;
    int varying_ps = 0;

    if(drds == 1) {
        printf("RDS Disabled\n");
    } else {
        if(ps) {
            set_rds_ps(ps);
            printf("PI: %04X, PS: \"%s\".\n", pi, ps);
        } else {
            printf("PI: %04X, PS: <Varying>.\n", pi);
            varying_ps = 1;
        }
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
    //if( divider ) // PLL modulation
    {   // note samples are [-10:10]

        // The deviation specifies how wide the signal is (from its lowest bandwidht to its highest, but not including sub-carriers). 
        // Use 75kHz for WFM (broadcast radio, or 50khz can be used)
        // and about 2.5kHz for NFM (walkie-talkie style radio)
        deviation_scale_factor=  0.1 * (deviation ) ; // todo PPM
    }

    for (;;)
	{
        if(drds == 0) {
            // Default (varying) PS
            if(varying_ps) {
                if(count == 512) {
                    snprintf(myps, 9, "%08d", count2);
                    set_rds_ps(myps);
                    count2++;
                }
                if(count == 1024) {
                    set_rds_ps("RPi-Live");
                    count = 0;
                }
                count++;
            }
        }

        if(control_pipe) {
            ResultAndArg pollResult = poll_control_pipe();
            if(pollResult.res == CONTROL_PIPE_PS_SET) {
                varying_ps = 0;
            } else if(pollResult.res == CONTROL_PIPE_RDS_SET) {
                drds = (int)pollResult.arg;
            } else if(pollResult.res == CONTROL_PIPE_PWR_SET) {
                pad_reg[GPIO_PAD_0_27] = 0x5a000018 + (int)pollResult.arg;
                pad_reg[GPIO_PAD_28_45] = 0x5a000018 + (int)pollResult.arg;
            } else if(pollResult.res == CONTROL_PIPE_DEVIATION_SET) {
                deviation = atoi(pollResult.arg);
                deviation_scale_factor=  0.1 * (deviation );
            } else if(pollResult.res == CONTROL_PIPE_STEREO_SET) {
                disablestereo = (int)pollResult.arg;
            } else if(pollResult.res == CONTROL_PIPE_GAIN_SET) {
                gaim = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORDECAY_SET) {
                compressor_decay = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_COMPRESSORATTACK_SET) {
                compressor_attack = std::stof(pollResult.arg);
            } else if(pollResult.res == CONTROL_PIPE_CT_SET) {
                rds_ct_enabled = (int)pollResult.arg;
            }
        }

			if( fm_mpx_get_samples(data, drds, compressor_decay, compressor_attack, compressor_max_gain_recip, disablestereo, gaim, enablecompressor, rds_ct_enabled) < 0 ) {
                    terminate(0);
                }
                data_len = DATA_SIZE;
				for(int i=0;i< data_len;i++)
			{

            	devfreq[i] = data[i]*deviation_scale_factor;


            }
			fmmod->SetFrequencySamples(devfreq,data_len);
	}

    return 0;
}


int main(int argc, char **argv) {
    char *audio_file = NULL;
    char *control_pipe = NULL;
    uint32_t carrier_freq = 100000000;
    char *ps = "Pi-FmSa";
    char *rt = "Broadcasting on a Raspberry Pi: Simply Advanced";
    uint16_t pi = 0x1234;
    int pty = 0;
    float compressor_decay = 0.999995;
    float compressor_attack = 1.0;
    float compressor_max_gain_recip = 0.01;
    int enable_compressor = 1;
    int ta = 0;
    int tp = 0;
    int af_size = 0;
    int gpiopin = 4;
    int raw = 0;
    int drds = 0;
    int power = 7;
    float gain = 1;
    int rawSampleRate = 44100;
    int rawChannels = 2;
    int compressorchanges = 0;
    double preemp = 50e-6; //eu
    int deviation = 75000;
    int alternative_freq[100] = {};
    float ppm = 0;
    int bypassfreqrange = 0;
    int ct = 1;
    float cutofffreq = 15700;
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
            if((carrier_freq < 64e6 || carrier_freq > 108e6) && !bypassfreqrange)
               fatal("Incorrect frequency specification. Must be in megahertz, of the form 107.9, between 64 and 108. (going that low for UKF radios, such as the UNITRA Jowita or other old band FM Radios)\n");
        } else if(strcmp("-pi", arg)==0 && param != NULL) {
            i++;
            pi = (uint16_t) strtol(param, NULL, 16);
        } else if(strcmp("-ps", arg)==0 && param != NULL) {
            i++;
            ps = param;
        } else if(strcmp("-rt", arg)==0 && param != NULL) {
            i++;
            rt = param;
        } else if(strcmp("-ppm", arg)==0 && param != NULL) {
            i++;
            ppm = atof(param);
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
        } else if(strcmp("-pty", arg)==0 && param != NULL) {
            i++;
            pty = atoi(param);
        } else if(strcmp("-gpiopin", arg)==0 && param != NULL) {
            i++;
            int pinnum = atoi(param);
            if (!(pinnum == 4 || pinnum == 20 || pinnum == 32 || pinnum == 34 || pinnum == 6)) {
                fatal("Invalid gpio pin, allowed: 4, 20, 32, 34, 6");
            } else {
                gpiopin = pinnum;
            }
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
            if(strcmp("ukf", param)==0) {
                deviation = 65000; //i don't know the original bandwidht, but when testing on an UNITRA LIZA R-203, the sound doesn't sound out of order, correct this if im wrong
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
            if(tpower > 7 || tpower < 0) {
                fatal("Power can be between 0 and 7");
            } else {
                power = tpower;
            }
        } else if(strcmp("-raw", arg)==0) {
            i++;
            raw = 1;
        } else if(strcmp("-disablerds", arg)==0) {
            i++;
            drds = 1;
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
            } else if(strcmp("22", param)==0) {
                preemp = 22e-6; //22
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
            "Syntax: pi_fm_rds [-freq freq] [-audio file] [-ppm ppm_error] [-pi pi_code]\n"
            "                  [-ps ps_text] [-rt rt_text] [-ctl control_pipe] [-pty program_type] [-raw play raw audio from stdin] [-disablerds] [-af alt freq] [-preemphasis us] [-rawchannels when using the raw option you can change this] [-rawsamplerate same business] [-deviation the deviation, default is 75000, there are 2 predefined other cases: ukf (for old radios such as the UNITRA Jowita), nfm] [-tp] [-ta]\n", arg);
        }
    }
    if(compressorchanges) {
        printf("You've changed the compressor settings, just don't set it too low, so the deviation won't go crazy\n");
    }
    if(enable_compressor) {
        printf("DUDE YOU ARE CRAZY?\n");
    }
    alternative_freq[0] = af_size;
    int FifoSize=DATA_SIZE*2;
    //fmmod=new ngfmdmasync(carrier_freq,228000,14,FifoSize, false, gpiopin); //you can mod
    fmmod=new ngfmdmasync(carrier_freq,228000,14,FifoSize, false);
    int errcode = tx(carrier_freq,  audio_file, pi, ps, rt, ppm, control_pipe, pty, alternative_freq, raw, drds, preemp, power, rawSampleRate, rawChannels, deviation, ta, tp, cutofffreq, gain, compressor_decay, compressor_attack, compressor_max_gain_recip, enable_compressor, ct);
    terminate(errcode);
}
