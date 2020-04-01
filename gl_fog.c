/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002 John Fitzgibbons and others

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
//gl_fog.c -- global and volumetric fog

#include "quakedef.h"

extern cvar_t r_drawflat;

//==============================================================================
//
//  GLOBAL FOG
//
//==============================================================================

#define FOGEXP2 //comment out for linear fog

float fog_density;
float fog_red;
float fog_green;
float fog_blue;

float old_density;
float old_red;
float old_green;
float old_blue;

float fade_time; //duration of fade
float fade_done; //time when fade will be done

/*
=============
Fog_Update

update internal variables
=============
*/
void Fog_Update (float density, float red, float green, float blue, float time)
{
	//save previous settings for fade
	if (time > 0)
	{
		//check for a fade in progress
		if (fade_done > cl.time)
		{
			float f, d;

			f = (fade_done - cl.time) / fade_time;
			old_density = f * old_density + (1.0 - f) * fog_density; 
			old_red = f * old_red + (1.0 - f) * fog_red;
			old_green = f * old_green + (1.0 - f) * fog_green;
			old_blue = f * old_blue + (1.0 - f) * fog_blue;
		}
		else
		{
			old_density = fog_density;
			old_red = fog_red;
			old_green = fog_green;
			old_blue = fog_blue;
		}
	}

	fog_density = density;
	fog_red = red;
	fog_green = green;
	fog_blue = blue;
	fade_time = time;
	fade_done = cl.time + time;
}

/*
=============
Fog_ParseServerMessage

handle an SVC_FOG message from server
=============
*/
void Fog_ParseServerMessage (void)
{
	float density, red, green, blue, time;

	density = MSG_ReadByte() / 255.0;
	red = MSG_ReadByte() / 255.0;
	green = MSG_ReadByte() / 255.0;
	blue = MSG_ReadByte() / 255.0;
	time = max(0.0, MSG_ReadShort() / 100.0);
	
	Fog_Update (density, red, green, blue, time);
}

/*
=============
Fog_FogCommand_f

handle the 'fog' console command
=============
*/
void Fog_FogCommand_f (void)
{
	switch (Cmd_Argc())
	{
	default:
	case 1:
		Con_Printf("usage:\n");
		Con_Printf("   fog <density>\n");
		Con_Printf("   fog <red> <green> <blue>\n");
		Con_Printf("   fog <density> <red> <green> <blue>\n");
		Con_Printf("current values:\n");
		Con_Printf("   \"density\" is \"%f\"\n", fog_density);
		Con_Printf("   \"red\" is \"%f\"\n", fog_red);
		Con_Printf("   \"green\" is \"%f\"\n", fog_green);
		Con_Printf("   \"blue\" is \"%f\"\n", fog_blue);
		break;
	case 2:
		Fog_Update(max(0.0, atof(Cmd_Argv(1))),
				   fog_red,
				   fog_green,
				   fog_blue,
				   0.0);
		break;
	case 3: //TEST
		Fog_Update(max(0.0, atof(Cmd_Argv(1))),
				   fog_red,
				   fog_green,
				   fog_blue,
				   atof(Cmd_Argv(2)));
		break;
	case 4:
		Fog_Update(fog_density,
				   CLAMP(0.0, atof(Cmd_Argv(1)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(2)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(3)), 1.0),
				   0.0);
		break;
	case 5:
		Fog_Update(max(0.0, atof(Cmd_Argv(1))),
				   CLAMP(0.0, atof(Cmd_Argv(2)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(3)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(4)), 1.0),
				   0.0);
		break;
	case 6: //TEST
		Fog_Update(max(0.0, atof(Cmd_Argv(1))),
				   CLAMP(0.0, atof(Cmd_Argv(2)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(3)), 1.0),
				   CLAMP(0.0, atof(Cmd_Argv(4)), 1.0),
				   atof(Cmd_Argv(5)));
		break;
	}
}

