## Reading out the presets from the command line

    esptool.py --chip esp32 --port [port] --baud 921600 read_flash 0x210000 0x1f0000 spiffs.bin
    mkspiffs -u spiffs/ spiffs.bin

NOTE: You can get `mkspiffs` from https://github.com/igrr/mkspiffs.

## TODO

- add control of OSC1 amplitude from GUI
- add readout of current preset for GUI

- display: show sustain plateau as dashed / dotted line
- display: show envelope values
- display: add extra padding before text in black rectangles
- display: check for parameter cache initialization

- fix: sustain = 0 does not work
- improve velocity -> amplitude mapping

- review sync logic in synth_calculate_buffer()
- should envelope be in amplitude or in "power"? (log-scale)
- using a look up table for sin() could probably remove need for buffers
- include hard and/or soft reset in audio codec driver
- figure out this warning: ../main/signal_generator.c:48:5: warning: variably modified 'buffer' at file scope

## Planned features

- add more voices
- add simple sequencer
- add filters

## References

### Hardware

- SparkFun MIDI Shield: https://www.sparkfun.com/products/12898
- https://www.pjrc.com/store/teensy3_audio.html
  - datasheet: https://www.pjrc.com/teensy/SGTL5000.pdf
  - linux driver: https://github.com/torvalds/linux/blob/master/sound/soc/codecs/sgtl5000.c
  - Teensy Audio Library: https://github.com/PaulStoffregen/Audio
  - https://github.com/IoTBits/ESP32_SGTL5000_driver/
  - https://github.com/macaba/ESP32Audio
- What does capless mean: https://www.electronicproducts.com/design-considerations-for-driving-headphones/
- example for a GPIO expander with ADCs (for controls): MAX11603EEE

### MIDI

- https://www.notesandvolts.com/2012/01/fun-with-arduino-midi-input-basics.html
- https://www.youtube.com/watch?v=GxfHijjn0ZM
- https://www.instructables.com/Send-and-Receive-MIDI-with-Arduino/
- https://www.pjrc.com/teensy/td_libs_MIDI.html
- https://learn.sparkfun.com/tutorials/midi-tutorial/all
- https://www.cs.cmu.edu/~music/cmsip/readings/Standard-MIDI-file-format-updated.pdf
- https://github.com/midibox/esp32-idf-uartmidi

### Rotary encoder

- https://github.com/DavidAntliff/esp32-rotary-encoder.git

### Others

- Guenther-Chiptune: https://www.youtube.com/watch?v=msIQIWeMnBE