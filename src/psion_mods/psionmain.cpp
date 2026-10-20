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

// Some stuff from sdl_mapper.cpp 

void MAPPER_AddHandler(MAPPER_Handler * handler,MapKeys key,Bitu mods,char const * const eventname,char const * const buttonname) {
}

bool autofire = false;

struct State_Block {
    bool active;			//If this isn't set don't draw
    bool updating;
    unsigned char pixels[4*640*400];
};

bool JOYSTICK_IsEnabled(Bitu which){
    return false;
}

float JOYSTICK_GetMove_X(Bitu which){
    return 0.0;
}

float JOYSTICK_GetMove_Y(Bitu which){
    return 0.0;
}


static State_Block state;

extern const char* RunningProgram;
extern bool CPU_CycleAutoAdjust;
//Globals for keyboard initialisation
bool startup_state_numlock=false;
bool startup_state_capslock=false;

void GFX_SetTitle(Bit32s cycles,Bits frameskip,bool paused){
}

Bitu GFX_GetBestMode(Bitu flags) {
    return flags;
}

void GFX_ResetScreen(void) {
}

Bitu GFX_SetSize(Bitu width,Bitu height,Bitu flags,double scalex,double scaley,GFX_CallBack_t callback) {
    if (state.updating)
	GFX_EndUpdate( 0 );
    GFX_Start();
    return GFX_CAN_32;
}

void GFX_CaptureMouse(void) {
}

bool mouselocked; //Global variable for mapper


bool GFX_StartUpdate(Bit8u * & pixels,Bitu & pitch) {
	if (!state.active || state.updating)
		return false;
	pixels=(Bit8u *)state.pixels;
	pitch=640*4;
	state.updating=true;
	return true;
}

void GFX_EndUpdate( const Bit16u *changedLines ) {
    static unsigned int last_tickcount=0,new_tickcount=0;
    if (!state.updating)
	return;
    state.updating=false;
    /******/
    psion::ProcessScreen(state.pixels);

}

void GFX_SetPalette(Bitu start,Bitu count,GFX_PalEntry * entries) {
}

Bitu GFX_GetRGB(Bit8u red,Bit8u green,Bit8u blue) {
    return red|(green<<8)|(blue<<16);
}

void GFX_Stop() {
	if (state.updating)
		GFX_EndUpdate( 0 );
	state.active=false;
}

void GFX_Start() {
	state.active=true;
}

void Mouse_AutoLock(bool enable) {
}

void GFX_LosingFocus(void) {
}

void GFX_Events() {
    /****/
    psion::ProcessConsole();
    /****/
}

void GFX_ShowMsg(char const* format,...) {
	// DOSBox's messages are diagnostics too: only with --verbose.
	static std::string pending;
	if(!psion::Verbose()){
	    return;
	}
	char buf[512];
	va_list msg;
	va_start(msg,format);
	vsnprintf(buf,sizeof(buf)-1,format,msg);
	va_end(msg);
	strcat(buf,"\n");
	psion::WriteLines(stderr,pending,buf);
}

void restart_program(std::vector<std::string> & parameters) {
}

//extern void UI_Init(void);
int main(int argc, char* argv[]) {
    CommandLine com_line(argc,argv);
    if(com_line.FindExist("--verbose",true) || com_line.FindExist("-v",true)){
	psion::SetVerbose(true);
    }
    Config myconf(&com_line);
    control=&myconf;
    DOSBOX_Init();
    /****/
    psion::IO_Init();
    /****/
    if (control->cmdline->FindExist("-version") ||
	control->cmdline->FindExist("--version") ) {
	printf("\nDOSBox version %s, copyright 2002-2013 DOSBox Team.\n\n",VERSION);
	printf("DOSBox is written by the DOSBox Team (See AUTHORS file))\n");
	printf("DOSBox comes with ABSOLUTELY NO WARRANTY.  This is free software,\n");
	printf("and you are welcome to redistribute it under certain conditions;\n");
	printf("please read the COPYING file thoroughly before doing so.\n\n");
	return 0;
    }
    LOG_MSG("DOSBox version %s",VERSION);
    LOG_MSG("Copyright 2002-2013 DOSBox Team, published under GNU GPL.");
    LOG_MSG("Psion Chess (1985)");
    LOG_MSG("Copyright Richard Lang.");
    control->Init();
    control->StartUp();
    return 0;
}

