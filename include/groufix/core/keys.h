/**
 * This file is part of groufix.
 * Copyright (c) Stef Velzel. All rights reserved.
 *
 * groufix : graphics engine produced by Stef Velzel.
 * www     : <www.vuzzel.nl>
 */


#ifndef GFX_CORE_KEYS_H
#define GFX_CORE_KEYS_H

#include "groufix/def.h"


/**
 * Keyboard modifiers (identical to GLFW modifiers).
 * These are bitfields and can be &'ed together.
 */
typedef enum GFXModifier
{
	GFX_MOD_NONE      = 0x0000,
	GFX_MOD_SHIFT     = 0x0001,
	GFX_MOD_CONTROL   = 0x0002,
	GFX_MOD_ALT       = 0x0004,
	GFX_MOD_SUPER     = 0x0008,
	GFX_MOD_CAPS_LOCK = 0x0010,
	GFX_MOD_NUM_LOCK  = 0x0020

} GFXModifier;

GFX_BIT_FIELD(GFXModifier)


/**
 * Keyboard keys (identical to GLFW keys).
 */
typedef enum GFXKey
{
	GFX_KEY_UNKNOWN = -1,
	GFX_KEY_WORLD_1 = 161,
	GFX_KEY_WORLD_2 = 162,

	GFX_KEY_APOSTROPHE    = 39, // '
	GFX_KEY_COMMA         = 44, // ,
	GFX_KEY_MINUS         = 45, // -
	GFX_KEY_PERIOD        = 46, // .
	GFX_KEY_SLASH         = 47, // /
	GFX_KEY_SEMICOLON     = 59, // ;
	GFX_KEY_EQUAL         = 61, // =
	GFX_KEY_LEFT_BRACKET  = 91, // [
	GFX_KEY_BACKSLASH     = 92, // \.
	GFX_KEY_RIGHT_BRACKET = 93, // ]
	GFX_KEY_GRAVE_ACCENT  = 96, // `

	GFX_KEY_SPACE        = 32,
	GFX_KEY_ESCAPE       = 256,
	GFX_KEY_ENTER        = 257,
	GFX_KEY_TAB          = 258,
	GFX_KEY_BACKSPACE    = 259,
	GFX_KEY_INSERT       = 260,
	GFX_KEY_DELETE       = 261,
	GFX_KEY_RIGHT        = 262,
	GFX_KEY_LEFT         = 263,
	GFX_KEY_DOWN         = 264,
	GFX_KEY_UP           = 265,
	GFX_KEY_PAGE_UP      = 266,
	GFX_KEY_PAGE_DOWN    = 267,
	GFX_KEY_HOME         = 268,
	GFX_KEY_END          = 269,
	GFX_KEY_PRINT_SCREEN = 283,
	GFX_KEY_PAUSE        = 284,
	GFX_KEY_MENU         = 348,

	GFX_KEY_CAPS_LOCK   = 280,
	GFX_KEY_SCROLL_LOCK = 281,
	GFX_KEY_NUM_LOCK    = 282,

	GFX_KEY_LEFT_SHIFT    = 340,
	GFX_KEY_LEFT_CONTROL  = 341,
	GFX_KEY_LEFT_ALT      = 342,
	GFX_KEY_LEFT_SUPER    = 343,
	GFX_KEY_RIGHT_SHIFT   = 344,
	GFX_KEY_RIGHT_CONTROL = 345,
	GFX_KEY_RIGHT_ALT     = 346,
	GFX_KEY_RIGHT_SUPER   = 347,

	GFX_KEY_F1  = 290,
	GFX_KEY_F2  = 291,
	GFX_KEY_F3  = 292,
	GFX_KEY_F4  = 293,
	GFX_KEY_F5  = 294,
	GFX_KEY_F6  = 295,
	GFX_KEY_F7  = 296,
	GFX_KEY_F8  = 297,
	GFX_KEY_F9  = 298,
	GFX_KEY_F10 = 299,
	GFX_KEY_F11 = 300,
	GFX_KEY_F12 = 301,
	GFX_KEY_F13 = 302,
	GFX_KEY_F14 = 303,
	GFX_KEY_F15 = 304,
	GFX_KEY_F16 = 305,
	GFX_KEY_F17 = 306,
	GFX_KEY_F18 = 307,
	GFX_KEY_F19 = 308,
	GFX_KEY_F20 = 309,
	GFX_KEY_F21 = 310,
	GFX_KEY_F22 = 311,
	GFX_KEY_F23 = 312,
	GFX_KEY_F24 = 313,
	GFX_KEY_F25 = 314,

	GFX_KEY_0 = 48,
	GFX_KEY_1 = 49,
	GFX_KEY_2 = 50,
	GFX_KEY_3 = 51,
	GFX_KEY_4 = 52,
	GFX_KEY_5 = 53,
	GFX_KEY_6 = 54,
	GFX_KEY_7 = 55,
	GFX_KEY_8 = 56,
	GFX_KEY_9 = 57,

	GFX_KEY_A = 65,
	GFX_KEY_B = 66,
	GFX_KEY_C = 67,
	GFX_KEY_D = 68,
	GFX_KEY_E = 69,
	GFX_KEY_F = 70,
	GFX_KEY_G = 71,
	GFX_KEY_H = 72,
	GFX_KEY_I = 73,
	GFX_KEY_J = 74,
	GFX_KEY_K = 75,
	GFX_KEY_L = 76,
	GFX_KEY_M = 77,
	GFX_KEY_N = 78,
	GFX_KEY_O = 79,
	GFX_KEY_P = 80,
	GFX_KEY_Q = 81,
	GFX_KEY_R = 82,
	GFX_KEY_S = 83,
	GFX_KEY_T = 84,
	GFX_KEY_U = 85,
	GFX_KEY_V = 86,
	GFX_KEY_W = 87,
	GFX_KEY_X = 88,
	GFX_KEY_Y = 89,
	GFX_KEY_Z = 90,

	GFX_KEY_KP_DECIMAL  = 330,
	GFX_KEY_KP_DIVIDE   = 331,
	GFX_KEY_KP_MULTIPLY = 332,
	GFX_KEY_KP_SUBTRACT = 333,
	GFX_KEY_KP_ADD      = 334,
	GFX_KEY_KP_ENTER    = 335,
	GFX_KEY_KP_EQUAL    = 336,

	GFX_KEY_KP_0 = 320,
	GFX_KEY_KP_1 = 321,
	GFX_KEY_KP_2 = 322,
	GFX_KEY_KP_3 = 323,
	GFX_KEY_KP_4 = 324,
	GFX_KEY_KP_5 = 325,
	GFX_KEY_KP_6 = 326,
	GFX_KEY_KP_7 = 327,
	GFX_KEY_KP_8 = 328,
	GFX_KEY_KP_9 = 329

} GFXKey;


/**
 * Mouse buttons (identical to GLFW mouse buttons).
 */
typedef enum GFXMouseButton
{
	GFX_MOUSE_1 = 0,
	GFX_MOUSE_2 = 1,
	GFX_MOUSE_3 = 2,
	GFX_MOUSE_4 = 3,
	GFX_MOUSE_5 = 4,
	GFX_MOUSE_6 = 5,
	GFX_MOUSE_7 = 6,
	GFX_MOUSE_8 = 7,

	GFX_MOUSE_LEFT   = GFX_MOUSE_1,
	GFX_MOUSE_RIGHT  = GFX_MOUSE_2,
	GFX_MOUSE_MIDDLE = GFX_MOUSE_3

} GFXMouseButton;


#endif
