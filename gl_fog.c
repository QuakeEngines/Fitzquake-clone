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
//gl_fog.c -- fitzquake fog code

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

float fade_time = 0.0; //duration of fade
float fade_done = 0.0; //time when fade will be done

/*
=============
Fog_Parse

update internal variables whenever an SVC_FOG or console fog command is issued
=============
*/
void Fog_Parse (float density, float red, float green, float blue, float time)
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
Fog_FogCommand_f

called to handle the 'fog' console command
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
		Fog_Parse(max(0.0, atof(Cmd_Argv(1))),
				  fog_red,
				  fog_green,
				  fog_blue,
				  0.0);
		break;
	case 3: //TEST
		Fog_Parse(max(0.0, atof(Cmd_Argv(1))),
				  fog_red,
				  fog_green,
				  fog_blue,
				  atof(Cmd_Argv(2)));
		break;
	case 4:
		Fog_Parse(fog_density,
				  Clamp(0.0, atof(Cmd_Argv(2)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(3)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(4)), 1.0),
				  0.0);
		break;
	case 5:
		Fog_Parse(max(0.0, atof(Cmd_Argv(1))),
				  Clamp(0.0, atof(Cmd_Argv(2)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(3)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(4)), 1.0),
				  0.0);
		break;
	case 6: //TEST
		Fog_Parse(max(0.0, atof(Cmd_Argv(1))),
				  Clamp(0.0, atof(Cmd_Argv(2)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(3)), 1.0),
				  Clamp(0.0, atof(Cmd_Argv(4)), 1.0),
				  atof(Cmd_Argv(5)));
		break;
	}
}

/*
=============
Fog_NewMap

called whenever a map is loaded
=============
*/
void Fog_NewMap (void)
{
	char key[128], value[4096];
	char *data;

	//initially no fog
	fog_density = 0;

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
Fog_Init

called when quake initializes
=============
*/
void Fog_Init (void)
{
	fog_density = 0.0;
	fog_red = 0.3;
	fog_green = 0.3;
	fog_blue = 0.3;

	Cmd_AddCommand ("fog",Fog_FogCommand_f);

#ifdef FOGEXP2
	glFogi(GL_FOG_MODE, GL_EXP2);
#else
	glFogi(GL_FOG_START, 0);
	glFogi(GL_FOG_MODE, GL_LINEAR);
#endif
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
	glFogf(GL_FOG_END, 96.0 / d);
#endif
}

/*
=============
Fog_EnableGFog

called before drawing stuff that should be fogged
=============
*/
void Fog_EnableGFog (void)
{
	float f, d;

	if (r_drawflat.value)
		return;

	if (fade_done > cl.time)
	{
		f = (fade_done - cl.time) / fade_time;
		d = f * old_density + (1.0 - f) * fog_density;
	}
	else
	{
		d = fog_density;
	}

	if (d > 0)
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
	float f, d;

	if (r_drawflat.value)
		return;

	if (fade_done > cl.time)
	{
		f = (fade_done - cl.time) / fade_time;
		d = f * old_density + (1.0 - f) * fog_density;
	}
	else
	{
		d = fog_density;
	}

	if (d > 0)
		glDisable(GL_FOG);
}

/*
=============
Fog_ColorForSky

called before drawing flat-colored sky
=============
*/
void Fog_ColorForSky (void)
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
	}
	else
	{
		d = fog_density;
		c[0] = fog_red;
		c[1] = fog_green;
		c[2] = fog_blue;
	}

	if (d > 0)
		glColor3f (c[0], c[1], c[2]);
}
