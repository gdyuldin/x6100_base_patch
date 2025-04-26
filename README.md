# X6100 extended BASE firmware

This repo contains code and compiled firmawres for BASE part of the X6100 transceiver with some modifications/improvements.
It might be used with OEM GUI and with alternative firmware. Some of parameters (compression on/off, compression level) might be configured in the future with alternative firmware.

Disclaimer: this code provided as is, use it for your own risk.

Compiled firmwares might be downloaded at [firmwares](./firmwares/) directory.

## Changelog:

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
