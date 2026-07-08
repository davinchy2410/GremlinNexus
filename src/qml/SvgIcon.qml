import QtQuick
import GremblingNexus

Image {
    id: root
    
    // Default size
    width: 16
    height: 16
    
    property string pathData: ""
    property color color: Theme.text
    property real strokeWidth: 0.75

    // Fase (UI polish): most icons in this app are thin-line Feather-style
    // paths (fill="none", stroke-only) - but a Material-style solid glyph
    // (e.g. a filled pencil) is *authored* as closed fill regions, often
    // self-overlapping in ways that only look right under nonzero-fill -
    // stroking a fill-shaped path instead renders it thin/broken, not
    // "thick and solid". filled: true switches the whole SVG to
    // fill=color/stroke=none instead of adding a second icon component.
    property bool filled: false

    // Qt's svg parser needs '#' to be URL-encoded in data URIs.
    // color.toString() returns "#RRGGBB" or "#AARRGGBB"
    // We replace the leading "#" with "%23"
    readonly property string colorStr: "%23" + color.toString().substring(1)

    // Build a minimalist Feather-style (stroked) or Material-style (filled) SVG.
    source: pathData === "" ? "" : (filled
        ? "data:image/svg+xml;utf8,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"" + colorStr + "\" stroke=\"none\"><path d=\"" + pathData + "\"/></svg>"
        : "data:image/svg+xml;utf8,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"" + colorStr + "\" stroke-width=\"" + strokeWidth + "\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"" + pathData + "\"/></svg>")
    
    sourceSize.width: width
    sourceSize.height: height
    smooth: true
    antialiasing: true
}
