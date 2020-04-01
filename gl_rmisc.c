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
// r_misc.c

#include "quakedef.h"

//johnfitz -- new cvars
extern cvar_t r_stereo;
extern cvar_t r_stereodepth;
extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t gl_overbright_models;
cvar_t r_waterwarp = {"r_waterwarp", "1"};
//johnfitz

extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
void R_SetClearColor_f (void)
{
	byte	*rgb;
	int		s;

	s = (int)r_clearcolor.value & 0xFF;
	rgb = (byte*)(d_8to24table + s);
	glClearColor (rgb[0]/255.0,rgb[1]/255.0,rgb[2]/255.0,0);
}

/*
====================
R_Novis_f -- johnfitz
====================
*/
void R_Novis_f (void)
{
	extern int vis_changed;
	vis_changed = TRUE;
}

//==============================================================================

/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
void R_Envmap_f (void)
{
	byte	buffer[256*256*4];
	char	name[1024];

	glDrawBuffer  (GL_FRONT);
	glReadBuffer  (GL_FRONT);
	envmap = true;

	r_refdef.vrect.x = 0;
	r_refdef.vrect.y = 0;
	r_refdef.vrect.width = 256;
	r_refdef.vrect.height = 256;

	r_refdef.viewangles[0] = 0;
	r_refdef.viewangles[1] = 0;
	r_refdef.viewangles[2] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env0.rgb", buffer, sizeof(buffer));		

	r_refdef.viewangles[1] = 90;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env1.rgb", buffer, sizeof(buffer));		

	r_refdef.viewangles[1] = 180;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env2.rgb", buffer, sizeof(buffer));		

	r_refdef.viewangles[1] = 270;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env3.rgb", buffer, sizeof(buffer));		

	r_refdef.viewangles[0] = -90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env4.rgb", buffer, sizeof(buffer));		

	r_refdef.viewangles[0] = 90;
	r_refdef.viewangles[1] = 0;
	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	R_RenderView ();
	glReadPixels (0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
	COM_WriteFile ("env5.rgb", buffer, sizeof(buffer));		

	envmap = false;
	glDrawBuffer  (GL_BACK);
	glReadBuffer  (GL_BACK);
	GL_EndRendering ();
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{	
	extern byte *hunk_base;
	extern cvar_t gl_finish;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);	
	Cmd_AddCommand ("envmap", R_Envmap_f);	
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);	

	Cvar_RegisterVariable (&r_norefresh, NULL);
	Cvar_RegisterVariable (&r_lightmap, NULL);
	Cvar_RegisterVariable (&r_fullbright, NULL);
	Cvar_RegisterVariable (&r_drawentities, NULL);
	Cvar_RegisterVariable (&r_drawviewmodel, NULL);
	Cvar_RegisterVariable (&r_shadows, NULL);
	Cvar_RegisterVariable (&r_wateralpha, NULL);
	Cvar_RegisterVariable (&r_dynamic, NULL);
	Cvar_RegisterVariable (&r_novis, R_Novis_f);
	Cvar_RegisterVariable (&r_speeds, NULL);

	Cvar_RegisterVariable (&gl_finish, NULL);
	Cvar_RegisterVariable (&gl_clear, NULL);
	Cvar_RegisterVariable (&gl_texsort, NULL);
	Cvar_RegisterVariable (&gl_cull, NULL);
	Cvar_RegisterVariable (&gl_smoothmodels, NULL);
	Cvar_RegisterVariable (&gl_affinemodels, NULL);
	Cvar_RegisterVariable (&gl_polyblend, NULL);
	Cvar_RegisterVariable (&gl_flashblend, NULL);
	Cvar_RegisterVariable (&gl_playermip, NULL);
	Cvar_RegisterVariable (&gl_nocolors, NULL);
	Cvar_RegisterVariable (&gl_keeptjunctions, NULL);

	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_stereo, NULL);
	Cvar_RegisterVariable (&r_stereodepth, NULL);
	Cvar_RegisterVariable (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterwarp, NULL);
	Cvar_RegisterVariable (&r_drawflat, NULL);
	Cvar_RegisterVariable (&r_flatlightstyles, NULL);
	Cvar_RegisterVariable (&gl_farclip, NULL);
	Cvar_RegisterVariable (&gl_fullbrights, NULL);
	Cvar_RegisterVariable (&gl_overbright_models, NULL);
	//johnfitz

	R_InitParticles ();
	R_SetClearColor_f (); //johnfitz

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz

