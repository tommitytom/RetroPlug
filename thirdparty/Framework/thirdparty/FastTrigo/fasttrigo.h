/* FastTrigo 1.0 (c) 2013 Robin Lobel

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
*/

#ifndef FASTTRIGO_H
#define FASTTRIGO_H

#include <math.h>
#ifdef QT_GUI_LIB
#include <QtGui>
#endif
#include <intrin.h>
#include <xmmintrin.h>
#include <pmmintrin.h>

//Default accuracy
namespace FT
{
    float sqrt(float squared);
    float length(float x, float y);
    float length(float x, float y, float z);
    float atan2(float y, float x);
    float cos(float angle);
    float sin(float angle);
    void  sincos(float angle, float *sin, float *cos);

    __m128 sqrt_ps(__m128 squared);
    __m128 length_ps(__m128 x, __m128 y);
    __m128 length_ps(__m128 x, __m128 y, __m128 z);
    __m128 atan2_ps(__m128 y, __m128 x);
    __m128 cos_ps(__m128 angle);
    __m128 sin_ps(__m128 angle);
    void   sincos_ps(__m128 angle, __m128 *sin, __m128 *cos);
    void   interleave_ps(__m128 x0x1x2x3, __m128 y0y1y2y3, __m128 *x0y0x1y1, __m128 *x2y2x3y3);
    void   deinterleave_ps(__m128 x0y0x1y1, __m128 x2y2x3y3, __m128 *x0x1x2x3, __m128 *y0y1y2y3);

#ifdef QT_GUI_LIB
    float length(QVector2D vector); //cartesian vector(x,y)
    float length(QVector3D vector); //cartesian vector(x,y,z)
    float angle(QVector2D vector); //cartesian vector(x,y)
    float azimuth(QVector3D vector); //cartesian vector(x,y,z)
    float inclination(QVector3D vector); //cartesian vector(x,y,z)
    float x(QVector2D vector); //polar vector(length,angle)
    float y(QVector2D vector); //polar vector(length,angle)
    float x(QVector3D vector); //spherical vector(length,azimuth,inclination)
    float y(QVector3D vector); //spherical vector(length,azimuth,inclination)
    float z(QVector3D vector); //spherical vector(length,azimuth,inclination)
    QVector2D cartesian2polar(QVector2D vector);
    QVector2D polar2cartesian(QVector2D vector);
    QVector3D cartesian2spherical(QVector3D vector);
    QVector3D spherical2cartesian(QVector3D vector);
#endif
};

//More accurate
namespace FTA
{
    float sqrt(float squared);
    float length(float x, float y);
    float length(float x, float y, float z);
    float atan2(float y, float x);
    float cos(float angle);
    float sin(float angle);
    void  sincos(float angle, float *sin, float *cos);

    __m128 sqrt_ps(__m128 squared);
    __m128 length_ps(__m128 x, __m128 y);
    __m128 length_ps(__m128 x, __m128 y, __m128 z);
    __m128 atan2_ps(__m128 y, __m128 x);
    __m128 cos_ps(__m128 angle);
    __m128 sin_ps(__m128 angle);
    void   sincos_ps(__m128 angle, __m128 *sin, __m128 *cos);
    void   interleave_ps(__m128 x0x1x2x3, __m128 y0y1y2y3, __m128 *x0y0x1y1, __m128 *x2y2x3y3);
    void   deinterleave_ps(__m128 x0y0x1y1, __m128 x2y2x3y3, __m128 *x0x1x2x3, __m128 *y0y1y2y3);

#ifdef QT_GUI_LIB
    float length(QVector2D vector); //cartesian vector(x,y)
    float length(QVector3D vector); //cartesian vector(x,y,z)
    float angle(QVector2D vector); //cartesian vector(x,y)
    float azimuth(QVector3D vector); //cartesian vector(x,y,z)
    float inclination(QVector3D vector); //cartesian vector(x,y,z)
    float x(QVector2D vector); //polar vector(length,angle)
    float y(QVector2D vector); //polar vector(length,angle)
    float x(QVector3D vector); //spherical vector(length,azimuth,inclination)
    float y(QVector3D vector); //spherical vector(length,azimuth,inclination)
    float z(QVector3D vector); //spherical vector(length,azimuth,inclination)
    QVector2D cartesian2polar(QVector2D vector);
    QVector2D polar2cartesian(QVector2D vector);
    QVector3D cartesian2spherical(QVector3D vector);
    QVector3D spherical2cartesian(QVector3D vector);
#endif
};

#endif // FASTTRIGO_H
