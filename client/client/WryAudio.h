//
//  WryAudio.h
//  client
//
//  Created by Antony Searle on 15/7/2023.
//

#ifndef WryAudio_h
#define WryAudio_h

#include <AVFoundation/AVFoundation.h>

@interface WryAudio : NSObject

- (void) play:(NSString*)name at:(AVAudio3DPoint)location;

@end



#endif /* WryAudio_h */
