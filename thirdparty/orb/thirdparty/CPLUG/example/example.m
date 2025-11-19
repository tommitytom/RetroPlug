#include "example.c"

#if CPLUG_WANT_GUI
#import <Cocoa/Cocoa.h>

@interface MyGUIWrapper : NSView
{
@public
    MyGUI             gui;
    CFRunLoopTimerRef timerRef;
}

@end

@implementation MyGUIWrapper

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    return YES;
}

#if CPLUG_GUI_RESIZABLE
- (void)viewDidMoveToWindow
{
    NSWindow*         window      = [self window];
    NSWindowStyleMask windowStyle = [window styleMask];
    windowStyle                   |= NSWindowStyleMaskResizable;
    [window setStyleMask:windowStyle];
    [super viewDidMoveToWindow];
}
#endif // CPLUG_GUI_RESIZABLE

- (void)removeFromSuperview
{
    // Do your deinit here, not in cplug_destroyGUI, which never gets called in Audio Units!
    // Be prepared for [removeFromSuperview] to be called by a host before cplug_destroyGUI()
    if (timerRef)
    {
        CFRunLoopTimerInvalidate(timerRef);
        CFRelease(timerRef);
        timerRef = NULL;
    }

    if (gui.plugin != NULL)
        gui.plugin->gui = NULL;

    if (gui.img != NULL)
        free(gui.img);
    gui.img = NULL;
    [super removeFromSuperview];
}

- (void)setFrameSize:(NSSize)newSize
{
    // handle host resize
    uint32_t width  = newSize.width;
    uint32_t height = newSize.height;

    CPLUG_LOG_ASSERT(width > 0);
    CPLUG_LOG_ASSERT(height > 0);

    gui.width  = width;
    gui.height = height;
    gui.img    = (uint32_t*)realloc(gui.img, width * height * sizeof(*gui.img));

    [super setFrameSize:newSize];
}

- (void)viewWillAppear
{
    // handle visible = true
}

- (void)viewDidDisappear
{
    // handle visible = false
}

- (void)viewDidChangeBackingProperties:(NSNotification*)pNotification
{
    NSWindow* window      = self.window;
    CGFloat   scaleFactor = window.backingScaleFactor;
    // handle scale factor
    [super viewDidChangeBackingProperties];
}

- (void)drawRect:(NSRect)dirtyRect
{
    drawGUI(&gui);
    const unsigned char* const _Nullable data = (unsigned char*)gui.img;
    NSDrawBitmap(self.bounds, gui.width, gui.height, 8, 3, 32, 4 * gui.width, NO, NO, NSDeviceRGBColorSpace, &data);
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseDown(&gui, cursor.x, self.bounds.size.height - cursor.y);
}

- (void)mouseUp:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseUp(&gui);
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
    handleMouseMove(&gui, cursor.x, self.bounds.size.height - cursor.y);
    if (gui.mouseDragging)
        [self setNeedsDisplayInRect:self.bounds];
}

@end

void timer_cb(CFRunLoopTimerRef timer, void* info)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)info;
    if (tickGUI(&wrapper->gui))
        [wrapper setNeedsDisplayInRect:wrapper.bounds];
}

void* cplug_createGUI(void* userPlugin)
{
    NSRect frame;
    frame.origin.x    = 0;
    frame.origin.y    = 0;
    frame.size.width  = GUI_DEFAULT_WIDTH;
    frame.size.height = GUI_DEFAULT_HEIGHT;

    MyGUIWrapper* wrapper = [[MyGUIWrapper alloc] initWithFrame:frame];
    CPLUG_LOG_ASSERT_RETURN(wrapper != NULL, NULL);

    wrapper.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    wrapper.layer.opaque              = YES;

    MyGUI* gui = &wrapper->gui;
    memset(gui, 0, sizeof(*gui));

    gui->plugin      = (MyPlugin*)userPlugin;
    gui->plugin->gui = gui;

    gui->window = wrapper;
    gui->width  = GUI_DEFAULT_WIDTH;
    gui->height = GUI_DEFAULT_HEIGHT;
    gui->img    = (uint32_t*)realloc(gui->img, gui->width * gui->height * sizeof(*gui->img));

    memcpy(gui->plugin->paramValuesMain, gui->plugin->paramValuesAudio, sizeof(gui->plugin->paramValuesMain));

    CFRunLoopTimerContext context = {};
    context.info                  = wrapper;
    double interval               = 0.01; // 10ms

    wrapper->timerRef =
        CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + interval, interval, 0, 0, timer_cb, &context);
    assert(wrapper->timerRef != NULL);

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), wrapper->timerRef, kCFRunLoopCommonModes);

    return wrapper;
}

// NOTE: VST3 & CLAP only. When building with AUv2, do deinit in removeFromSuperview.
void cplug_destroyGUI(void* userGUI)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;
    if (wrapper.superview)
        [wrapper removeFromSuperview];
    [wrapper release];
}

void cplug_setParent(void* userGUI, void* view)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;
    if (wrapper.superview)
        [wrapper removeFromSuperview];
    if (view)
        [(NSView*)view addSubview:wrapper];
}

void cplug_setVisible(void* userGUI, bool visible) { [(MyGUIWrapper*)userGUI setHidden:(visible ? NO : YES)]; }

void cplug_getSize(void* userGUI, uint32_t* width, uint32_t* height)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;
    *width                = (uint32_t)wrapper.frame.size.width;
    *height               = (uint32_t)wrapper.frame.size.height;
}

bool cplug_setSize(void* userGUI, uint32_t width, uint32_t height)
{
    MyGUIWrapper* wrapper = (MyGUIWrapper*)userGUI;

    NSSize size;
    size.width  = width;
    size.height = height;
    [wrapper setFrameSize:size];
    return true;
}

void cplug_setScaleFactor(void* userGUI, float scale)
{
    // ignore. handle in 'viewDidChangeBackingProperties'
}

// AUv2 only
#ifdef CPLUG_BUILD_AUV2
#include <AudioToolbox/AUCocoaUIView.h>
#include <AudioToolbox/AudioUnit.h>

@interface CPLUG_AUV2_VIEW_CLASS : NSObject <AUCocoaUIBase>
- (NSView*)uiViewForAudioUnit:(AudioUnit)audioUnit withSize:(NSSize)preferredSize;
- (unsigned)interfaceVersion;
@end

@implementation CPLUG_AUV2_VIEW_CLASS

- (NSView*)uiViewForAudioUnit:(AudioUnit)inUnit withSize:(NSSize)size
{
    cplug_log("uiViewForAudioUnit => %p %f %f", inUnit, size.width, size.height);
    void*  userPlugin = NULL;
    UInt32 dataSize   = 8;

    AudioUnitGetProperty(inUnit, kAudioUnitProperty_UserPlugin, kAudioUnitScope_Global, 0, &userPlugin, &dataSize);
    CPLUG_LOG_ASSERT_RETURN(userPlugin != NULL, NULL);

    return (NSView*)cplug_createGUI(userPlugin);
}

- (unsigned)interfaceVersion
{
    return 0;
}

@end
#endif // CPLUG_BUILD_AUV2
#endif // CPLUG_WANT_GUI