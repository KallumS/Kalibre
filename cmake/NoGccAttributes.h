/* Forced into Shine's files on MSVC (decision 0129): GCC's __attribute__,
   which MSVC hasn't, is nothing there. MSVC's /D can't define a macro that
   takes an argument, so it's done here. */
#pragma once
#define __attribute__(x)
