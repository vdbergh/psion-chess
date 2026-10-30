/*
 *  Copyright (C) 2002-2014  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#include "dosbox.h"
#include "video.h"
#include "mapper.h"
#include "control.h"
#include "psion.h"
#include "version.h"

// Stubs for what the SDL front end (sdl_main.cpp, sdl_mapper.cpp) provides.

void MAPPER_AddHandler(MAPPER_Handler *handler, MapKeys key, Bitu mods,
                       char const *const eventname,
                       char const *const buttonname) {}

struct State_Block {
    bool active; // If this isn't set don't draw
    bool updating;
    unsigned char pixels[4 * 640 * 400];
};

bool JOYSTICK_IsEnabled(Bitu which) {
    return false;
}

float JOYSTICK_GetMove_X(Bitu which) {
    return 0.0;
}

float JOYSTICK_GetMove_Y(Bitu which) {
    return 0.0;
}

static State_Block state;

// Globals for keyboard initialisation
bool startup_state_numlock = false;
bool startup_state_capslock = false;

void GFX_SetTitle(Bit32s cycles, Bits frameskip, bool paused) {}

Bitu GFX_GetBestMode(Bitu flags) {
    return flags;
}

void GFX_ResetScreen(void) {}

Bitu GFX_SetSize(Bitu width, Bitu height, Bitu flags, double scalex,
                 double scaley, GFX_CallBack_t callback) {
    if (state.updating)
        GFX_EndUpdate(0);
    GFX_Start();
    return GFX_CAN_32;
}

void GFX_CaptureMouse(void) {}

bool mouselocked; // Global variable for mapper

bool GFX_StartUpdate(Bit8u *&pixels, Bitu &pitch) {
    if (!state.active || state.updating)
        return false;
    pixels = (Bit8u *)state.pixels;
    pitch = 640 * 4;
    state.updating = true;
    return true;
}

void GFX_EndUpdate(const Bit16u *changedLines) {
    if (!state.updating)
        return;
    state.updating = false;
    psion::ProcessScreen(state.pixels);
}

void GFX_SetPalette(Bitu start, Bitu count, GFX_PalEntry *entries) {}

Bitu GFX_GetRGB(Bit8u red, Bit8u green, Bit8u blue) {
    return red | (green << 8) | (blue << 16);
}

void GFX_Stop() {
    if (state.updating)
        GFX_EndUpdate(0);
    state.active = false;
}

void GFX_Start() {
    state.active = true;
}

void Mouse_AutoLock(bool enable) {}

void GFX_LosingFocus(void) {}

void GFX_Events() {
    psion::ProcessConsole();
}

void GFX_ShowMsg(char const *format, ...) {
    // DOSBox's messages are diagnostics too: only with --verbose.
    static std::string pending;
    if (!psion::Verbose()) {
        return;
    }
    char buf[512];
    va_list msg;
    va_start(msg, format);
    vsnprintf(buf, sizeof(buf) - 1, format, msg);
    va_end(msg);
    strcat(buf, "\n");
    psion::WriteLines(stderr, pending, buf);
}

void restart_program(std::vector<std::string> &parameters) {}

static void usage(FILE *f) {
    fprintf(f, "usage: psion [options]\n"
               "\n"
               "  -V, --verbose   write diagnostics to stderr\n"
               "  -c, --cycles N  run the emulated PC at N instructions per ms\n"
               "  -v, --version   print the version and exit\n"
               "  -h, --help      print this help and exit\n");
}

int main(int argc, char *argv[]) {
    // DOSBox's own command line parsing ignores case, so -v and -V would be
    // the same: the options are parsed here, and DOSBox gets none.
    int cycles = 0;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);
        if (arg == "-h" || arg == "--help") {
            usage(stdout);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            printf("Psion Chess UCI adapter %s\n", PSION_VERSION);
            return 0;
        } else if (arg == "-V" || arg == "--verbose") {
            psion::SetVerbose(true);
        } else if (arg == "-c" || arg == "--cycles") {
            // For tests: run the emulated PC at a fixed speed, in
            // instructions per millisecond, instead of as fast as possible.
            char *end;
            long n = i + 1 < argc ? strtol(argv[++i], &end, 10) : 0;
            if (n <= 0 || *end || n > 1000000) {
                fprintf(stderr, "psion: %s needs a number of cycles from 1 "
                                "to 1000000\n", arg.c_str());
                return 2;
            }
            cycles = (int)n;
        } else {
            fprintf(stderr, "psion: invalid option: %s\n\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    CommandLine com_line(1, argv);
    Config myconf(&com_line);
    control = &myconf;
    DOSBOX_Init();
    if (cycles) {
        char line[32];
        snprintf(line, sizeof(line), "cycles=%d", cycles);
        control->GetSection("dosbox")->HandleInputline(line);
    }
    psion::IO_Init();
    LOG_MSG("DOSBox version %s", VERSION);
    LOG_MSG("Copyright 2002-2013 DOSBox Team, published under GNU GPL.");
    LOG_MSG("Psion Chess (1985)");
    LOG_MSG("Copyright Richard Lang.");
    control->Init();
    control->StartUp();
    return 0;
}
