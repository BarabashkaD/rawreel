// The native slider queries on wxOSX (Cocoa). wxSlider's handle is its NSSlider; the cell gives the
// bar (track) and knob (thumb) rects in the view's bounds. wxOSX places the view so that its
// alignment rect is the wx window's rect (wxWidgetCocoaImpl::GetLayoutInset, wxWindowMac::
// MacGetBorderSize), so a wx client x is a bounds x less the left alignment-rect inset. Compiled
// with ARC (-fobjc-arc, CMakeLists.txt); main thread only.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include "dvv/media/types.h"
#include "gui/native_controls.h"

// wx's own maker of a bundle that hands wxOSX the NSImage itself (wx/private/bmpbndl.h, which wx
// does not install; the function is exported from wx core). wxStockPreferencesPage gets its SF
// Symbols through it, so the toolbar keeps the template image at every size.
wxBitmapBundle wxOSXMakeBundleFromImage(WXImage image);

namespace rawreel {

namespace {

struct SliderRects {
    NSRect bar;
    NSRect knob;
    CGFloat toWx;  // added to a bounds x to give a wx client x
};

std::optional<SliderRects> sliderRects(const wxSlider& slider) {
    NSView* const view = slider.GetHandle();
    if (view == nil || ![view isKindOfClass:[NSSlider class]]) {
        return std::nullopt;
    }
    NSCell* const cell = [static_cast<NSSlider*>(view) cell];
    if (![cell isKindOfClass:[NSSliderCell class]]) {
        return std::nullopt;
    }
    NSSliderCell* const slider_cell = static_cast<NSSliderCell*>(cell);
    const BOOL flipped = [view isFlipped];
    return SliderRects{[slider_cell barRectFlipped:flipped], [slider_cell knobRectFlipped:flipped],
                       -NSMinX([view bounds]) - [view alignmentRectInsets].left};
}

int toInt(CGFloat x) {
    return static_cast<int>(std::lround(x));
}

}  // namespace

// The knob stays inside the bar: its centre travels from the bar's left end plus half the knob's
// width to the right end less that half.
std::optional<SliderTrack> sliderTrack(const wxSlider& slider) {
    const std::optional<SliderRects> rects = sliderRects(slider);
    if (!rects) {
        return std::nullopt;
    }
    const CGFloat half = NSWidth(rects->knob) / 2;
    return SliderTrack{toInt(rects->toWx + NSMinX(rects->bar) + half),
                       toInt(rects->toWx + NSMaxX(rects->bar) - half), toInt(NSWidth(rects->knob))};
}

std::optional<int> nativeThumbCentre(const wxSlider& slider) {
    const std::optional<SliderRects> rects = sliderRects(slider);
    if (!rects) {
        return std::nullopt;
    }
    return toInt(rects->toWx + NSMidX(rects->knob));
}

// wxOSX gives a mouse event's x in the view's bounds (wxSetupCoordinates): shift it as the track.
int sliderEventX(const wxSlider& slider, int event_x) {
    const std::optional<SliderRects> rects = sliderRects(slider);
    if (!rects) {
        return event_x;
    }
    return toInt(rects->toWx + event_x);
}

std::optional<wxRect> sliderThumbRect(const wxSlider& /*slider*/) {
    return std::nullopt;
}

// A sublayer of the NSSlider's layer, found by its name; no implicit animation, so it follows the
// pointer at once.
void showSliderGhost(wxSlider& slider, std::optional<int> x) {
    NSView* const view = slider.GetHandle();
    if (view == nil || ![view isKindOfClass:[NSSlider class]]) {
        return;
    }
    [view setWantsLayer:YES];
    CALayer* ghost = nil;
    for (CALayer* sub in view.layer.sublayers) {
        if ([sub.name isEqualToString:@"rawreelGhostThumb"]) {
            ghost = sub;
        }
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!x) {
        [ghost removeFromSuperlayer];
    } else if (const std::optional<SliderRects> rects = sliderRects(slider)) {
        if (ghost == nil) {
            ghost = [CALayer layer];
            ghost.name = @"rawreelGhostThumb";
            ghost.backgroundColor = [[NSColor systemGrayColor] colorWithAlphaComponent:0.5].CGColor;
            ghost.zPosition = 10;
            [view.layer addSublayer:ghost];
        }
        const NSRect knob = rects->knob;
        const CGFloat centre = static_cast<CGFloat>(*x) - rects->toWx;
        ghost.frame =
            CGRectMake(centre - (NSWidth(knob) / 2), NSMinY(knob), NSWidth(knob), NSHeight(knob));
        ghost.cornerRadius = std::min(NSWidth(knob), NSHeight(knob)) / 2;
    }
    [CATransaction commit];
}

namespace {

// The undo manager of `window`'s editable text view while it has the keyboard focus; nil
// otherwise. A single-line field (NSTextField, NSComboBox) edits through its window's field
// editor, whose delegate is the field.
NSUndoManager* textUndoManager(wxWindow& window) {
    NSView* const view = window.GetHandle();
    NSResponder* const responder = view != nil ? [[view window] firstResponder] : nil;
    if (responder == nil || ![responder isKindOfClass:[NSTextView class]]) {
        return nil;
    }
    NSTextView* const text = static_cast<NSTextView*>(responder);
    if (![text isEditable]) {
        return nil;
    }
    NSView* owner = text;
    if ([text isFieldEditor] && [[text delegate] isKindOfClass:[NSView class]]) {
        owner = static_cast<NSView*>(static_cast<id>([text delegate]));
    }
    if (owner != view && ![owner isDescendantOf:view]) {
        return nil;
    }
    return [text undoManager];
}

}  // namespace

bool canUndoText(wxWindow& window, bool redo) {
    NSUndoManager* const manager = textUndoManager(window);
    return manager != nil && (redo ? [manager canRedo] : [manager canUndo]);
}

bool undoText(wxWindow& window, bool redo) {
    NSUndoManager* const manager = textUndoManager(window);
    if (manager == nil || !(redo ? [manager canRedo] : [manager canUndo])) {
        return false;
    }
    if (redo) {
        [manager redo];
    } else {
        [manager undo];
    }
    return true;
}

wxString representedFilename(wxTopLevelWindow& window) {
    NSWindow* const ns_window = window.GetWXWindow();
    NSString* const name = ns_window != nil ? [ns_window representedFilename] : nil;
    const char* const text = name != nil ? [name UTF8String] : nullptr;
    return text != nullptr ? wxString::FromUTF8(text) : wxString();
}

wxBitmapBundle settingsPageIcon(std::string_view sf_symbol) {
    NSString* const name = [[NSString alloc] initWithBytes:sf_symbol.data()
                                                    length:sf_symbol.size()
                                                  encoding:NSUTF8StringEncoding];
    NSImage* const image =
        name != nil ? [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil] : nil;
    if (image == nil) {
        return {};
    }
    [image setTemplate:YES];  // SF Symbols are; the toolbar tints a template image
    return wxOSXMakeBundleFromImage(image);
}

void revealInFileManager(const std::filesystem::path& file_or_folder) {
    const std::string text = dvv::media::utf8FromPath(file_or_folder);
    NSString* const path = [NSString stringWithUTF8String:text.c_str()];
    if (path == nil) {
        return;
    }
    [[NSWorkspace sharedWorkspace]
        activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:path] ]];
}

// The button's NSView is its NSButton, the element VoiceOver reads.
void setAccessibleName(wxWindow& window, const wxString& name) {
    NSView* const view = window.GetHandle();
    if (view == nil) {
        return;
    }
    [view setAccessibilityLabel:[NSString stringWithUTF8String:name.utf8_str().data()]];
}

// secondaryLabelColor is a dynamic colour; wx keeps a fixed one, so it is resolved here for the
// appearance the label draws with.
void setSecondaryText(wxWindow& label) {
    NSView* const view = label.GetHandle();
    NSAppearance* const appearance =
        view != nil ? [view effectiveAppearance] : [NSApp effectiveAppearance];
    __block NSColor* colour = nil;
    [appearance performAsCurrentDrawingAppearance:^{
      colour = [[NSColor secondaryLabelColor] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    }];
    if (colour == nil) {
        return;
    }
    const auto channel = [](CGFloat value) {
        return static_cast<unsigned char>(
            std::lround(std::clamp(value, CGFloat{0}, CGFloat{1}) * 255));
    };
    label.SetForegroundColour(
        wxColour(channel([colour redComponent]), channel([colour greenComponent]),
                 channel([colour blueComponent]), channel([colour alphaComponent])));
}

}  // namespace rawreel
