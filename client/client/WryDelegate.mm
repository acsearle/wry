//
//  WryDelegate.mm
//  client
//
//  Created by Antony Searle on 21/6/2023.
//

#import <AVFoundation/AVFoundation.h>

#include "ClientRenderer.h"
#include "ClientView.h"
#include "WryDelegate.h"


// [1] https://sarunw.com/posts/how-to-create-macos-app-without-storyboard/

// Our simple application with a single window and view uses a single class as
// application delegate, window delegate and first responder to respond to all
// user interface events

@interface WryDelegate ()

@end

@implementation WryDelegate
{
    std::shared_ptr<wry::model> _model;
    NSWindow* _window;
    ClientRenderer *_renderer;
    
    AVAudioEngine* _audio_engine;
    AVAudioPCMBuffer* _audio_buffer;
    AVAudioPCMBuffer* _audio_buffer2;
    NSMutableArray<AVAudioPlayerNode*>* _audio_players;
    AVAudioEnvironmentNode* _audio_environment;
    
}

-(nonnull instancetype) initWithModel:(std::shared_ptr<wry::model>)mdl
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    if ((self = [super init])) {
        _model = mdl;
    }
    return self;
}

-(void) dealloc {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark NSApplicationDelegate

- (void)applicationWillFinishLaunching:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    
    NSRect contentRect = NSMakeRect(0.0, 0.0, 960, 540);
    _window = [[NSWindow alloc] initWithContentRect:contentRect
                                          styleMask:(NSWindowStyleMaskMiniaturizable
                                                     | NSWindowStyleMaskClosable
                                                     | NSWindowStyleMaskResizable
                                                     | NSWindowStyleMaskTitled)
                                            backing:NSBackingStoreBuffered
                                              defer:YES];
    _window.delegate = self;
    _window.title = @"WryApplication";
    [_window center];
    //[_window setContentViewController:[[ViewController alloc] initWithModel:_model]];
    // [_window setContentView:[
    
    WryMetalView* view = [[WryMetalView alloc]
                        initWithFrame:contentRect model:_model];
    
    view.metalLayer.device =  MTLCreateSystemDefaultDevice();
    view.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    view.delegate = self;
    
    _renderer = [[ClientRenderer alloc] initWithMetalDevice:view.metalLayer.device
                                        drawablePixelFormat:view.metalLayer.pixelFormat
                                                      model:_model];
    
    _window.contentView = view;
    
    
    {
        NSURL* url = [[NSBundle mainBundle]
                      URLForResource:@"Keyboard-Button-Click-07-c-FesliyanStudios.com2"
                      withExtension:@"mp3"];
        
        NSError* err = nil;
        
        AVAudioFile* file = [[AVAudioFile alloc]
                             initForReading:url error:&err];
        
        if (err)
            NSLog(@"%@", [err localizedDescription]);
        
        _audio_buffer = [[AVAudioPCMBuffer alloc]
                         initWithPCMFormat:file.processingFormat
                         frameCapacity:(int) file.length];
        
        [file readIntoBuffer:_audio_buffer error:&err];
        
        url = [[NSBundle mainBundle]
               URLForResource:@"mixkit-typewriter-classic-return-1381"
               withExtension:@"wav"];
        file = [[AVAudioFile alloc]
                initForReading:url error:&err];
        if (err)
            NSLog(@"%@", [err localizedDescription]);
        _audio_buffer2 = [[AVAudioPCMBuffer alloc]
                          initWithPCMFormat:file.processingFormat
                          frameCapacity:(int) file.length];
        [file readIntoBuffer:_audio_buffer2 error:&err];
        
        /*
         {
         auto* p = _audio_buffer.floatChannelData[0];
         auto n = _audio_buffer.frameLength;
         for (int i = 0; i != n; ++i) {
         if (p[i]) {
         printf("%d\n", i);
         break;
         }
         }
         }
         */
        
        if (err)
            NSLog(@"%@", [err localizedDescription]);
        
        
        
        _audio_engine = [[AVAudioEngine alloc] init];
        
        if (err)
            NSLog(@"%@", [err localizedDescription]);
        
        _audio_environment = [[AVAudioEnvironmentNode alloc] init];
        [_audio_engine attachNode:_audio_environment];
        [_audio_engine connect:_audio_environment
                            to:_audio_engine.mainMixerNode format:nil];
        
        _audio_players = [[NSMutableArray<AVAudioPlayerNode*> alloc] init];
        
    }
    
    
    
    /*
     NSError* e = nil;
     _audio_player = [[AVAudioPlayer alloc]
     initWithContentsOfURL:u
     error:&e];
     if (e) {
     NSLog(@"%@", [e localizedDescription]);
     }
     //[_audio_player play];
     [_audio_player prepareToPlay];
     */
    
    
    
    
    
    
    //AVAudioPlayerNode* player = [[AVAudioPlayerNode alloc] init];
    
    view.nextResponder = self;

    
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationWillBecomeActive:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_window makeKeyAndOrderFront:nil];
}

