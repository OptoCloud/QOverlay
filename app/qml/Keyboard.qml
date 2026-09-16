import QtQuick

// On-screen keyboard: full TKL layout (function row, nav cluster, inverted-T arrows), laid
// out on an absolute unit grid so the clusters line up. The physical arrangement follows the
// OS layout: ANSI (flat Enter, backslash on the QWERTY row) or ISO (tall Enter, the extra
// <>| key, ' on the home row) — picked from windowManager.keyboardLegends.iso. Char legends
// come from windowManager.keyboardLegends.k keyed by set-1 scancode, merged over a static US
// fallback so a key is never blank (and non-printable results are ignored). Printable keys
// inject Unicode text (correct regardless of layout); control keys inject key events.
// Dual-controller: both rays hover/press keys independently. Context: windowManager,
// pointerLeft, pointerRight.
Rectangle {
    id: root

    readonly property real u: 80                       // key size (one unit)
    readonly property real gap: 8
    readonly property real pad: 24
    readonly property real keyH: u * 0.92
    readonly property real colPitch: u + gap
    readonly property real rowPitch: keyH + gap
    readonly property real rc: 15.25                   // right-cluster origin column (nav/arrows)
    readonly property real fnGap: 14                   // extra gap below the function row
    readonly property int gridRows: 6

    implicitWidth: (rc + 3) * colPitch - gap + 2 * pad
    implicitHeight: gridRows * rowPitch - gap + fnGap + 30 + 2 * pad

    radius: 22
    color: "#0f1218"
    border.color: "#252b36"
    border.width: 1

    // Caps is a real lock (persists until tapped again); Shift is one-shot — it arms for the
    // next key and auto-releases after it, matching a physical keyboard's "type one capital".
    // `shifted` (the effective shift used for legends/output) is either of them being active.
    property bool capsLock: false
    property bool shiftOneShot: false
    readonly property bool shifted: capsLock || shiftOneShot

    readonly property bool kbIso: (windowManager && windowManager.keyboardLegends
                                   && windowManager.keyboardLegends.iso === true)

    // US-ANSI fallback keyed by set-1 scancode (decimal string), used when the OS legend is
    // absent or non-printable so a key never renders blank.
    readonly property var fallback: ({
        "41":{n:"`",s:"~"},
        "2":{n:"1",s:"!"},"3":{n:"2",s:"@"},"4":{n:"3",s:"#"},"5":{n:"4",s:"$"},"6":{n:"5",s:"%"},
        "7":{n:"6",s:"^"},"8":{n:"7",s:"&"},"9":{n:"8",s:"*"},"10":{n:"9",s:"("},"11":{n:"0",s:")"},
        "12":{n:"-",s:"_"},"13":{n:"=",s:"+"},
        "16":{n:"q",s:"Q"},"17":{n:"w",s:"W"},"18":{n:"e",s:"E"},"19":{n:"r",s:"R"},"20":{n:"t",s:"T"},
        "21":{n:"y",s:"Y"},"22":{n:"u",s:"U"},"23":{n:"i",s:"I"},"24":{n:"o",s:"O"},"25":{n:"p",s:"P"},
        "26":{n:"[",s:"{"},"27":{n:"]",s:"}"},"43":{n:"\\",s:"|"},
        "30":{n:"a",s:"A"},"31":{n:"s",s:"S"},"32":{n:"d",s:"D"},"33":{n:"f",s:"F"},"34":{n:"g",s:"G"},
        "35":{n:"h",s:"H"},"36":{n:"j",s:"J"},"37":{n:"k",s:"K"},"38":{n:"l",s:"L"},"39":{n:";",s:":"},
        "40":{n:"'",s:"\""},
        "44":{n:"z",s:"Z"},"45":{n:"x",s:"X"},"46":{n:"c",s:"C"},"47":{n:"v",s:"V"},"48":{n:"b",s:"B"},
        "49":{n:"n",s:"N"},"50":{n:"m",s:"M"},"51":{n:",",s:"<"},"52":{n:".",s:">"},"53":{n:"/",s:"?"},
        "86":{n:"<",s:">"}
    })

    // Legend for a scancode: OS value if printable (code >= 32), else the US fallback, so a
    // control/dead-composed char from the OS never leaves the key blank.
    function legendFor(sc) {
        var km = (windowManager && windowManager.keyboardLegends && windowManager.keyboardLegends.k)
                 ? windowManager.keyboardLegends.k : ({})
        var fb = fallback[sc] || {n:"",s:""}
        var k = km[sc] || {}
        function pick(v, f) { return (v && v.length >= 1 && v.charCodeAt(0) >= 32) ? v : f }
        return { n: pick(k.n, fb.n), s: pick(k.s, fb.s) }
    }

    // Build the flat key list (positions in unit coords) for the current physical layout.
    function buildKeys(iso) {
        var K = []
        var r0 = root.rc, r1 = root.rc + 1, r2 = root.rc + 2   // right-cluster columns
        function ch(x, y, sc, w) { K.push({ x:x, y:y, w:(w||1), h:1, sc:sc }) }
        function mk(x, y, w, cap, o) {
            var d = { x:x, y:y, w:w, h:(o&&o.h)||1, cap:cap, mod:true }
            if (o) {
                if (o.key !== undefined) d.key = o.key
                if (o.caps) d.caps = true      // Caps Lock (persistent latch)
                if (o.shift) d.shift = true    // Shift (one-shot; auto-releases after next key)
                if (o.space) d.space = true
                if (o.enter) d.enter = true    // ISO boot-shaped Enter (custom outline)
                if (o.icon) d.icon = o.icon    // custom glyph (e.g. "win")
                if (o.emph) d.emph = true      // bolder label (Shift)
                if (o.arrow) d.arrow = true    // big bold glyph (arrow keys)
                if (o.system) d.system = true  // inject globally, not into the focused window (Win key)
            }
            K.push(d)
        }

        // Function row.
        mk(0, 0, 1, "Esc", {key:Qt.Key_Escape})
        var fx = [2,3,4,5, 6.5,7.5,8.5,9.5, 11,12,13,14]
        var fk = [Qt.Key_F1,Qt.Key_F2,Qt.Key_F3,Qt.Key_F4,Qt.Key_F5,Qt.Key_F6,
                  Qt.Key_F7,Qt.Key_F8,Qt.Key_F9,Qt.Key_F10,Qt.Key_F11,Qt.Key_F12]
        for (var f = 0; f < 12; ++f) mk(fx[f], 0, 1, "F"+(f+1), {key:fk[f]})
        mk(r0, 0, 1, "PrtSc", {key:Qt.Key_Print})
        mk(r1, 0, 1, "ScrLk", {key:Qt.Key_ScrollLock})
        mk(r2, 0, 1, "Pause", {key:Qt.Key_Pause})

        // Number row + nav.
        var num = ["41","2","3","4","5","6","7","8","9","10","11","12","13"]
        for (var n = 0; n < 13; ++n) ch(n, 1, num[n])
        mk(13, 1, 2, "⌫", {key:Qt.Key_Backspace})
        mk(r0, 1, 1, "Ins",  {key:Qt.Key_Insert})
        mk(r1, 1, 1, "Home", {key:Qt.Key_Home})
        mk(r2, 1, 1, "PgUp", {key:Qt.Key_PageUp})

        // QWERTY row + nav.
        mk(0, 2, 1.5, "Tab", {key:Qt.Key_Tab})
        var qw = ["16","17","18","19","20","21","22","23","24","25","26","27"]
        for (var q = 0; q < 12; ++q) ch(1.5 + q, 2, qw[q])
        if (!iso) ch(13.5, 2, "43")                                  // ANSI backslash
        if (iso)  mk(13.5, 2, 1.5, "⏎", {key:Qt.Key_Return, h:2, enter:true}) // boot-shaped ISO Enter
        mk(r0, 2, 1, "Del",  {key:Qt.Key_Delete})
        mk(r1, 2, 1, "End",  {key:Qt.Key_End})
        mk(r2, 2, 1, "PgDn", {key:Qt.Key_PageDown})

        // Home row + Enter.
        mk(0, 3, 1.75, "Caps", {caps:true})
        var hm = ["30","31","32","33","34","35","36","37","38","39","40"]
        for (var h = 0; h < 11; ++h) ch(1.75 + h, 3, hm[h])
        if (iso) ch(12.75, 3, "43", 1.0)                             // ISO ' fills under the Enter's top arm
        else     mk(12.75, 3, 2.25, "⏎", {key:Qt.Key_Return})        // ANSI Enter

        // Bottom row + up arrow.
        if (iso) { mk(0, 4, 1.25, "⇧ Shift", {shift:true, emph:true}); ch(1.25, 4, "86") }  // LShift + <>|
        else     { mk(0, 4, 2.25, "⇧ Shift", {shift:true, emph:true}) }
        var bt = ["44","45","46","47","48","49","50","51","52","53"]
        for (var b = 0; b < 10; ++b) ch(2.25 + b, 4, bt[b])
        mk(12.25, 4, 2.75, "⇧ Shift", {shift:true, emph:true})
        mk(r1, 4, 1, "↑", {key:Qt.Key_Up, arrow:true})               // above ↓ → inverted-T

        // Modifier row + arrows.
        mk(0,     5, 1.25, "Ctrl")
        mk(1.25,  5, 1.25, "", {key:Qt.Key_Meta, icon:"win", system:true})
        mk(2.5,   5, 1.25, "Alt")
        mk(3.75,  5, 6.25, "Space", {space:true})
        mk(10,    5, 1.25, "AltGr")
        mk(11.25, 5, 1.25, "", {key:Qt.Key_Meta, icon:"win", system:true})
        mk(12.5,  5, 1.25, "Fn")   // decorative (matches the physical Fn key position)
        mk(13.75, 5, 1.25, "Ctrl")
        mk(r0, 5, 1, "←", {key:Qt.Key_Left,  arrow:true})
        mk(r1, 5, 1, "↓", {key:Qt.Key_Down,  arrow:true})
        mk(r2, 5, 1, "→", {key:Qt.Key_Right, arrow:true})

        return K
    }

    readonly property var keys: buildKeys(kbIso)

    function activate(kd) {
        if (!windowManager || !kd) return
        if (kd.caps)  { root.capsLock = !root.capsLock; return }
        if (kd.shift) { root.shiftOneShot = !root.shiftOneShot; return }
        // Any other key consumes a one-shot Shift (Caps stays latched). Captured in `wasShifted`
        // before we clear it, so this keypress still uses the armed shift.
        var wasShifted = root.shifted
        root.shiftOneShot = false
        // System keys (Windows key) must go to the OS globally — posting them to a captured
        // window does nothing (the shell opens Start from a global input, not a window message).
        if (kd.system && kd.key !== undefined) { windowManager.sendSystemKey(kd.key); return }
        if (kd.key !== undefined) { windowManager.sendKey(kd.key, true); windowManager.sendKey(kd.key, false); return }
        if (kd.space) { windowManager.sendText(" "); return }
        if (kd.sc !== undefined) { var lg = root.legendFor(kd.sc); windowManager.sendText(wasShifted ? lg.s : lg.n); return }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 8
        text: (windowManager && windowManager.hasKeyboardFocus())
              ? "typing into last-clicked overlay" : "click an overlay to focus it"
        color: "#4a515f"; font.pixelSize: 14
    }

    Item {
        id: board
        anchors.left: parent.left
        anchors.leftMargin: root.pad
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.pad
        width: (root.rc + 3) * root.colPitch - root.gap
        height: root.gridRows * root.rowPitch - root.gap + root.fnGap

        Repeater {
            model: root.keys

            Item {
                id: keyRect
                required property var modelData
                // `modelData` is briefly undefined while the Repeater rebuilds its model (keys
                // recompute when the ISO/ANSI layout flips), so every access below is guarded.
                readonly property bool isMod: !!modelData && modelData.mod === true
                readonly property bool isEnter: !!modelData && modelData.enter === true
                readonly property bool isWin: !!modelData && modelData.icon === "win"
                readonly property var legend: (!!modelData && modelData.sc !== undefined) ? root.legendFor(modelData.sc) : null
                readonly property bool isToggleOn: !!modelData && ((modelData.caps === true && root.capsLock)
                                                                   || (modelData.shift === true && root.shiftOneShot))

                // Width of the Enter's bottom-left notch, in this item's local px (0.25u).
                readonly property real notch: 0.25 * root.colPitch

                x: modelData ? modelData.x * root.colPitch : 0
                // Extra breathing room below the function row (everything from row 1 down).
                y: modelData ? modelData.y * root.rowPitch + (modelData.y >= 1 ? root.fnGap : 0) : 0
                width: modelData ? modelData.w * root.u + (modelData.w - 1) * root.gap : 0
                height: modelData ? (modelData.h || 1) * root.keyH + ((modelData.h || 1) - 1) * root.gap : 0

                readonly property color keyColor: hot ? "#2f6df0"
                     : isToggleOn ? "#274063" : isMod ? "#161b23" : "#1e242e"
                readonly property color keyBorder: hot ? "#5b8bff" : "#2b3340"

                function over(p) {
                    if (!p || !p.active) return false
                    var lp = mapFromItem(root, p.x, p.y)
                    if (lp.x < 0 || lp.y < 0 || lp.x >= width || lp.y >= height) return false
                    // Exclude the Enter's empty bottom-left notch so the ' key beneath it wins there.
                    if (isEnter && lp.y > root.keyH && lp.x < notch) return false
                    return true
                }
                readonly property bool hot: over(pointerLeft) || over(pointerRight)
                onHotChanged: if (isEnter) enterBg.requestPaint()

                // Plain rectangular background (all keys except the boot-shaped ISO Enter).
                Rectangle {
                    visible: !keyRect.isEnter
                    anchors.fill: parent
                    radius: 11
                    color: keyRect.keyColor
                    border.color: keyRect.keyBorder
                    border.width: 1
                }

                // Boot-shaped ISO Enter: wide top arm + narrower stem, one rounded outline.
                Canvas {
                    id: enterBg
                    visible: keyRect.isEnter
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        var W = width, H = height, r = 11, nx = keyRect.notch, armH = root.keyH
                        ctx.beginPath()
                        ctx.moveTo(W / 2, 0)
                        ctx.arcTo(W, 0,  W, H,     r)
                        ctx.arcTo(W, H,  nx, H,    r)
                        ctx.arcTo(nx, H, nx, armH, r)
                        ctx.arcTo(nx, armH, 0, armH, r)
                        ctx.arcTo(0, armH, 0, 0,   r)
                        ctx.arcTo(0, 0,  W / 2, 0,  r)
                        ctx.closePath()
                        ctx.fillStyle = keyRect.keyColor
                        ctx.fill()
                        ctx.lineWidth = 1
                        ctx.strokeStyle = keyRect.keyBorder
                        ctx.stroke()
                    }
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    Component.onCompleted: requestPaint()
                }

                // Windows-logo glyph: a four-pane flag in perspective — smaller on the left
                // (receding), mirror-symmetric top-to-bottom, split by a centered cross. Drawn
                // fully inside the Canvas so it never clips the key edge.
                Canvas {
                    id: winBg
                    visible: keyRect.isWin
                    anchors.centerIn: parent
                    width: 26; height: 22
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        var W = width, H = height, m = 2, g = 1.9
                        var xL = m, xR = W - m, xm = (xL + xR) / 2 - 1.4, yc = H / 2  // cross nudged left
                        var hR = H - 2 * m           // right edge is full height
                        var hL = hR * 0.62           // left edge smaller → perspective
                        function hAt(x) { return hL + (hR - hL) * ((x - xL) / (xR - xL)) }
                        function topY(x) { return yc - hAt(x) / 2 }
                        function botY(x) { return yc + hAt(x) / 2 }
                        function quad(ax, ay, bx, by, cx, cy, dx, dy) {
                            ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by)
                            ctx.lineTo(cx, cy); ctx.lineTo(dx, dy); ctx.closePath(); ctx.fill()
                        }
                        ctx.fillStyle = keyRect.hot ? "#ffffff" : "#9db8e6"
                        quad(xL, topY(xL),     xm - g, topY(xm - g), xm - g, yc - g,       xL, yc - g)        // top-left
                        quad(xm + g, topY(xm + g), xR, topY(xR),     xR, yc - g,           xm + g, yc - g)    // top-right
                        quad(xL, yc + g,       xm - g, yc + g,       xm - g, botY(xm - g), xL, botY(xL))      // bottom-left
                        quad(xm + g, yc + g,   xR, yc + g,           xR, botY(xR),         xm + g, botY(xm + g)) // bottom-right
                    }
                    Connections { target: keyRect; function onHotChanged() { winBg.requestPaint() } }
                }

                // Text label (skipped for the Windows-icon keys).
                Text {
                    visible: !keyRect.isWin
                    anchors.centerIn: parent
                    text: !keyRect.modelData ? ""
                          : keyRect.isMod ? (keyRect.modelData.cap || "")
                          : (keyRect.legend ? (root.shifted ? keyRect.legend.s : keyRect.legend.n) : "")
                    color: keyRect.hot ? "#ffffff" : (keyRect.isMod ? "#9aa4b4" : "#e6eaf2")
                    font.pixelSize: (keyRect.modelData && keyRect.modelData.arrow) ? 28 : (keyRect.isMod ? 16 : 24)
                    font.bold: keyRect.hot || (!!keyRect.modelData && (keyRect.modelData.emph === true || keyRect.modelData.arrow === true))
                }

                Connections {
                    target: pointerLeft
                    function onPressedDown() { if (keyRect.over(pointerLeft)) root.activate(keyRect.modelData) }
                }
                Connections {
                    target: pointerRight
                    function onPressedDown() { if (keyRect.over(pointerRight)) root.activate(keyRect.modelData) }
                }
            }
        }
    }

    // VR pointer cursors are separate OpenVR overlays now (see PointerCursor). The keys use
    // pointerLeft/pointerRight for hover-highlight (`hot`) and press activation.
}
