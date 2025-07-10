#include "Path.h"

#include "config.h"

#import <Foundation/Foundation.h>

fs::path getContentPath(tstring file, bool isSystem) {
    NSArray* pPaths;
  
    if (isSystem)
        pPaths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSSystemDomainMask, YES);
    else
        pPaths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);

    NSString *pApplicationSupportDirectory = [pPaths objectAtIndex:0];

    return tstring([pApplicationSupportDirectory UTF8String]) + "/RetroPlug/" + file;
}
