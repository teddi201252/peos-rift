//========================================================================
// GLFW on PeOS: the Null platform, with its window in Starlight
//------------------------------------------------------------------------
// Copyright (c) 2026 PeOS
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software. Permission is granted to anyone
// to use this software for any purpose, including commercial applications,
// and to alter it and redistribute it freely (the zlib license, as GLFW).
//========================================================================
// Rift Ascend port, phase C (docs/rift-ascend-port-plan.md). GLFW keeps its
// Null platform (windows, input state, monitors); on PeOS (__peos__) that
// platform gets its one window from Starlight through SLUI: the events
// (keys, characters, the pointer, the size) come in through
// _glfwPeOSPump, the OpenGL context is peos_gl's (Mesa on the Intel GPU),
// and each swap copies the frame into the window's pixels.

#include <peos_gl.h>

typedef struct _GLFWcontextPeOS
{
    peos_gl_context* handle;
    int              width;
    int              height;
    int              interval;       // glfwSwapInterval: frames of 1/60 s between swaps
    uint64_t         lastSwapNs;
} _GLFWcontextPeOS;

#define GLFW_NULL_CONTEXT_STATE _GLFWcontextPeOS peos;

GLFWbool _glfwCreateContextPeOS(_GLFWwindow* window,
                                const _GLFWctxconfig* ctxconfig,
                                const _GLFWfbconfig* fbconfig);
// The window's size from Starlight (waits for it, and for the pixels).
void _glfwPeOSWindowCreated(_GLFWwindow* window);
void _glfwPeOSWindowDestroyed(_GLFWwindow* window);
// Starlight's events into GLFW: 0 does not wait, < 0 waits for one, else
// up to timeout seconds.
void _glfwPeOSPump(double timeout);
void _glfwPeOSSetCursorMode(_GLFWwindow* window, int mode);
// A cursor made from an image is drawn into the frame at each swap (Starlight
// shows none over the pixels then); a standard one is Starlight's arrow.
void _glfwPeOSCreateCursor(_GLFWcursor* cursor, const GLFWimage* image, int xhot, int yhot);
void _glfwPeOSDestroyCursor(_GLFWcursor* cursor);
void _glfwPeOSSetCursor(_GLFWwindow* window, _GLFWcursor* cursor);
void _glfwPeOSSetClipboardString(const char* string);
void _glfwPeOSGetClipboardString(void);
