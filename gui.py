import argparse

from PyQt5.QtCore import Qt, QIODevice, pyqtSignal
from PyQt5.QtWidgets import QApplication, QWidget, QLabel, QGridLayout, QSizePolicy
from PyQt5.QtSerialPort import QSerialPort

from widgets import (WaveformSelectorWidget, PresetSelectorWidget, PushButtonWidget,
                     ToggleButtonWidget, SliderWidget)

MIDI_SB_CONTROL_CHANGE = (0b1011 << 4)

MIDI_CC_LFO_FREQ           = (0x4a)
MIDI_CC_LFO_ON_OFF         = (0x4d)
MIDI_CC_OSC2_FREQ          = (0x4c)
MIDI_CC_OSC2_AMP           = (0x49)
MIDI_CC_OSC2_SYNC_ON_OFF   = (0x47)
MIDI_CC_WF_OSC1            = (0x4e)
MIDI_CC_WF_OSC2            = (0x4f)
MIDI_CC_WF_LFO             = (0x5b)
MIDI_CC_ENV_ATTACK         = (0x5d)
MIDI_CC_ENV_DECAY          = (0x5e)
MIDI_CC_ENV_SUSTAIN        = (0x0a)
MIDI_CC_ENV_RELEASE        = (0x5c)
MIDI_CC_SELECT_PRESET      = (0x07)
MIDI_CC_SAVE_PRESET        = (0x46)
MIDI_CC_DUMP_PARAMS        = (0x42)
MIDI_CC_NOISE_AMP          = (0x43)
MIDI_CC_OSC1_AMP           = (0x44)


