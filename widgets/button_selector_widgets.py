from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QWidget, QPushButton, QButtonGroup, QVBoxLayout


class ButtonSelectorWidget(QWidget):
    buttons = {}

    value_changed = pyqtSignal(int)

    def __init__(self, parent=None):
        super(ButtonSelectorWidget, self).__init__(parent)

        self.group = QButtonGroup(self)
        layout = QVBoxLayout()

        for btn_id, btn_info in self.buttons.items():
            btn = QPushButton(btn_info["label"])
            btn.setCheckable(True)
            layout.addWidget(btn)
            self.group.addButton(btn, btn_id)

        self.group.setExclusive(True)
        self.group.button(0).setChecked(True)
        self.group.idToggled.connect(self.on_id_toggled)
        
        self.setLayout(layout)

    def on_id_toggled(self, btn_id, checked):
        if checked:
            self.value_changed.emit(self.buttons[btn_id]["midi_value"])

    def set_value(self, value):
        for btn_id, btn_info in self.buttons.items():
            if value == btn_info["midi_value"]:
                self.group.blockSignals(True)
                self.group.button(btn_id).setChecked(True)
                self.group.blockSignals(False)
                break


class WaveformSelectorWidget(ButtonSelectorWidget):
    buttons = {
        0: {"label": "Sine", "midi_value": 0},
        1: {"label": "Sawtooth", "midi_value": 16},
        2: {"label": "Square", "midi_value": 32},
    }


class PresetSelectorWidget(ButtonSelectorWidget):
    buttons = {
        0: {"label": "P0", "midi_value": 0},
        1: {"label": "P1", "midi_value": 20},
        2: {"label": "P2", "midi_value": 40},
        3: {"label": "P3", "midi_value": 60},
        4: {"label": "P4", "midi_value": 80},
        5: {"label": "P5", "midi_value": 100},
        6: {"label": "P6", "midi_value": 120},
    }