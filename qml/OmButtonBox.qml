import QtQuick
import QtQuick.Controls

// A dialog's button row in the theme: transparent over the card, buttons
// drawn by OmButton — including the ones Qt makes from standardButtons.
DialogButtonBox {
    spacing: 8
    padding: 12
    topPadding: 4
    alignment: Qt.AlignRight
    background: null
    delegate: OmButton {}
}
