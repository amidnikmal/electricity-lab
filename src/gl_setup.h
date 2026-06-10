#pragma once

#define GL_SILENCE_DEPRECATION

#ifdef _WIN32
// GL/gl.h on Windows needs the WINGDIAPI/APIENTRY macros from windows.h.
// glcorearb.h is not shipped with the Windows SDK / MinGW; the app only uses
// GL 1.1 entry points (glViewport/glClearColor/glClear), so GL/gl.h suffices.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#include <GL/glcorearb.h>
#endif
