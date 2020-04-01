/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2003 John Fitzgibbons and others

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net 
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full
	

*/


int			glx, gly, glwidth, glheight;

float		scr_con_current;
float		scr_conlines;		// lines of console to display

float		oldscreensize, oldfov, oldsbarscale; //johnfitz -- added oldsbarscale

cvar_t		scr_menuscale = {"scr_menuscale", "1", true}; //johnfitz
cvar_t		scr_sbarscale = {"scr_sbarscale", "1", true}; //johnfitz
cvar_t		scr_conwidth = {"scr_conwidth", "0", true}; //johnfitz
cvar_t		scr_showfps = {"scr_showfps", "0"}; //johnfitz
cvar_t		scr_clock = {"scr_clock", "0"}; //johnfitz

cvar_t		scr_viewsize = {"viewsize","100", true};
cvar_t		scr_fov = {"fov","90"};	// 10 - 170
cvar_t		scr_conspeed = {"scr_conspeed","300"};
cvar_t		scr_centertime = {"scr_centertime","2"};
cvar_t		scr_showram = {"showram","1"};
cvar_t		scr_showturtle = {"showturtle","0"};
cvar_t		scr_showpause = {"showpause","1"};
cvar_t		scr_printspeed = {"scr_printspeed","8"};
cvar_t		gl_triplebuffer = {"gl_triplebuffer", "1", true };

extern	cvar_t	crosshair;

qboolean	scr_initialized;		// ready to draw

qpic_t		*scr_ram;
qpic_t		*scr_net;
qpic_t		*scr_turtle;

int			clearconsole;
int			clearnotify;

int			sb_lines;

viddef_t	vid;				// global video state

vrect_t		scr_vrect;

qboolean	scr_disabled_for_loading;
qboolean	scr_drawloading;
float		scr_disabled_time;

qboolean	block_drawing;

int	scr_tileclear_updates = 0; //johnfitz

void SCR_ScreenShot_f (void);

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
int			scr_center_lines;
int			scr_erase_lines;
int			scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str) //update centerprint data
{
	strncpy (scr_centerstring, str, sizeof(scr_centerstring)-1);
	scr_centertime_off = scr_centertime.value;
	scr_centertime_start = cl.time;

// count the number of lines for centering
	scr_center_lines = 1;
	while (*str)
	{
		if (*str == '\n')
			scr_center_lines++;
		str++;
	}
}

void SCR_DrawCenterString (void) //actually do the drawing
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;
	
	GL_SetCanvas (CANVAS_MENU); //johnfitz

// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = 200*0.35;	//johnfitz -- 320x200 coordinate system
	else
		y = 48;

	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l*8)/2;	//johnfitz -- 320x200 coordinate system
		for (j=0 ; j<l ; j++, x+=8)
		{
			Draw_Character (x, y, start[j]);	//johnfitz -- stretch overlays
			if (!remaining--)
				return;
		}
			
		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

void SCR_CheckDrawCenterString (void)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	scr_centertime_off -= host_frametime;
	
	if (scr_centertime_off <= 0 && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
	if (cl.paused) //johnfitz -- don't show centerprint during a pause
		return;

	SCR_DrawCenterString ();
}

//=============================================================================

/*
====================
CalcFovy
====================
*/
float CalcFovy (float fov_x, float width, float height)
{
        float   a;
        float   x;

        if (fov_x < 1 || fov_x > 179)
                Sys_Error ("Bad fov: %f", fov_x);

        x = width/tan(fov_x/360*M_PI);

        a = atan (height/x);

        a = a*360/M_PI;

        return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void SCR_CalcRefdef (void)
{
	vrect_t		vrect;
	float		size, scale; //johnfitz -- scale
	int			h;

	vid.recalc_refdef = 0;

// force the status bar to redraw
	Sbar_Changed ();

	scr_tileclear_updates = 0; //johnfitz
	
// bound viewsize
	if (scr_viewsize.value < 30)
		Cvar_Set ("viewsize","30");
	if (scr_viewsize.value > 120)
		Cvar_Set ("viewsize","120");

// bound field of view
	if (scr_fov.value < 10)
		Cvar_Set ("fov","10");
	if (scr_fov.value > 170)
		Cvar_Set ("fov","170");

	//johnfitz -- rewrote this section
	size = scr_viewsize.value;
	scale = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);

	if (size >= 120 || cl.intermission) //intermission is always full screen
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24 * scale;
	else
		sb_lines = 48 * scale;

	size = min(scr_viewsize.value, 100) / 100;
	//johnfitz

	//johnfitz -- rewrote this section
	r_refdef.vrect.width = max(glwidth * size, 96); //no smaller than 96 (why??)
	r_refdef.vrect.height = min(glheight * size, glheight - sb_lines); //make room for sbar
	r_refdef.vrect.x = (glwidth - r_refdef.vrect.width)/2;
	r_refdef.vrect.y = (glheight - sb_lines - r_refdef.vrect.height)/2;
	//johnfitz

	r_refdef.fov_x = scr_fov.value;
	r_refdef.fov_y = CalcFovy (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

	scr_vrect = r_refdef.vrect;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize.value+10);
	vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize.value-10);
	vid.recalc_refdef = 1;
}