class GUI(QWidget):
    def __init__(self, port_name, parent=None):
        super(GUI, self).__init__(parent)

        self.setWindowTitle("Synthesmaller GUI")
        self.setGeometry(100, 100, 800, 600)

        self.line_buffer = bytearray()
        self.is_parsing_values = False

        self.port = QSerialPort()
        self.port.setPortName(port_name)
        self.port.setBaudRate(115200)
        print("Opening serial port: ", self.port.open(QIODevice.ReadWrite))

        # dictionary that maps control widgets to CC values
        self.ctrl_map = {}

        # set up sliders
        for cc in [MIDI_CC_LFO_FREQ, MIDI_CC_OSC2_AMP, MIDI_CC_OSC2_FREQ,
                MIDI_CC_ENV_ATTACK, MIDI_CC_ENV_DECAY, MIDI_CC_ENV_SUSTAIN,
                MIDI_CC_ENV_RELEASE, MIDI_CC_NOISE_AMP, MIDI_CC_OSC1_AMP]:
            self.add_control(cc, SliderWidget())

        # set up toggle buttons
        self.add_control(MIDI_CC_OSC2_SYNC_ON_OFF, ToggleButtonWidget("OSC2_SYNC"))
        self.add_control(MIDI_CC_LFO_ON_OFF, ToggleButtonWidget("LFO_ON"))

        # set up waveform selectors
        self.add_control(MIDI_CC_WF_OSC1, WaveformSelectorWidget())
        self.add_control(MIDI_CC_WF_OSC2, WaveformSelectorWidget())
        self.add_control(MIDI_CC_WF_LFO, WaveformSelectorWidget())

        # set up preset selector and save button
        self.add_control(MIDI_CC_SELECT_PRESET, PresetSelectorWidget())
        self.add_control(MIDI_CC_SAVE_PRESET, PushButtonWidget("Save"))

        # dictionary that maps CC values to control widgets
        self.cc_map = {v: k for k, v in self.ctrl_map.items()}

        # set up layout
        layout = QGridLayout()

        layout.addWidget(QLabel("<b>OSC1</b>"), 0, 0)
        layout.addWidget(QLabel("Amp"), 1, 0)
        layout.addWidget(self.cc_map[MIDI_CC_OSC1_AMP], 2, 0)
        layout.addWidget(self.cc_map[MIDI_CC_WF_OSC1], 4, 0)

        layout.addWidget(QLabel("<b>OSC2</b>"), 0, 1)
        layout.addWidget(QLabel("Freq"), 1, 1)
        layout.addWidget(self.cc_map[MIDI_CC_OSC2_FREQ], 2, 1)
        layout.addWidget(QLabel("Amp"), 1, 2)
        layout.addWidget(self.cc_map[MIDI_CC_OSC2_AMP], 2, 2)
        layout.addWidget(self.cc_map[MIDI_CC_OSC2_SYNC_ON_OFF], 3, 1)
        layout.addWidget(self.cc_map[MIDI_CC_WF_OSC2], 4, 1)

        layout.addWidget(QLabel("<b>LFO</b>"), 0, 3)
        layout.addWidget(QLabel("Freq"), 1, 3)
        layout.addWidget(self.cc_map[MIDI_CC_LFO_FREQ], 2, 3)
        layout.addWidget(self.cc_map[MIDI_CC_LFO_ON_OFF], 3, 3)
        layout.addWidget(self.cc_map[MIDI_CC_WF_LFO], 4, 3)

        layout.addWidget(QLabel("<b>Envelope</b>"), 0, 4)
        layout.addWidget(QLabel("Attack"), 1, 4)
        layout.addWidget(self.cc_map[MIDI_CC_ENV_ATTACK], 2, 4)
        layout.addWidget(QLabel("Decay"), 1, 5)
        layout.addWidget(self.cc_map[MIDI_CC_ENV_DECAY], 2, 5)
        layout.addWidget(QLabel("Sustain"), 1, 6)
        layout.addWidget(self.cc_map[MIDI_CC_ENV_SUSTAIN], 2, 6)
        layout.addWidget(QLabel("Release"), 1, 7)
        layout.addWidget(self.cc_map[MIDI_CC_ENV_RELEASE], 2, 7)

        layout.addWidget(QLabel("<b>Noise</b>"), 0, 8)
        layout.addWidget(QLabel("Amp"), 1, 8)
        layout.addWidget(self.cc_map[MIDI_CC_NOISE_AMP], 2, 8)

        layout.addWidget(QLabel("<b>Presets</b>"), 0, 9)
        layout.addWidget(self.cc_map[MIDI_CC_SELECT_PRESET], 2, 9)
        layout.addWidget(self.cc_map[MIDI_CC_SAVE_PRESET], 3, 9)

        self.setLayout(layout)

        # distribute columns evenly
        for i in range(layout.columnCount()):
            spacer = QWidget()
            spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Minimum)
            layout.addWidget(spacer, 5, i)
            layout.setColumnStretch(i, 1)

        self.port.readyRead.connect(self.on_ready_read)

        self.update_from_synth()

    def add_control(self, cc: int, widget: QWidget):
        self.ctrl_map[widget] = cc
        widget.value_changed.connect(self.on_value_changed)

    def on_value_changed(self, value: int):
        frame = MIDI_SB_CONTROL_CHANGE.to_bytes(1, "little")

        if self.sender() in self.ctrl_map:
            frame += self.ctrl_map[self.sender()].to_bytes(1, "little")
            frame += value.to_bytes(1, "little")
            self.port.write(frame)
        
        if self.ctrl_map[self.sender()] == MIDI_CC_SELECT_PRESET:
            self.update_from_synth()

    def update_from_synth(self):
        frame = MIDI_SB_CONTROL_CHANGE.to_bytes(1, "little")
        frame += MIDI_CC_DUMP_PARAMS.to_bytes(1, "little")
        frame += int(1).to_bytes(1, "little")   # dummy byte
        self.port.write(frame)

    def on_ready_read(self):
        self.line_buffer += bytes(self.port.readAll())
        
        while True:
            i = self.line_buffer.find(b"\n")
            if i < 0:
                break

            line = self.line_buffer[:i].strip(b"\r\n").decode("utf-8")
            print("[LOG]", line)

            if line == "MIDI_VALUES_END":
                self.is_parsing_values = False

            if self.is_parsing_values and len(line) == 5 and line[2] == ":":
                cc, value = [int(s, base=16) for s in line.split(":")]
                self.cc_map[cc].set_value(value)

            if line == "MIDI_VALUES_START":
                self.is_parsing_values = True

            self.line_buffer[:i + 1] = b""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port_name", action="store")
    args = parser.parse_args()

    app = QApplication([])

    gui = GUI(args.port_name)
    gui.show()

    app.exec()

if __name__ == "__main__":
    main()