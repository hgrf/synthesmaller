from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QPushButton


class PushButtonWidget(QPushButton):
    value_changed = pyqtSignal(int)

    def __init__(self, label: str, parent=None):
        super(PushButtonWidget, self).__init__(label, parent)

        self.clicked.connect(lambda: self.value_changed.emit(1))