/*
==================
SCR_Conwidth_f -- johnfitz -- called when scr_conwidth changes
==================
*/
void SCR_Conwidth_f (void)
{
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : glwidth;
	vid.conwidth = CLAMP (320, vid.conwidth, glwidth);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * glheight / glwidth;
}

//============================================================================

/*
==================
SCR_LoadPics -- johnfitz
==================
*/
void SCR_LoadPics (void)
{
	scr_ram = Draw_PicFromWad ("ram");
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");
}
/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	//johnfitz -- new cvars
	Cvar_RegisterVariable (&scr_menuscale, NULL);
	Cvar_RegisterVariable (&scr_sbarscale, NULL);
	Cvar_RegisterVariable (&scr_conwidth, &SCR_Conwidth_f);
	Cvar_RegisterVariable (&scr_showfps, NULL);
	Cvar_RegisterVariable (&scr_clock, NULL);
	//johnfitz

	Cvar_RegisterVariable (&scr_fov, NULL);
	Cvar_RegisterVariable (&scr_viewsize, NULL);
	Cvar_RegisterVariable (&scr_conspeed, NULL);
	Cvar_RegisterVariable (&scr_showram, NULL);
	Cvar_RegisterVariable (&scr_showturtle, NULL);
	Cvar_RegisterVariable (&scr_showpause, NULL);
	Cvar_RegisterVariable (&scr_centertime, NULL);
	Cvar_RegisterVariable (&scr_printspeed, NULL);
	Cvar_RegisterVariable (&gl_triplebuffer, NULL);

	Cmd_AddCommand ("screenshot",SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);

	SCR_LoadPics (); //johnfitz

	scr_initialized = true;
}

//============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
void SCR_DrawFPS (void)
{
	static char		str[10];
	static double	frametimes[8];
	float fps;
	int y;
	
	frametimes[r_framecount%8] = host_frametime;

	if (r_framecount%8 == 0)
	{
		fps = frametimes[0] + frametimes[1] + frametimes[2] + frametimes[3] + 
			  frametimes[4] + frametimes[5] + frametimes[6] + frametimes[7];
		fps = 8.0 / fps;
		sprintf (str, "%3.0f FPS", fps);
	}

	if (!scr_showfps.value || r_framecount < 8)
		return;

	y = glheight - sb_lines - 8;

	if (scr_clock.value)
		y -= 8; //make room for clock

	Draw_String (glwidth - (strlen(str)<<3), y, str);

	scr_tileclear_updates = 0;
}

/*
==============
SCR_DrawClock -- johnfitz
==============
*/
void SCR_DrawClock (void)
{
	char	str[9];

	if (scr_clock.value == 1)
	{
		int minutes, seconds;

		minutes = cl.time / 60;
		seconds = ((int)cl.time)%60;

		sprintf (str,"%i:%i%i", minutes, seconds/10, seconds%10);
	}
#ifdef _WIN32
	else if (scr_clock.value == 2)
	{
		int hours, minutes, seconds;
		SYSTEMTIME systime;
		char m[3] = "AM";

		GetLocalTime(&systime);
		hours = systime.wHour;
		minutes = systime.wMinute;
		seconds = systime.wSecond;

		if (hours > 12)
		{
			hours -= 12;
			strcpy(m, "PM");
		}
		else if (hours == 0)
			hours = 12;

		sprintf (str,"%i:%i%i:%i%i %s", hours%12, minutes/10, minutes%10, seconds/10, seconds%10, m);
	}
	else if (scr_clock.value == 3)
	{
		int hours, minutes, seconds;
		SYSTEMTIME systime;

		GetLocalTime(&systime);
		hours = systime.wHour;
		minutes = systime.wMinute;
		seconds = systime.wSecond;

		sprintf (str,"%i:%i%i:%i%i", hours%12, minutes/10, minutes%10, seconds/10, seconds%10);
	}
#endif
	else
		return;

	Draw_String (glwidth - (strlen(str)<<3), glheight - sb_lines - 8, str);

	scr_tileclear_updates = 0;
}

