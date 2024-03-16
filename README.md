if you wanna use pifmrds, then scroll down to pifmrds usage ([here](#pifmrds-usage))
![rpitx banner](/doc/rpitxlogo.png)
# About rpitx2
**rpitx2** is a general radio frequency transmitter for Raspberry Pi which doesn't require any other hardware unless filter to avoid interference. It can handle frequencies from 5 KHz up to 1500 MHz.

Rpitx2 is a software made for educational on RF system. It has not been tested for compliance with regulations governing transmission of radio signals. You are responsible for using your Raspberry Pi legally.

RPITX2 IS BASED ON RPITX

A forum is available : https://groups.io/g/rpitx

_Created by Evariste Courjaud F5OEO. See Licence for using it.

# Installation

Assuming a Raspbian Lite installation (stretch) : https://www.raspberrypi.org/downloads/raspbian/

Be sure to have git package installed :
```sh
sudo apt-get update
sudo apt-get install git
```
You can now clone the repository. A script (install.sh) is there for easy installation. You could inspect it and make steps manually in case of any doubt. You can note that /boot/config.txt should be prompt to be modified during the installation. If it is not accepted, **rpitx2** will be unstable.  

```sh
git clone https://github.com/KubaPro010/rpitx2
cd rpitx2
./install.sh
```
Make a reboot in order to use **rpitx2** in a stable state.
That's it !
```sh
sudo reboot
```
# Update
you wanna update? sure, just run `./compile.sh` if you have a error type in `chmod +x ./compile.sh` and there you go it will update and compile!

# Hardware
![bpf](/doc/bpf-warning.png)
**note: you *probably* don't need it, i've been transmitting since summer 2023 without it, and no one knocked at my door, but your country laws can be stricter**

| Raspberry Model      | Status  |
| ---------------------|:-------:|
| Pizero|OK|
| PizeroW|OK|
| PiA+|OK|
| PiB|Partial|
| PiB+|OK|
| P2B|OK|
| Pi3B|OK|
| Pi3B+|OK|
| Pi3A+|OK|
| Pi4|Partial (system may crash completly)|
| Pi400|Partial (system probably will crash completly)|
| Pi5|Doesn't work (open issue if you have an RPI 5, maybe)|

Plug a wire on GPIO 4, means Pin 7 of the GPIO header ([header P1](http://elinux.org/RPi_Low-level_peripherals#General_Purpose_Input.2FOutput_.28GPIO.29)). This acts as the antenna. The optimal length of the wire depends the frequency you want to transmit on, but it works with a few centimeters for local testing. (Use https://www.southwestantennas.com/calculator/antenna-wavelength to calculate the lenght, make sure to use the 1/4 wave setting, as for 1/2 wave you'd need a impedance transformator, and these are not cheap [also yes, i learned it the hard way after having to cut my antenna])

# Hardware (detailed compatibility
It definitly works on a RPI 3 A+, as im testing this program on it, ive used it on a pi 400, it sometimes works with no problem, but sometime it just crashes, possible cause can be this: "the rpi swtiches the gpio pin so fast it cant tell it states, for example if its 0.5 instead of high or low, whats next?" but, that just a theory, A ELECTRONIC THEORY

# How to use it
![easymenu](/doc/easymenu.png)
## Easytest
**easytest** is the easiest way to start and see some demonstration. All transmission are made on free ISM band (434MHZ).
To launch it, go to rpitx folder and launch easytest.sh :
```sh
cd rpitx
./easytest.sh
```
Choose your choice with arrows and enter to start it.**Don't forget, some test are made in loop, you have to press CTRL^C to exit and back to menu.**

Easy way to monitor what you are doing is by using a SDR software and a SDR receiver like a rtl-sdr one and set the frequency to 434MHZ.

### Carrier ### 
![Carrier](/doc/image.png)
A simple carrier generated at 434MHZ. 

### Chirp ### 
![Chirp](/doc/chirp.png)
A carrier which move around 434MHZ.

### Spectrum ###
![Spectrum](/doc/spectrumrpitx.png)
A picture is displayed on the waterfall on your SDR. Note that you should make some tweaks in order to obtain contrast and correct size depending on your reception and SDR software you use.

### RfMyFace ###
![Rfmyface](/doc/rfmyface.png)
Spectrum painting of your face using the raspicam for fun !

### FM with RDS ###
![FMRDS](/doc/fmrds.png)
Broadcast FM with RDS. You should receive it with your SDR. This is the modulation that you should hear on your classical FM Radio receiver, but at this time, the frequency is too high.

### Single Side Band modulation (SSB) ###
![SSB](/doc/ssbrpitx.png)
This is the classical Hamradio analog voice modulation. Use your SDR in USB mode.

### Slow Scan Television (SSTV) ###
![SSTV](/doc/sstvrpitx.JPG)
This is a picture transmission mode using audio modulation (USB mode). You need an extra software to decode and display it (qsstv,msstv...). This demo uses the Martin1 mode of sstv.


### Pocsag (pager mode) ###
![pocsag](/doc/pocsagrpitx.JPG)
This is a mode used by pagers. You need an extra software to decode. Set your SDR in NFM mode.

### Freedv (digital voice) ###
![freedv](/doc/freedvrpitx.JPG)
This is state of the art opensource digital modulation. You need Freedv for demodulation.

### Opera (Beacon) ###
![opera](/doc/operarpitx.JPG)
This a beacon mode which sound like Morse. You need opera in mode 0.5 to decode.

## Rpitx and low cost RTL-SDR dongle ##
![rtlmenu](/doc/rlsdrmenu.png)

**rtlmenu** allows to use rtl-sdr receiver dongle and **rpitx2** together. This combine receiver and transmission for experimenting. 
To launch it, go to rpitx folder and launch rtlmenu.sh :
```sh
./rtlmenu.sh
```
You have first to set receiver frequency and gain of rtl-sdr. Warning about gain, you should ensure that you have enough gain to receive the signal but not to strong which could saturate it and will not be useful by **rpitx2**.

Choose your choice with arrows and enter to start it.**Don't forget, some test are made in loop, you have to press CTRL^C to exit and back to menu.**


### Record and play ###
![replay](/doc/replay.png)

A typical application, is to replay a signal. Picture above shows a replay of a signal from a RF remote switch.
So first, record few seconds of signal, CTRL^C for stop recording. Then replay it with play.

### Transponder ###
![fmtransponder](/doc/fmtransponder.png)
We can also live transmitting a received band frequency. Here the input frequency is a FM broadcast station which is retransmit on 434MHZ.

### Relay with transmodulation ###
We assume that input frequency is tuned on FM station. It is demodulated and modulate to SSB on 434MHZ. SSB is not HiFi, so prefere to choose a talk radio, music sounds like bit weird !

# PiFMRds usage
See https://github.com/ChristopheJacquet/PiFmRds/blob/master/README.md and https://github.com/miegl/PiFmAdv/blob/master/README.md, it will show what you can do, but theres MORE, yeah, this ain't some that undeveloped code, this is developed, but whats the quality of the code? lets not talk about that, okay? anyways, let me show you the *features* of this :)

first, the normal args, like `pifmrds -arg argtoarg?`<br>
if you have a arg with no argume... wait what args with args? anyways like the input of the arg, right? if we'd take `-disablerds`, you need to pass something to it or the arg parser will shit itself, pass in anything, i always do `mhm`, have fuck, i mean fun (blud trying to be funny, blud saing blud, blud missplled saying)<br>
`-compressordecay` - compressor decay, specify in float (like: 0.999, default: 0.999995)<br>
`-compressorattack` - same thing but attack (default: 1.0)<br>
`-compressormaxgainrecip` - i dunno (default: 0.01)<br>
`-bfr` - by default a 65-108 freq range is defined, a freq outside makes the program crash, this bypasses the range, also it requires a arg, it can be anything as the arg is not parsed, so pass: `-bfr thisbypassescrap`<br>
`-deviation` - sets how large the fm signal can be, in khz, default is 75khz<br>
`-raw` - same arg as bfr, but this disables the format of the audio and sets channels and samplerate forcefully<br>
`-rawchannels` - change the sample rate if raw<br>
`-rawsamplerate` - same stuff but sample rate<br>
`-cutofffreq` - fm broadcast uses a cut off freq around 16-18 khz, to avoid interferance with the 19khz stereo pilot<br>
`-audiogain` - audio too loud or too quiet? use this, this defines how many times the audio can be<br>
`-power` - for now works only for rpi3, but you can change the code very easy to fix it for your pi<br>
`-disablerds` - same arg as bfr, pass this in and no rds anymore<br>
`-disablecompressor` - same as bfr, dont pass this please, if your audio is loud enough<br>
`-disablect` - disable rds ct if you'd want for some reason<br>
`-preemphasis` - you can enter 0 or off for disabled, or us (75μs), default is 50μs, you can also enter any num and have that μs of pre-emp<br>
`-af` - same as pifmadv<br>
`-rdsvolume` - rds volume, so how many times is the rds "louder"<br>
`-pilotvolume` - pilot volume<br><br>

now you know what you can pass as the args to the program, but theres a pipe still, it wont include the ones in pifmadv or pifmrds:<br>
`PI` - you can change pi code while runtime, useful when you forgot to set a pi code, but you probably won't care about it<br>
`CT` - turn ct on or off, but while *runtime*<br>
`PWR` - same as arg `-power`<br>
`RTB` - there are 2 rts, they can have both one text of content, you can use this to refresh the current rt, or if you have a sequence of rt texts, you can switch rt a and rtb (rt sets rt a and this sets rt b)<br>
`MS`  - pass "ON" or "OFF" to signal ms, `MS ON` means music is on, `MS OFF` means that speech is on
`RDS` - turn on or off rds, same as for example ms<br>
`DEV` - set deviation while *runtime*<br>
`GAI` (not gay) - set the gain while *runtime*<br>
`STR` - turn stereo on and off<br>
`COD` - change compressor decay<br>
`COA` - change compressor attack<br>
`RDV` - gain but not audio but rds gain<br>
`PAU` - pause, kinda, it will cancel out any audio, you could use `GAI 0` but you could forgor the old gain value, right?<br>
`PIV` - gain but not audio or rds but stereo pilot gain (default = 0.9)<br>
`COM` - toggle, like rds but it doesnt toggle the rds, it does toggle the COMPRESSOR<br>
`CMG` - change compressor max gain recip<br>
<br>
and thats all, and remember kids dont pirate


# Range
It has been mostly untested, but i've tested it on 95 mhz, with a 79 cm antenna, the range depending on the directionality, transmitter elavation, it can reach about 300 meters

# Transmitter Power
By default the Raspberry Pi outputs 4 mA on a gpio pin, but here it gets overriden to max, which is about 16 mA, some apps have a setting to change the power, here is a sheet of the power levels and their correspondent radiated power: (NOTE: RADIATED POWER, NOT ERP, TO HAVE THE EXACT TX POWER OF A TRANSMITTER, THE ANTENNA EFFIECENCY AND GAIN WOULD NEED TO BE COUNTED IN THE CALCULATION)
<br>**0 = 6.6 mW**
<br>**1 = 13.2 mW**
<br>**2 = 19.8 mW**
<br>**3 = 26.4 mW**
<br>**4 = 33.0 mW**
<br>**5 = 39.6 mW**
<br>**6 = 46.2 mW**
<br>**7 = 52.8 mW**<br>
milliwatt, not to be confused with megawatt, if it would be megawatts then your antenna, pi, power supply and the surrondings would dissapear

# To continue
**rpitx2** is a generic RF transmitter. There is a lot of modulation to do with it and also documentation to make all that easy to contribute. This will be the next step ! Feel free to inspect scripts, change parameters (frequencies, audio input, pictures...). 

# Credits
All rights of the original authors reserved.
F5OEO tried to include all licences and authors in sourcecode
