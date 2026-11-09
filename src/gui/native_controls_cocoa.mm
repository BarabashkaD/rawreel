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
#include <optional>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include "gui/native_controls.h"

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

// The button's NSView is its NSButton, the element VoiceOver reads.
void setAccessibleName(wxWindow& window, const wxString& name) {
    NSView* const view = window.GetHandle();
    if (view == nil) {
        return;
    }
    [view setAccessibilityLabel:[NSString stringWithUTF8String:name.utf8_str().data()]];
}

}  // namespace rawreel