/*
==============
SCR_DrawRam
==============
*/
void SCR_DrawRam (void)
{
	if (!scr_showram.value)
		return;

	if (!r_cache_thrash)
		return;

	Draw_Pic (scr_vrect.x+32, scr_vrect.y, scr_ram);
}

/*
==============
SCR_DrawTurtle
==============
*/
void SCR_DrawTurtle (void)
{
	static int	count;
	
	if (!scr_showturtle.value)
		return;

	if (host_frametime < 0.1)
	{
		count = 0;
		return;
	}

	count++;
	if (count < 3)
		return;

	Draw_Pic (scr_vrect.x, scr_vrect.y, scr_turtle);
}

/*
==============
SCR_DrawNet
==============
*/
void SCR_DrawNet (void)
{
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	Draw_Pic (scr_vrect.x+64, scr_vrect.y, scr_net);
}

/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	qpic_t	*pic;

	if (!cl.paused)
		return;

	if (!scr_showpause.value)		// turn off for screenshots
		return;

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	pic = Draw_CachePic ("gfx/pause.lmp");
	Draw_Pic ( (320 - pic->width)/2, (240 - 48 - pic->height)/2, pic); //johnfitz -- stretched menus

	scr_tileclear_updates = 0; //johnfitz
}

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	qpic_t	*pic;

	if (!scr_drawloading)
		return;

	GL_SetCanvas (CANVAS_MENU); //johnfitz
		
	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic ( (320 - pic->width)/2, (240 - 48 - pic->height)/2, pic); //johnfitz -- stretched menus

	scr_tileclear_updates = 0; //johnfitz
}



//=============================================================================


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	Con_CheckResize ();
	
	if (scr_drawloading)
		return;		// never a console with loading plaque
		
// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	if (con_forcedup)
	{
		scr_conlines = glheight; //full screen //johnfitz -- glheight instead of vid.height
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = glheight/2; //half screen //johnfitz -- glheight instead of vid.height
	else
		scr_conlines = 0; //none visible
	
	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed.value*host_frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed.value*host_frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

	if (clearconsole++ < vid.numpages)
		Sbar_Changed ();

	if (!con_forcedup && scr_con_current)
		scr_tileclear_updates = 0; //johnfitz
}
	
/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	if (scr_con_current)
	{
		Con_DrawConsole (scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/ 

/* 
================== 
SCR_ScreenShot_f
================== 
*/  
void SCR_ScreenShot_f (void) 
{
	byte		*buffer;
	char		tganame[80]; 
	char		checkname[MAX_OSPATH];
	int			i, c, temp;
// 
// find a file name to save it to 
// 
	strcpy(tganame,"quake00.tga");
		
	for (i=0 ; i<=99 ; i++) 
	{ 
		tganame[5] = i/10 + '0'; 
		tganame[6] = i%10 + '0'; 
		sprintf (checkname, "%s/%s", com_gamedir, tganame);
		if (Sys_FileTime(checkname) == -1)
			break;	// file doesn't exist
	} 
	if (i==100) 
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't create a TGA file\n"); //johnfitz -- TGA, not PCX!
		return;
 	}


	buffer = malloc(glwidth*glheight*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = glwidth&255;
	buffer[13] = glwidth>>8;
	buffer[14] = glheight&255;
	buffer[15] = glheight>>8;
	buffer[16] = 24;	// pixel size

	glReadPixels (glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, buffer+18); 

	// swap rgb to bgr
	c = 18+glwidth*glheight*3;
	for (i=18 ; i<c ; i+=3)
	{
		temp = buffer[i];
		buffer[i] = buffer[i+2];
		buffer[i+2] = temp;
	}
	COM_WriteFile (tganame, buffer, glwidth*glheight*3 + 18 );

	free (buffer);
	Con_Printf ("Wrote %s\n", tganame);
} 


//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected)
		return;
	if (cls.signon != SIGNONS)
		return;
	
// redraw with no console and the loading plaque
	Con_ClearNotify ();
	scr_centertime_off = 0;
	scr_con_current = 0;

	scr_drawloading = true;
	Sbar_Changed ();
	SCR_UpdateScreen ();
	scr_drawloading = false;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	Con_ClearNotify ();
}

//=============================================================================

char	*scr_notifystring;
qboolean	scr_drawdialog;

void SCR_DrawNotifyString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;

	GL_SetCanvas (CANVAS_MENU); //johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; //johnfitz -- stretched overlays

	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l*8)/2; //johnfitz -- stretched overlays
		for (j=0 ; j<l ; j++, x+=8)
			Draw_Character (x, y, start[j]);
			
		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.  
