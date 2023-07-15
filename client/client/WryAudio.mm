//
//  WryAudio.mm
//  client
//
//  Created by Antony Searle on 15/7/2023.
//

#include "WryAudio.h"

@implementation WryAudio
{
    AVAudioEngine* _audio_engine;
    AVAudioEnvironmentNode* _audio_environment;
    NSMutableArray<AVAudioPlayerNode*>* _audio_players;
    
    NSMutableDictionary<NSString*, AVAudioPCMBuffer*>* _audio_buffers;
}

- (void)loadResource:(NSString*)name withExtension:(NSString*)ext {
    NSError* err = nil;
    NSURL* url = [[NSBundle mainBundle]
                  URLForResource:name
                  withExtension:ext];
    AVAudioFile* file = [[AVAudioFile alloc]
                         initForReading:url
                         error:&err];
    if (err) {
        NSLog(@"%@", [err localizedDescription]);
        return;
    }
    AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
                                initWithPCMFormat:file.processingFormat
                                frameCapacity:(int) file.length];
    [file readIntoBuffer:buffer error:&err];
    if (err) {
        NSLog(@"%@", [err localizedDescription]);
        return;
    }
    [_audio_buffers setObject:buffer forKey:name];
}

- (nonnull instancetype)init {
    if ((self = [super init])) {
        _audio_engine = [[AVAudioEngine alloc] init];
        _audio_environment = [[AVAudioEnvironmentNode alloc] init];
        [_audio_engine attachNode:_audio_environment];
        [_audio_engine connect:_audio_environment
                            to:_audio_engine.mainMixerNode format:nil];
        _audio_players = [[NSMutableArray<AVAudioPlayerNode*> alloc] init];
        _audio_buffers = [[NSMutableDictionary<NSString*, AVAudioPCMBuffer*> alloc] init];
        [self loadResource:@"Keyboard-Button-Click-07-c-FesliyanStudios.com2" withExtension:@"mp3"];
        [self loadResource:@"mixkit-typewriter-classic-return-1381" withExtension:@"wav"];
    }
    return self;
}

- (void)play:(NSString *)name at:(AVAudio3DPoint)location {
    
    // get the buffer
    AVAudioPCMBuffer* buffer = [_audio_buffers objectForKey:name];
    if (!buffer) {
        NSLog(@"AudioPCMBuffer \"%@\" not found\n", name);
        return;
    }
    
    AVAudioPlayerNode* player = nil;
    // try_pop an existing unused player
    @synchronized (_audio_players) {
        if (_audio_players.count) {
            player = _audio_players.lastObject;
            [_audio_players removeLastObject];
        }
    }
    if (!player) {
        player = [[AVAudioPlayerNode alloc] init];
        [_audio_engine attachNode:player];
        [_audio_engine connect:player
                            to:_audio_environment
                        format:buffer.format]; // <-- dodgy, is this same for all samples?
        NSError* err = nil;
        [_audio_engine startAndReturnError:&err]; // <-- lazy start
        if (err) {
            NSLog(@"%@", [err localizedDescription]);
            return;
        }
        player.sourceMode = AVAudio3DMixingSourceModePointSource;
        [player play];
    }
    
    // put the audio player in space somewhere
    // coordiante system seems to be
    // +X : right
    // +Y : up?
    // +Z : forward?
    // coordinates are in meters
    player.position = location;
    
    // schedule the waveform on the player
    [player scheduleBuffer:buffer
         completionHandler:^{
        // "don't stop the player in the handler, it may deadlock"
        // when playback completes, put the player back in the stack
        @synchronized (self->_audio_players) {
            [self->_audio_players addObject:player];
        }
    }];
}

@end
