#pragma once

#define STRINGIFY(X) STRINGIFY2(X)    
#define STRINGIFY2(X) #X

#define CAT(X,Y) CAT2(X,Y)
#define CAT2(X,Y) X##Y
#define CAT_2 CAT

#define INCLUDE_APPLICATION(X) STRINGIFY(CAT_2(X,.h))
