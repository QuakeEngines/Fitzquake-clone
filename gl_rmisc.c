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
extern cvar_t r_clearcolor;
//extern cvar_t r_fullbright_world;
//extern cvar_t r_fullbright_bmodels;
//extern cvar_t r_fullbright_bspmodels;
//extern cvar_t r_fullbright_models;
//extern cvar_t r_fullbright_particles;
extern cvar_t r_particles;
extern cvar_t r_drawflat;
//extern cvar_t _gl_texturemode;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
cvar_t r_waterwarp = {"r_waterwarp", "1"};
//johnfitz

extern int gl_filter_min, gl_filter_max; //johnfitz
extern int particletexture1, particletexture2, particletexture3; //johnfitz

void Draw_TextureMode_f (void); //johnfitz


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

/*
==================
R_InitTextures
==================
*/
void	R_InitTextures (void)
{
	int		x,y, m;
	byte	*dest;

// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
	
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->offsets[0] = sizeof(texture_t);
	r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16*16;
	r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8*8;
	r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4*4;
	
	for (m=0 ; m<4 ; m++)
	{
		dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}	
}
/*
================================================================================

Particles

================================================================================
*/
//johnfitz -- generate nice antialiased 32x32 circle for particles
int R_ParticleTextureLookup (int x, int y, int sharpness) 
{
	int r; //distance from point x,y to circle origin, squared
	int a; //alpha value to return

	x -= 16;
	y -= 16;
	r = x * x + y * y;
	r = r > 255 ? 255 : r;
	a = sharpness * (255 - r);
	a = a > 255 ? 255 : a;
	return a;
}
//johnfitz

void R_InitParticleTextures (void)
{
	int		x,y;
	byte	data[64][64][4]; //johnfitz -- bigger texture

	// particle texture 1 -- circle
	particletexture1 = texture_extension_number++;
    GL_Bind(particletexture1);

	for (x=0 ; x<64 ; x++)
		for (y=0 ; y<64 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = R_ParticleTextureLookup(x, y, 8);
		}
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// particle texture 2 -- square
	particletexture2 = texture_extension_number++;
    GL_Bind(particletexture2);

	for (x=0 ; x<2 ; x++)
		for (y=0 ; y<2 ; y++)
		{
			data[y][x][0] = 255;
			data[y][x][1] = 255;
			data[y][x][2] = 255;
			data[y][x][3] = x || y ? 0 : 255;
		}
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	//set default
	particletexture = particletexture1;
}

/*
===============
R_SetParticleTexture_f
===============
*/
void R_SetParticleTexture_f (void)
{
	switch ((int)(r_particles.value))
	{
	case 1:
		particletexture = particletexture1;
		break;
	case 2:
		particletexture = particletexture2;
		break;
	}
}

//==============================================================================

//johnfitz -- for the darkened background when you go to the menu
byte fadescreen[8][8] =
{
	{0,1,1,1,0,1,1,1},
	{1,1,0,1,1,1,0,1},
	{0,1,1,1,0,1,1,1},
	{1,1,0,1,1,1,0,1},
	{0,1,1,1,0,1,1,1},
	{1,1,0,1,1,1,0,1},
	{0,1,1,1,0,1,1,1},
	{1,1,0,1,1,1,0,1},
};

