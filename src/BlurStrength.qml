import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Blur strength as steps on one slider, from Light to Very strong, with the radius and
// passes each step sets shown beneath. A strength set through those parts matches no
// step; the slider then sits on the nearest one and says Custom until it is moved.
RowLayout {
    id: root
    property var entry: ({})
    property var st: ({})
    property bool editable: false
    property string stateId: ""
    readonly property var choices: st.choices || []
    readonly property string current: st.value === undefined ? "" : st.value
    readonly property int currentIndex: {
        for (var i = 0; i < choices.length; ++i)
            if (choices[i].value === current)
                return i
        return -1
    }
    readonly property var extra: st.extra || ({})
    readonly property int size: parseInt(extra.size) || 0
    readonly property int passes: parseInt(extra.passes) || 0
    // Kawase blur spreads about size × 2^passes, which is how near a custom pair is to each step.
    function nearest() {
        var reach = size * Math.pow(2, passes), best = 0, bestGap = Infinity
        var presets = entry.presets || {}
        for (var i = 0; i < choices.length; ++i) {
            var preset = presets[choices[i].value] || {}
            var s = preset.size !== undefined ? preset.size : 4
            var p = preset.passes !== undefined ? preset.passes : 2
            var gap = Math.abs(Math.log(s * Math.pow(2, p)) - Math.log(Math.max(1, reach)))
            if (gap < bestGap) {
                bestGap = gap
                best = i
            }
        }
        return best
    }
    spacing: 12

    ColumnLayout {
        spacing: 2
        Layout.alignment: Qt.AlignVCenter
        AppText {
            objectName: "blurStrengthName"
            Layout.alignment: Qt.AlignRight
            text: slider.pressed ? qsTranslate("catalog", root.choices[Math.round(slider.value)].label)
                  : root.currentIndex >= 0 ? qsTranslate("catalog", root.choices[root.currentIndex].label)
                  : qsTr("Custom")
            font.weight: Font.DemiBold
        }
        AppText {
            objectName: "blurStrengthParts"
            Layout.alignment: Qt.AlignRight
            visible: root.size > 0
            // Radius × passes, as Hyprland calls them.
            text: qsTr("%1 px × %2").arg(root.size).arg(root.passes)
            mono: true
            secondary: true
            scale_: 0.85
        }
        AppText {
            objectName: "blurStrengthCost"
            Layout.alignment: Qt.AlignRight
            visible: root.passes >= 3
            text: qsTr("Heavier on the GPU")
            secondary: true
            scale_: 0.85
        }
    }
    AppSlider {
        id: slider
        objectName: "blurStrengthSlider"
        label: qsTranslate("catalog", root.entry.label || "")
        Layout.preferredWidth: 200
        from: 0
        to: Math.max(0, root.choices.length - 1)
        stepSize: 1
        snapMode: Slider.SnapAlways
        enabled: root.editable
        value: root.currentIndex >= 0 ? root.currentIndex : root.nearest()
        Accessible.description: root.currentIndex >= 0 ? qsTranslate("catalog", root.choices[root.currentIndex].label) : qsTr("Custom")
        function commit() {
            var choice = root.choices[Math.round(value)]
            if (choice && choice.value !== root.current)
                win.requestChange(root.stateId, choice.value)
        }
        onPressedChanged: if (!pressed) commit()
        Keys.onReleased: function (event) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                commit()
        }
    }
}
