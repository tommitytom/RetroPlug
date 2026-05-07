FastTrigo 1.0 (c) 2013 Robin Lobel
=========
Fast yet accurate trigonometric functions

Each namespace (FT, FTA) has 3 sets of functions:

    Scalar: standard trigonometric functions
    Packed Scalar: same functions computing 4 values at the same time (using SSE/SSE2/SSE3/SSE4.1 if available)
    Qt: convenience functions if using QVector2D/QVector3D classes from Qt
  
FT Accuracy:

    FT::sqrt/sqrt_ps max error: 0.032% (average error: 0.0094%)
    FT::atan2/atan2_ps max error: 0.024% (0.0015 radians, 0.086 degrees)
    FT::cos/cos_ps max error: 0.06%
    FT::sin/sin_ps max error: 0.06%

FT Speed up (MSVC2012 x64):

    FT::sqrt speed up: x2.5 (from standard sqrt)
    FT::atan2 speed up: x2.3 (from standard atan2)
    FT::sin/cos speed up: x1.9 (from standard sin/cos)
    FT::sincos speed up: x2.3 (from standard sin+cos)
    FT::sqrt_ps speed up: x8 (from standard sqrt)
    FT::atan2_ps speed up: x7.3 (from standard atan2)
    FT::sin_ps/cos_ps speed up: x4.9 (from standard sin/cos)
    FT::sincos_ps speed up: x6.2 (from standard sin+cos)

FTA Accuracy:

    FTA::sqrt/sqrt_ps max error: 0%
    FTA::atan2/atan2_ps max error: 0.0005%
    FTA::cos/cos_ps max error: 0.0007%
    FTA::sin/sin_ps max error: 0.0007%

FTA Speed up (MSVC2012 x64):

    FTA::sqrt speed up: x1.5 (from standard sqrt)
    FTA::atan2 speed up: x1.7 (from standard atan2)
    FTA::sin/cos speed up: x1.6 (from standard sin/cos)
    FTA::sincos speed up: x1.8 (from standard sin+cos)
    FTA::sqrt_ps speed up: x4.9 (from standard sqrt)
    FTA::atan2_ps speed up: x5.2 (from standard atan2)
    FTA::sin_ps/cos_ps speed up: x4.3 (from standard sin/cos)
    FTA::sincos_ps speed up: x5.2 (from standard sin+cos)

Distributed under Revised BSD License
