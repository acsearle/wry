//
//  AppDelegate.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#include "AppDelegate.h"
#include "ViewController.h"

// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

@interface AppDelegate ()

@end

@implementation AppDelegate
{
    NSWindow* _window;
    std::shared_ptr<wry::model> _model;
}

-(void) dealloc {
    printf("~AppDelegate\n");
}

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>)mdl
{
    if ((self = [super init])) {
        _model = mdl;
    }
    return self;
}

- (void)applicationWillFinishLaunching:(NSNotification *)aNotification {

    // Insert code here to initialize your application
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 1440.0, 900.0)
                                          styleMask:(NSWindowStyleMaskMiniaturizable
                                                     | NSWindowStyleMaskClosable
                                                     | NSWindowStyleMaskResizable
                                                     | NSWindowStyleMaskTitled)
                                            backing:NSBackingStoreBuffered
                                              defer:YES];
    //[_window center];
    [_window setTitle:@"Client"];
    [_window setContentViewController:[[ViewController alloc] initWithModel:_model]];
    // don't display the window until the app becomes active
    
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    [_window makeKeyAndOrderFront:nil];
}

- (void)applicationWillResignActive:(NSNotification *)aNotification {
    printf("will resign active\n");
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    printf("should terminate\n");
    return NSTerminateNow;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    printf("will terminate\n");
}


- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    return YES;
}

@end