/*
=============
Fog_ParseWorldspawn

called at map load
=============
*/
void Fog_ParseWorldspawn (void)
{
	char key[128], value[4096];
	char *data;

	//initially no fog
	fog_density = 0.0;
	old_density = 0.0;
	fade_time = 0.0;
	fade_done = 0.0;

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			strcpy(key, com_token + 1);
		else
			strcpy(key, com_token);
		while (key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_Parse(data);
		if (!data)
			return; // error
		strcpy(value, com_token);

		if (!strcmp("fog", key))
		{
			sscanf(value, "%f %f %f %f", &fog_density, &fog_red, &fog_green, &fog_blue);
		}
	}
}

/*
=============
Fog_SetupFrame

called at the beginning of each frame
=============
*/
void Fog_SetupFrame (void)
{
	float c[4];
	float f, d;

	if (fade_done > cl.time)
	{
		f = (fade_done - cl.time) / fade_time;
		d = f * old_density + (1.0 - f) * fog_density; 
		c[0] = f * old_red + (1.0 - f) * fog_red;
		c[1] = f * old_green + (1.0 - f) * fog_green;
		c[2] = f * old_blue + (1.0 - f) * fog_blue;
		c[3] = 1.0;
	}
	else
	{
		d = fog_density;
		c[0] = fog_red;
		c[1] = fog_green;
		c[2] = fog_blue;
		c[3] = 1.0;
	}

	glFogfv(GL_FOG_COLOR, c);

#ifdef FOGEXP2
	glFogf(GL_FOG_DENSITY, d / 64.0);
#else
	glFogf(GL_FOG_END, 96.0 / max(d, 0.000001)); //don't divide by zero
#endif
}

/*
=============
Fog_GetDensity

returns current density of fog
=============
*/
float Fog_GetDensity (void)
{
	float f;

	if (fade_done > cl.time)
	{
		f = (fade_done - cl.time) / fade_time;
		return f * old_density + (1.0 - f) * fog_density; 
	}
	else
		return fog_density;
}

/*
=============
Fog_EnableGFog

called before drawing stuff that should be fogged
=============
*/
void Fog_EnableGFog (void)
{
	if (Fog_GetDensity() > 0 && !r_drawflat.value)
		glEnable(GL_FOG);
}

/*
=============
Fog_DisableGFog

called after drawing stuff that should be fogged
=============
*/
void Fog_DisableGFog (void)
{
	if (Fog_GetDensity() > 0 && !r_drawflat.value)
		glDisable(GL_FOG);
}

/*
=============
Fog_SetColorForSky

called before drawing flat-colored sky
=============
*/
void Fog_SetColorForSky (void)
{
	float c[3];
	float f, d;

	if (fade_done > cl.time)
	{
		f = (fade_done - cl.time) / fade_time;
		d = f * old_density + (1.0 - f) * fog_density; 
		c[0] = f * old_red + (1.0 - f) * fog_red;
		c[1] = f * old_green + (1.0 - f) * fog_green;
		c[2] = f * old_blue + (1.0 - f) * fog_blue;
	}
	else
	{
		d = fog_density;
		c[0] = fog_red;
		c[1] = fog_green;
		c[2] = fog_blue;
	}

	if (d > 0)
		glColor3fv (c);
}

//==============================================================================
//
//  VOLUMETRIC FOG
//
//==============================================================================

//removed for now

void Fog_DrawVFog (void){}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
=============
Fog_NewMap

called whenever a map is loaded
=============
*/
void Fog_NewMap (void)
{
	Fog_ParseWorldspawn (); //for global fog
}

/*
=============
Fog_Init

called when quake initializes
=============
*/
void Fog_Init (void)
{
	Cmd_AddCommand ("fog",Fog_FogCommand_f);

	//set up global fog
	fog_density = 0.0;
	fog_red = 0.3;
	fog_green = 0.3;
	fog_blue = 0.3;

#ifdef FOGEXP2
	glFogi(GL_FOG_MODE, GL_EXP2);
#else
	glFogi(GL_FOG_START, 0);
	glFogi(GL_FOG_MODE, GL_LINEAR);
#endif
}