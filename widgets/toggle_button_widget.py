from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QPushButton


class ToggleButtonWidget(QPushButton):
    value_changed = pyqtSignal(int)

    def __init__(self, label: str, parent=None):
        super(ToggleButtonWidget, self).__init__(label, parent)

        self.setCheckable(True)
        self.toggled.connect(lambda b: self.value_changed.emit(b))
    
    def set_value(self, value):
        self.blockSignals(True)
        self.setChecked(value)
        self.blockSignals(False)