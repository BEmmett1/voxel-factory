#pragma once

// Single include point for OpenGL. glad must be included before any other GL
// header, so everything in the engine that touches GL includes this instead of
// <glad/gl.h> directly.
#include <glad/gl.h>
