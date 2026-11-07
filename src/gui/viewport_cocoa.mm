// The viewport's native window on wxOSX (Cocoa): a layer-backed child NSView of the viewport's own
// view whose backing layer is a CAMetalLayer; the presenter's Vulkan surface (KosmicKrisp) is made
// on that layer. Compiled with ARC (-fobjc-arc, CMakeLists.txt). Every function here runs on the
// main thread (wx's GUI thread); the presenter thread only presents to the layer.
//
// Geometry (dvv-gpu on KosmicKrisp, Task 10): this file sets the view's frame (AppKit sets the
// backing layer's bounds from it) and the layer's contentsScale; nothing here writes drawableSize,
// which vkCreateSwapchainKHR sets to the swapchain extent. The extent is
// physicalPixelSize(view bounds, contentsScale), so the drawable maps 1:1 onto the display's
// pixels.

// clang-format off
#include <wx/wx.h>  // first (R11)
// clang-format on

#include <memory>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "core/view.h"
#include "dvv/base/error.h"
#include "dvv/platform/native_window.h"
#include "gui/native_window.h"
#include "gui/viewport.h"

// The child view. Owned by ViewportWindow::connection (a retained reference) while the presenter
// exists; its superview holds a second reference until the connection's deleter removes it.
@interface RawreelViewportView : NSView
// The viewport whose presenter draws to this view; null once the view is being removed.
@property(nonatomic, assign) rawreel::Viewport* viewport;
// Test only (setBackingScaleForTest): a scale > 0 replaces the window's backing scale factor.
@property(nonatomic, assign) CGFloat forcedScale;
// The layer's pixels per point: the forced scale, else the window's backing scale factor (the
// main screen's while the view is in no window).
- (CGFloat)backingScale;
@end

@implementation RawreelViewportView

- (BOOL)wantsUpdateLayer {
    return YES;  // nothing draws into the layer through AppKit; Vulkan presents to it
}

- (CALayer*)makeBackingLayer {
    CAMetalLayer* const layer = [CAMetalLayer layer];
    layer.opaque = YES;
    return layer;
}

// Clicks, scroll wheel and drops go to the viewport's own view (wx's handlers, the drop target):
// this view is never the hit view.
- (NSView*)hitTest:(NSPoint)point {
    static_cast<void>(point);
    return nil;
}

- (CGFloat)backingScale {
    if (self.forcedScale > 0) {
        return self.forcedScale;
    }
    if (self.window != nil) {
        return self.window.backingScaleFactor;
    }
    NSScreen* const screen = NSScreen.mainScreen;
    return screen != nil ? screen.backingScaleFactor : 1.0;
}

// Main thread: the window moved to a display with another scale factor (or the hook below). The
// view keeps its size in points, so wx sends no wxEVT_SIZE; the presenter is resized here, through
// the same entry as onSize (Review Focus 5).
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    self.layer.contentsScale = [self backingScale];
    if (self.viewport != nullptr) {
        self.viewport->resizePresenter();
    }
}

@end

namespace rawreel {

namespace {

dvv::Result<ViewportWindow> failure(const char* message) {
    return dvv::makeError(dvv::Errc::Internal, message);
}

NSView* viewOf(const wxWindow& window) {
    return window.GetHandle();
}

// The child view of `window`'s view, nil without a presenter.
RawreelViewportView* childOf(const wxWindow& window) {
    NSView* const parent = viewOf(window);
    if (parent == nil) {
        return nil;
    }
    for (NSView* view in parent.subviews) {
        if ([view isKindOfClass:[RawreelViewportView class]]) {
            return static_cast<RawreelViewportView*>(view);
        }
    }
    return nil;
}

// The child covers the parent's bounds; the layer's scale follows the view's backing scale.
void syncChild(RawreelViewportView* child, NSView* parent) {
    child.frame = parent.bounds;
    child.layer.contentsScale = [child backingScale];
}

// The deleter of ViewportWindow::connection: runs on the GUI thread after the presenter, its
// surface and its swapchain are destroyed (Viewport::stopPresenting), or when a failed
// startPresenting() drops the window, so the layer outlives every surface made from it.
void removeChild(void* retained) noexcept {
    @autoreleasepool {
        RawreelViewportView* const child = (__bridge_transfer RawreelViewportView*)retained;
        child.viewport = nullptr;  // first: leaving the window changes the backing properties
        [child removeFromSuperview];
    }
}

}  // namespace

dvv::Result<ViewportWindow> nativeWindowOf(wxWindow& window) {
    @autoreleasepool {
        NSView* const parent = viewOf(window);
        if (parent == nil) {
            return failure("the window has no NSView");
        }
        RawreelViewportView* const child =
            [[RawreelViewportView alloc] initWithFrame:parent.bounds];
        if (child == nil) {
            return failure("the viewport's Metal view could not be created");
        }
        child.wantsLayer = YES;  // the backing layer is the CAMetalLayer of makeBackingLayer
        child.autoresizingMask = NSViewNotSizable;  // syncNativeWindow() sets the frame
        if (![child.layer isKindOfClass:[CAMetalLayer class]]) {
            return failure("the viewport's view has no CAMetalLayer");
        }
        // The view only learns about resizes and scale changes while the viewport presents.
        child.viewport = dynamic_cast<Viewport*>(&window);
        [parent addSubview:child];
        syncChild(child, parent);
        void* const layer = (__bridge void*)child.layer;
        std::shared_ptr<void> connection((__bridge_retained void*)child, removeChild);
        return ViewportWindow{.window = dvv::platform::MetalWindow{.layer = layer},
                              .connection = std::move(connection)};
    }
}

void syncNativeWindow(wxWindow& window) {
    @autoreleasepool {
        if (RawreelViewportView* const child = childOf(window); child != nil) {
            syncChild(child, viewOf(window));
        }
    }
}

dvv::platform::PixelSize nativePixelSize(const wxWindow& window) {
    @autoreleasepool {
        if (RawreelViewportView* const child = childOf(window); child != nil) {
            const NSSize points = child.bounds.size;
            return physicalPixelSize(points.width, points.height, child.layer.contentsScale);
        }
        const wxSize points = window.GetClientSize();
        return physicalPixelSize(points.x, points.y, window.GetContentScaleFactor());
    }
}

bool setBackingScaleForTest(wxWindow& window, double scale) {
    @autoreleasepool {
        RawreelViewportView* const child = childOf(window);
        if (child == nil) {
            return false;
        }
        child.forcedScale = scale > 0 ? scale : 0;
        [child viewDidChangeBackingProperties];  // the real Cocoa entry, as after a display change
        return true;
    }
}

}  // namespace rawreel
