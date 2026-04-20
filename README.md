# X6100 extended BASE firmware

This repo contains code and compiled firmawres for BASE part of the X6100 transceiver with some modifications/improvements.
It might be used with OEM GUI and with alternative firmware. Some of parameters (compression on/off, compression level) might be configured in the future with alternative firmware.

Disclaimer: this code provided as is, use it for your own risk.

Compiled firmwares might be downloaded at [releases](https://github.com/gdyuldin/x6100_base_patch/releases) page (xgf file).

## Installing

1. Download one of the firmware file (click on name and then click on "View raw" link)
2. Boot with original firmware (without SD card)
3. Connect transceiver to WiFi
4. Copy downloaded file to transceiver using WinSCP or FileZilla. Destination path is `/usr/firmware`
5. Connect external power supply.
6. Open System settings -> Firmware upgrade on transceiver, choose uploaded file and update.
7. Check system info - BASE version should contain r<version> suffix

Steps 3-4 very similar to opening TX, just copy files instead of editing. https://www.youtube.com/watch?v=9oviNzXT-v8 

Also, calibration of the TX levels might be required with a patched version. 
Instruction - https://www.youtube.com/watch?v=zoN6uAewPK0
With latest R1CBU "Output gain" was renamed to "TX codec gain", also "Band output gain corr" was added. Calibration remains the same - adjust "TX codec gain" to achieve 10W output power. Then, for other bands you can adjust  "Band output gain corr", if output power is too low or ALC value is high.

## Changelog:

### r9:
* Fixed AGC on CW
* Updated filters (less bandpass ripple and "ringing", bit wider transition)
* Added TX bandpass filter control

### r8:
* Added IF shift
* Added fft_span (better zoom)
* Added bf16 flow format
* Replaced FM modulation/demodulation logic
* Added FM pre/de-emphasis
* Added DAC gain control
* Updated decimation/interpolation filters

### r7:
* Fix receiving unwanted signals for a few seconds after stop TX.  https://youtu.be/ljKug5yumMA

### r6:
* Fix SWR scan wrong values.
* Add IQ signals balancing for RX. This removing hum and reducing distortion while AM receiving.


### r4:
* Added ADC/DAC gain offset for different HW versions of the transceiver. Corresponding settings on R1CBU GUI is called "Output gain"

### r3:
* Added adaptive notch filter (can be turned on using R1CBU since v0.30.0)
* Added output gain control
* Fixed threshold and makeup offset control
* Adjusted CW output level
* Some refactoring

### r2:
* Added FM squelch
* Removes DC offset of demodulated AM/FM
* Reduces some clicking on mode change. Only FM->AM still remains, not found a way to solve it.
* Default compression ratio changed to 2:1
* Added a control for compression ratio and conmpressor on/off (will be in the next R1CBU GUI)

### r1:
* Added a compressor with noise gate for TX signals (except DATA and CW).
* FM modulation depth increased 10 times.
* Fixed AM TX (changed levels of the carrier and the signal level), added a soft limiter for AM to prevent overmodulation.
* True output power control (not only ACL threshold)
* TX bandpass filter low bound is 160 Hz


## Hacking instruction

* within `stm32f427zgtx_flash.ld` update .text section padding
* build project
* fix instructions within `asm/helper.s`
* update addresses within `patch.py`
* call `patch.py`
