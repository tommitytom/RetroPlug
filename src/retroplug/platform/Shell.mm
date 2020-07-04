#include "Shell.h"

void openShellFolder(const tstring& path) {
    NSString* ret = (NSString*)CFStringCreateWithCString(NULL,path.c_str(),kCFStringEncodingUTF8);
    NSURL *url = [NSURL fileURLWithPath:ret isDirectory:false];
    NSArray *fileURLs = [NSArray arrayWithObjects:url, /* ... */ nil];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:fileURLs];
}