==================
*/
int SCR_ModalMessage (char *text)
{
	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;
 
// draw a fresh screen
	scr_drawdialog = true;
	SCR_UpdateScreen ();
	scr_drawdialog = false;
	
	S_ClearBuffer ();		// so dma doesn't loop current sound

	do
	{
		key_count = -1;		// wait for a key down and up
		Sys_SendKeyEvents ();
	} while (key_lastpress != 'y' && key_lastpress != 'n' && key_lastpress != K_ESCAPE);

	SCR_UpdateScreen ();

	return key_lastpress == 'y';
}


//=============================================================================

//johnfitz -- deleted SCR_BringDownConsole


/*
==================
SCR_TileClear -- johnfitz -- modified to use glwidth/glheight instead of vid.width/vid.height
                             also fixed the dimentions of right and top panels
							 also added scr_tileclear_updates
==================
*/
void SCR_TileClear (void)
{
	if (scr_tileclear_updates >= vid.numpages && !gl_clear.value) 
		return;
	scr_tileclear_updates++;

	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear (0, 
						0,
						r_refdef.vrect.x,
						glheight - sb_lines);
		// right
		Draw_TileClear (r_refdef.vrect.x + r_refdef.vrect.width, 
						0, 
						glwidth - r_refdef.vrect.x - r_refdef.vrect.width,
						glheight - sb_lines);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear (r_refdef.vrect.x, 
						0, 
						r_refdef.vrect.width, 
						r_refdef.vrect.y);
		// bottom
		Draw_TileClear (r_refdef.vrect.x,
						r_refdef.vrect.y + r_refdef.vrect.height, 
						r_refdef.vrect.width, 
						glheight - r_refdef.vrect.y - r_refdef.vrect.height - sb_lines);
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void SCR_UpdateScreen (void)
{
	vrect_t		vrect;

	if (block_drawing)
		return;

	vid.numpages = 2 + (gl_triplebuffer.value ? 1 : 0); //johnfitz -- in case gl_triplebuffer is not 0 or 1

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
			return;
	}

	if (!scr_initialized || !con_initialized)
		return;				// not initialized yet


	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	
	//
	// determine size of refresh window
	//
	if (oldfov != scr_fov.value)
	{
		oldfov = scr_fov.value;
		vid.recalc_refdef = true;
	}

	if (oldscreensize != scr_viewsize.value)
	{
		oldscreensize = scr_viewsize.value;
		vid.recalc_refdef = true;
	}

	if (oldsbarscale != scr_sbarscale.value)
	{
		oldsbarscale = scr_sbarscale.value;
		vid.recalc_refdef = true;
	}

	if (vid.recalc_refdef)
		SCR_CalcRefdef ();

//
// do 3D refresh drawing, and then update the screen
//
	SCR_SetUpToDrawConsole ();
	
	V_RenderView ();

	GL_Set2D ();

	//FIXME: only call this when needed
	SCR_TileClear ();

	if (scr_drawdialog) //new game confirm
	{
		Sbar_Draw ();
		Draw_FadeScreen ();
		SCR_DrawNotifyString ();
	}
	else if (scr_drawloading) //loading
	{
		SCR_DrawLoading ();
		Sbar_Draw ();
	}
	else if (cl.intermission == 1 && key_dest == key_game) //end of level
	{
		Sbar_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game) //end of episode
	{
		Sbar_FinaleOverlay ();
		SCR_CheckDrawCenterString ();
	}
	else
	{
		if (crosshair.value)
			Draw_Character (scr_vrect.x + scr_vrect.width/2, scr_vrect.y + scr_vrect.height/2, '+');
		
		SCR_DrawFPS (); //johnfitz
		SCR_DrawClock (); //johnfitz
		SCR_DrawRam ();
		SCR_DrawNet ();
		SCR_DrawTurtle ();
		SCR_DrawPause ();
		SCR_CheckDrawCenterString ();
		Sbar_Draw ();
		SCR_DrawConsole ();	
		M_Draw ();
	}

	V_UpdateBlend (); //johnfitz -- V_UpdatePalette cleaned up and renamed

	GL_EndRendering ();
}

