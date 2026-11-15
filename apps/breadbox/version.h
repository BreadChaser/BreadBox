// BreadBox release version. The CIA title version derives from it (tools/build/title-version.py: 0.9.2 => 25647);
// private test CIAs pass an explicit VERSION=N counter instead.
#define BB_VERSION "0.9.2"
#ifndef BB_BUILD
// Older build scripts still pass the retired -DPSX3D_BUILD=...; accept it.
#ifdef PSX3D_BUILD
#define BB_BUILD PSX3D_BUILD
#else
#ifndef BB_SOURCE_REV
#define BB_SOURCE_REV "unversioned"
#endif
#define BB_BUILD "breadbox-" BB_VERSION "-" BB_SOURCE_REV
#endif
#endif
// Temporary alias for files still owned by the room/pause lines (main.c, home.inc
// and their branches). Delete it when those land and use BB_VERSION.
#define PSX3D_VERSION BB_VERSION
