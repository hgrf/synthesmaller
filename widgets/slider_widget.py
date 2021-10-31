from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import QSlider


class SliderWidget(QSlider):
    value_changed = pyqtSignal(int)

    def __init__(self, parent=None):
        super(SliderWidget, self).__init__(Qt.Vertical, parent)

        self.setRange(0, 127)
        self.valueChanged.connect(lambda v: self.value_changed.emit(v))

    def set_value(self, value):
        self.blockSignals(True)
        self.setValue(value)
        self.blockSignals(False)