void R_InitScreenTexture (void) //johnfitz -- for the darkened background when you go to the menu
{
	int		x,y;
	byte	data[8][8][4];

	screentexture = texture_extension_number++;
    GL_Bind(screentexture);

	for (x=0 ; x<8 ; x++)
	{
		for (y=0 ; y<8 ; y++)
		{
			data[y][x][0] = 0;
			data[y][x][1] = 0;
			data[y][x][2] = 0;
			data[y][x][3] = fadescreen[y][x]*255;
		}
	}
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
 	if (gl_mtexable)
		Cvar_SetValue ("gl_texsort", 0.0);
	Cvar_RegisterVariable (&gl_cull, NULL);
	Cvar_RegisterVariable (&gl_smoothmodels, NULL);
	Cvar_RegisterVariable (&gl_affinemodels, NULL);
	Cvar_RegisterVariable (&gl_polyblend, NULL);
	Cvar_RegisterVariable (&gl_flashblend, NULL);
	Cvar_RegisterVariable (&gl_playermip, NULL);
	Cvar_RegisterVariable (&gl_nocolors, NULL);
	Cvar_RegisterVariable (&gl_keeptjunctions, NULL);
	Cvar_RegisterVariable (&gl_reporttjunctions, NULL);
	Cvar_RegisterVariable (&gl_doubleeyes, NULL);

	//johnfitz -- new cvars
//	Cvar_RegisterVariable (&r_fullbright_world, NULL);
//	Cvar_RegisterVariable (&r_fullbright_bmodels, NULL);
//	Cvar_RegisterVariable (&r_fullbright_bspmodels, NULL);
//	Cvar_RegisterVariable (&r_fullbright_models, NULL);
//	Cvar_RegisterVariable (&r_fullbright_particles, NULL);
	Cvar_RegisterVariable (&r_particles, R_SetParticleTexture_f);
	Cvar_RegisterVariable (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterwarp, NULL);
	Cvar_RegisterVariable (&r_drawflat, NULL);
//	Cvar_RegisterVariable (&_gl_texturemode, Draw_TextureMode_f);
	Cvar_RegisterVariable (&gl_fullbrights, NULL);
	Cvar_RegisterVariable (&gl_farclip, NULL);
	//johnfitz

	R_InitParticles ();
	R_InitParticleTextures (); //johnfitz
	R_InitScreenTexture (); //johnfitz
	R_SetClearColor_f (); //johnfitz

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz

#ifdef GLTEST
	Test_Init ();
#endif

	playertextures = texture_extension_number;
	texture_extension_number += 16;
}

/*
===============
R_TranslatePlayerSkin

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

	GL_DisableMultitexture();

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

	inwidth = Pad(paliashdr->skinwidth); //johnfitz -- texels already padded in mod_loadallskins
	inheight = Pad(paliashdr->skinheight); //johnfitz -- texels already padded in mod_loadallskins

	// because this happens during gameplay, do it fast
	// instead of sending it through gl_upload 8
    GL_Bind(playertextures + playernum);

	scaled_width = gl_max_size.value < 512 ? gl_max_size.value : 512;
	scaled_height = gl_max_size.value < 256 ? gl_max_size.value : 256;

	// allow users to crunch sizes down even more if they want
	scaled_width >>= (int)gl_playermip.value;
	scaled_height >>= (int)gl_playermip.value;

	if (VID_Is8bit()) { // 8bit texture upload
		byte *out2;

		out2 = (byte *)pixels;
		memset(pixels, 0, sizeof(pixels));
		fracstep = inwidth*0x10000/scaled_width;
		for (i=0 ; i<scaled_height ; i++, out2 += scaled_width)
		{
			inrow = original + inwidth*(i*inheight/scaled_height);
			frac = fracstep >> 1;
			for (j=0 ; j<scaled_width ; j+=4)
			{
				out2[j] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+1] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+2] = translate[inrow[frac>>16]];
				frac += fracstep;
				out2[j+3] = translate[inrow[frac>>16]];
				frac += fracstep;
			}
		}

		GL_Upload8_EXT ((byte *)pixels, scaled_width, scaled_height, false, false);
		return;
	}

	for (i=0 ; i<256 ; i++)
		translate32[i] = d_8to24table[translate[i]];

	out = pixels;
	fracstep = inwidth*0x10000/scaled_width;
	for (i=0 ; i<scaled_height ; i++, out += scaled_width)
	{
		inrow = original + inwidth*(i*inheight/scaled_height);
		frac = fracstep >> 1;
		for (j=0 ; j<scaled_width ; j+=4)
		{
			out[j] = translate32[inrow[frac>>16]];
			frac += fracstep;
			out[j+1] = translate32[inrow[frac>>16]];
			frac += fracstep;
			out[j+2] = translate32[inrow[frac>>16]];
			frac += fracstep;
			out[j+3] = translate32[inrow[frac>>16]];
			frac += fracstep;
		}
	}
	glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
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

	glDrawBuffer  (GL_BACK);
	GL_EndRendering ();
}

void D_FlushCaches (void)
{
}