- (void)applicationWillResignActive:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)applicationDidResignActive:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return NSTerminateNow;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark NSWindowDelegate

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return frameSize;
}

- (void)windowDidResize:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)windowDidChangeScreen:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
}

#pragma mark ClientViewDelegate

- (void)drawableResize:(CGSize)size
{
    NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_renderer drawableResize:size];
}

- (void)renderToMetalLayer:(nonnull CAMetalLayer *)layer
{
    // NSLog(@"%s\n", __PRETTY_FUNCTION__);
    [_renderer renderToMetalLayer:layer];
}

#pragma mark NSResponder

// User interaction

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)keyDown:(NSEvent *)event {
    
    if (!event.ARepeat) {
        
    }
    
    if (event.characters.length) {
        // play keydown sound
        
        AVAudioPlayerNode* player = nil;
        
        // try_pop an existing unused player
        @synchronized (_audio_players) {
            if (_audio_players.count) {
                player = _audio_players.lastObject;
                [_audio_players removeLastObject];
            }
        }
        
        if (!player) {
            // set up a new player
            player = [[AVAudioPlayerNode alloc] init];
            [_audio_engine attachNode:player];
            [_audio_engine connect:player
             // to:_audio_engine.mainMixerNode
                                to:_audio_environment
                            format:_audio_buffer.format];
            NSError* err = nil;
            [_audio_engine startAndReturnError:&err];
            if (err)
                NSLog(@"%@", [err localizedDescription]);
            
            //player.renderingAlgorithm = AVAudio3DMixingRenderingAlgorithmAuto;
            player.sourceMode = AVAudio3DMixingSourceModePointSource;
            
            [player play];
            
        }
        
        // put the player in space somewhere
        player.position = AVAudioMake3DPoint(rand() & 1 ? -1.0 : +1.0,
                                             rand() & 1 ? -1.0 : +1.0,
                                             rand() & 1 ? -1.0 : +1.0);
        
        // schedule the waveform on the player
        [player scheduleBuffer:
         (([event.characters characterAtIndex:0] == NSCarriageReturnCharacter)
          ? _audio_buffer2 : _audio_buffer)
             completionHandler:^{
            // "don't stop the player in the handler, it may deadlock"
            // when playback completes, put the player back in the stack
            @synchronized (self->_audio_players) {
                [self->_audio_players addObject:player];
            }
        }];
    }
    
    // UTF-16 code for key, such as private use 0xf700 = NSUpArrowFunctionKey
    // printf("%x\n", [event.characters characterAtIndex:0]);
    
    // _model->_console.back().append(event.characters.UTF8String);
    NSLog(@"keyDown: \"%@\"\n", event.characters);
    if (event.characters.length) {
        NSLog(@"keyDown: (%x)\n", [event.characters characterAtIndex:0]);
        auto guard = std::unique_lock{_model->_mutex};
        switch ([event.characters characterAtIndex:0]) {
            case NSCarriageReturnCharacter:
                _model->_console.emplace_back();
                break;
            case NSDeleteCharacter:
                if (!_model->_console.back().empty())
                    _model->_console.back().pop_back();
                break;
            case NSUpArrowFunctionKey:
                std::rotate(_model->_console.begin(), _model->_console.end() - 1, _model->_console.end());
                break;
            case NSDownArrowFunctionKey:
                std::rotate(_model->_console.begin(), _model->_console.begin() + 1, _model->_console.end());
                break;
            case NSLeftArrowFunctionKey:
                if (!_model->_console.back().empty()) {
                    auto ch = _model->_console.back().pop_back();
                    _model->_console.back().push_front(ch);
                }
                break;
            case NSRightArrowFunctionKey:
                if (!_model->_console.back().empty()) {
                    auto ch = _model->_console.back().pop_front();
                    _model->_console.back().push_back(ch);
                }
                break;
            default:
                _model->_console.back().append(event.characters.UTF8String);
                break;
        }
    }
}


- (void)keyUp:(NSEvent *)event {
    // NSLog(@"keyUp: \"%@\"\n", event.characters);
}

- (void) mouseMoved:(NSEvent *)event {}
- (void) mouseEntered:(NSEvent *)event {}
- (void) mouseExited:(NSEvent *)event {}
- (void) mouseDown:(NSEvent *)event {}
- (void) mouseDragged:(NSEvent *)event {}
- (void) mouseUp:(NSEvent *)event {}
- (void) rightMouseDown:(NSEvent *)event {}
- (void) rightMouseDragged:(NSEvent *)event {}
- (void) rightMouseUp:(NSEvent *)event {}
- (void) otherMouseDown:(NSEvent *)event {}
- (void) otherMouseDragged:(NSEvent *)event {}
- (void) otherMouseUp:(NSEvent *)event {}


@end