#ifdef GLTEST
	Test_Init ();
#endif
}

/*
===============
R_TranslatePlayerSkin -- johnfitz -- much revision

Translates a skin texture by the per-player color lookup
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int		top, bottom;
	byte	translate[256];
	unsigned	translate32[256];
	int		i, j, s;
	model_t	*model;
	aliashdr_t *paliashdr;
	byte	*original;
	unsigned	pixels[512*256], *out;
	unsigned	scaled_width, scaled_height;
	int			inwidth, inheight;
	byte		*inrow;
	unsigned	frac, fracstep;
	extern	byte		**player_8bit_texels_tbl;

	top = cl.scores[playernum].colors & 0xf0;
	bottom = (cl.scores[playernum].colors &15)<<4;

	for (i=0 ; i<256 ; i++)
		translate[i] = i;

	for (i=0 ; i<16 ; i++)
	{
		if (top < 128)	// the artists made some backwards ranges.  sigh.
			translate[TOP_RANGE+i] = top+i;
		else
			translate[TOP_RANGE+i] = top+15-i;
				
		if (bottom < 128)
			translate[BOTTOM_RANGE+i] = bottom+i;
		else
			translate[BOTTOM_RANGE+i] = bottom+15-i;
	}

	//
	// locate the original skin pixels
	//
	currententity = &cl_entities[1+playernum];
	model = currententity->model;
	if (!model)
		return;		// player doesn't have a model yet
	if (model->type != mod_alias)
		return; // only translate skins on alias models

	paliashdr = (aliashdr_t *)Mod_Extradata (model);
	s = paliashdr->skinwidth * paliashdr->skinheight;
	if (currententity->skinnum < 0 || currententity->skinnum >= paliashdr->numskins) {
		Con_Printf("(%d): Invalid player skin #%d\n", playernum, currententity->skinnum);
		original = (byte *)paliashdr + paliashdr->texels[0];
	} else
		original = (byte *)paliashdr + paliashdr->texels[currententity->skinnum];
	if (s & 3)
		Sys_Error ("R_TranslateSkin: s&3");

	// because this happens during gameplay, do it fast
	// instead of sending it through gl_upload 8

	inwidth = Pad(paliashdr->skinwidth); //johnfitz -- texels already padded in mod_loadallskins
	inheight = Pad(paliashdr->skinheight); //johnfitz -- texels already padded in mod_loadallskins

	scaled_width = TexMgr_SafeTextureSize (inwidth); 
	scaled_height = TexMgr_SafeTextureSize (inheight);
	
	for (i=0 ; i<256 ; i++)
		translate32[i] = d_8to24table[translate[i]];

	//no need to resample; image is already padded to a power of 2
	{
		char	name[64];

		for (i=0; i<inwidth*inheight; i+=4)
		{
			pixels[i] = translate32[original[i]];
			pixels[i+1] = translate32[original[i+1]];
			pixels[i+2] = translate32[original[i+2]];
			pixels[i+3] = translate32[original[i+3]];
		}
		sprintf(name, "player_%i", playernum);
		playertextures[playernum] = TexMgr_LoadImage32 (name, scaled_width, scaled_height, pixels, 0); //johnfitz
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;
	
	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

	memset (&r_worldentity, 0, sizeof(r_worldentity));
	r_worldentity.model = cl.worldmodel;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;
		 	
	r_viewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();

// clear texture chains
	//johnfitz -- deleted sky-specific stuff, no longer needed
	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
	{
		if (!cl.worldmodel->textures[i])
			continue;
 		cl.worldmodel->textures[i]->texturechain = NULL;
	}

	Sky_NewMap (); //johnfitz
	Fog_NewMap (); //johnfitz

	r_framecount = 0; //johnfitz -- paranoid?
	r_visframecount = 0; //johnfitz -- paranoid?
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int			i;
	float		start, stop, time;
	int			startangle;
	vrect_t		vr;

	glDrawBuffer  (GL_FRONT);
	glFinish ();

	start = Sys_FloatTime ();
	for (i=0 ; i<128 ; i++)
	{
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
	}

	glFinish ();
	stop = Sys_FloatTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);

	glDrawBuffer (GL_BACK);
	GL_EndRendering ();
}

void D_FlushCaches (void)
{
}
