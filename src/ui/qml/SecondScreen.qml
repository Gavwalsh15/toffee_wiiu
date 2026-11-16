import QtQuick

Item {
    id: mode
    required property var preferences
    required property var stream
    required property var monitor
    required property var frontend
    required property var flasher
    required property var workspace

    readonly property bool on: preferences.secondScreen
    // Set when the user stops the stream or disconnects by hand, so Toffee does not undo it.
    // Turning the mode off and on again clears it.
    property bool held: false
    function hold() { if (on) held = true }
    property bool needsFlash: false
    Connections {
        target: mode.stream
        function onBridgeIncompatible() { mode.needsFlash = true }
        // A bridge connected by hand that turns out compatible clears it too.
        function onChanged() { if (mode.stream.ready) mode.needsFlash = false }
    }
    Connections {
        target: mode.flasher
        function onFinished(success) { if (success) mode.needsFlash = false }
    }
    onOnChanged: {
        held = false
        needsFlash = false
        if (!on && stream.streaming)
            stream.stopStream()
    }

    readonly property bool capturingMonitor: monitor.screenName.length > 0
        && stream.sceneMode === StreamController.DisplayCapture
        && stream.captureTarget >= 0
        && String(stream.captureTargets[stream.captureTarget]?.label ?? "").startsWith(monitor.screenName)
    readonly property string port: (workspace.portPicked || frontend.suggestedPort >= 0)
        && workspace.selectedPortIndex < frontend.ports.length
        ? frontend.ports[workspace.selectedPortIndex].name : ""

    readonly property string status: {
        if (!on)
            return ""
        if (!monitor.installed)
            return "Install the monitor driver in Settings first."
        if (monitor.problem && monitor.status)
            return monitor.status
        if (needsFlash)
            return "The bridge needs this version's firmware. Flash it from the Flash ESP page."
        if (held)
            return "Paused. Press Start stream, or turn second screen off and on, to carry on."
        if (!stream.connected)
            return port ? "Connecting to the bridge on " + port + "…" : "Plug in the ESP32-C5 bridge."
        if (stream.pairingCode)
            return "Press SYNC on the GamePad and enter " + stream.pairingCode + "."
        if (!stream.ready)
            return "Turn on the GamePad."
        if (!monitor.active)
            return "Adding the GamePad monitor…"
        if (!capturingMonitor || !stream.sourceReady)
            return "Capturing the GamePad monitor…"
        if (!stream.streaming)
            return "Starting the stream…"
        return "On. Drag windows onto the GamePad monitor; touch the GamePad to click."
    }

    Timer {
        interval: 1000; repeat: true
        running: mode.on && !mode.held && !mode.needsFlash
        onTriggered: {
            const s = mode.stream
            if (mode.flasher.flashing || mode.workspace.currentPage === 0 || s.busy && !s.streaming)
                return
            if (!s.connected) {
                if (mode.port)
                    s.connectDevice(mode.port)
                return
            }
            if (!mode.monitor.active || s.streaming)
                return
            if (!mode.capturingMonitor) {
                s.captureDisplay(mode.monitor.screenName)
                mode.workspace.showCapture()
                return
            }
            if (s.ready && s.sourceReady)
                s.startStream()
        }
    }
}
