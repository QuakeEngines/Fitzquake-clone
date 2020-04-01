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

// draw.c -- this is the only file outside the refresh that touches the
// vid buffer

#include "quakedef.h"

#define GL_COLOR_INDEX8_EXT     0x80E5

extern int skybox_texnum; //johnfitz

extern unsigned char d_15to8table[65536];

cvar_t		gl_nobind = {"gl_nobind", "0"};
cvar_t		gl_max_size = {"gl_max_size", "0"}; //johnfitz -- zero to disable user clamping
cvar_t		gl_picmip = {"gl_picmip", "0"};

cvar_t		scr_stretch_menus = {"scr_stretch_menus", "1", true}; //johnfitz
cvar_t		scr_conalpha = {"scr_conalpha", "1"}; //johnfitz

qpic_t		*draw_disc;
qpic_t		*draw_backtile;

int			translate_texture;
int			char_texture;

typedef struct
{
	int		texnum;
	float	sl, tl, sh, th;
} glpic_t;

int		gl_lightmap_format = 4;
int		gl_solid_format = 3;
int		gl_alpha_format = 4;

int		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
int		gl_filter_max = GL_LINEAR;

int		gl_hardware_maxsize; //johnfitz
int		gl_stencilbits; //johnfitz

int		texels;

typedef struct
{
	int		texnum;
	char	identifier[64];
	int		width, height;
	qboolean	mipmap;
	unsigned short	crc; //johnfitz -- texture crc
} gltexture_t;

#define	MAX_GLTEXTURES	1024
gltexture_t	gltextures[MAX_GLTEXTURES];
int			numgltextures;

//==============================================================================
//
//  TEXTURE SELECTION
//
//==============================================================================

void GL_Bind (int texnum)
{
	if (gl_nobind.value)
		texnum = char_texture;
	if (currenttexture == texnum)
		return;
	currenttexture = texnum;
#ifdef _WIN32
	bindTexFunc (GL_TEXTURE_2D, texnum);
#else
	glBindTexture(GL_TEXTURE_2D, texnum);
#endif
}

static GLenum oldtarget = TEXTURE0_SGIS;

void GL_SelectTexture (GLenum target) 
{
	if (!gl_mtexable)
		return;
	qglSelectTextureSGIS(target);
	if (target == oldtarget) 
		return;
	cnttextures[oldtarget-TEXTURE0_SGIS] = currenttexture;
	currenttexture = cnttextures[target-TEXTURE0_SGIS];
	oldtarget = target;
}

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT*4];
qboolean	scrap_dirty;
int			scrap_texnum;

// returns a texture number and the position inside it
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		bestx;
	int		texnum;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i=0 ; i<BLOCK_WIDTH-w ; i++)
		{
			best2 = 0;

			for (j=0 ; j<w ; j++)
			{
				if (scrap_allocated[texnum][i+j] >= best)
					break;
				if (scrap_allocated[texnum][i+j] > best2)
					best2 = scrap_allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i=0 ; i<w ; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
	return 0; //johnfitz -- shut up compiler
}

int	scrap_uploads;

void Scrap_Upload (void)
{
	int		texnum;

	scrap_uploads++;

	for (texnum=0 ; texnum<MAX_SCRAPS ; texnum++) {
		GL_Bind(scrap_texnum + texnum);
		GL_Upload8 (scrap_texels[texnum], BLOCK_WIDTH, BLOCK_HEIGHT, false, true);
	}
	scrap_dirty = false;
}

typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

qpic_t *Draw_PicFromWad (char *name)
{
	qpic_t	*p;
	glpic_t	*gl;

	p = W_GetLumpName (name);
	gl = (glpic_t *)p->data;

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i=0 ; i<p->height ; i++)
			for (j=0 ; j<p->width ; j++, k++)
				scrap_texels[texnum][(y+i)*BLOCK_WIDTH + x + j] = p->data[k];
		texnum += scrap_texnum;
		gl->texnum = texnum;
		gl->sl = (x+0.01)/(float)BLOCK_WIDTH;
		gl->sh = (x+p->width-0.01)/(float)BLOCK_WIDTH;
		gl->tl = (y+0.01)/(float)BLOCK_WIDTH;
		gl->th = (y+p->height-0.01)/(float)BLOCK_WIDTH;
	}
	else
	{
		gl->texnum = GL_LoadPicTexture (p);
		gl->sl = 0;
		gl->sh = (float)p->width/(float)Pad(p->width); //johnfitz -- account for padding
		gl->tl = 0;
		gl->th = (float)p->height/(float)Pad(p->height); //johnfitz -- account for padding
	}

	return p;
}


/*
================
Draw_CachePic
================
*/
qpic_t	*Draw_CachePic (char *path)
{
	cachepic_t	*pic;
	int			i;
	qpic_t		*dat;
	glpic_t		*gl;

	for (pic=menu_cachepics, i=0 ; i<menu_numcachepics ; pic++, i++)
		if (!strcmp (path, pic->name))
			return &pic->pic;

	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

//
// load the pic from disk
//
	dat = (qpic_t *)COM_LoadTempFile (path);	
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width*dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl = (glpic_t *)pic->pic.data;
	gl->texnum = GL_LoadPicTexture (dat);
	gl->sl = 0;
	gl->sh = (float)dat->width/(float)Pad(dat->width); //johnfitz -- account for padding
	gl->tl = 0;
	gl->th = (float)dat->height/(float)Pad(dat->height); //johnfitz -- account for padding

	return &pic->pic;
}

/*
===============
Draw_LoadConchars -- johnfitz -- load specially becuase the transparent color is black instead of pink
===============
*/
void AlphaEdgeFix(byte *data, int width, int height);
void Draw_LoadConchars (void)
{
#if 1
	//bypass GL_LoadTexture
	int			i, j, p;
	byte		*src;
	unsigned	data[128*128];
	unsigned	*rgba;

	src = W_GetLumpName ("conchars");

	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*128 + j];
			if (p == 0)
				p = 255;
			data[(i*128) + j] = d_8to24table[p];
		}
	AlphaEdgeFix((char *)&data, 128, 128);

	if (!char_texture)
		char_texture = texture_extension_number++;
	GL_Bind (char_texture);
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#else
	//use GL_LoadTexture
	byte	*data;
	int		i;

	data = W_GetLumpName ("conchars");

	for (i=0 ; i<256*64 ; i++)
		if (data[i] == 0)
			data[i] = 255;	// proper transparent color

	char_texture = GL_LoadTexture ("charset", 128, 128, data, false, true);
#endif
}

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
void Draw_LoadPics (void)
{
	Draw_LoadConchars ();
	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t	*pic;
	int			i;

	// reset numgltextures
	numgltextures = 0;

	// empty scrap
	memset(&scrap_allocated, 0, sizeof(scrap_allocated));

	// reload wad pics
	W_LoadWadFile ("gfx.wad");
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

typedef struct
{
	char *name;
	int	minimize, maximize;
} glmode_t;

glmode_t modes[] = {
	{"GL_NEAREST", GL_NEAREST, GL_NEAREST},
	{"GL_LINEAR", GL_LINEAR, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST},
	{"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}/*,
	//johnfitz -- easier aliases for some modes
	{"GL_BILINEAR", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR},
	{"GL_TRILINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR}*/
};

/*
===============
GL_TextureMode -- johnfitz -- set texture mode for one texnum
===============
*/
void GL_TextureMode (int texnum, int minmode, int maxmode)
{
	GL_Bind (texnum);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minmode);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, maxmode);
}

/*
===============
Draw_TextureMode_f -- johnfitz -- much revision
===============
*/
void Draw_TextureMode_f (void)
{
	int		i;
	gltexture_t	*glt;
	extern int	solidskytexture, alphaskytexture;

	if (Cmd_Argc() == 1)
	{
		for (i=0 ; i< 6 ; i++)
			if (gl_filter_min == modes[i].minimize)
			{
				Con_Printf ("%s\n", modes[i].name);
				return;
			}
		Con_Printf ("current filter is unknown???\n");
		return;
	}

	for (i=0 ; i< 6 ; i++)
	{
		if (!Q_strcasecmp (modes[i].name, Cmd_Argv(1) ) )
			break;
	}
	if (i == 6)
	{
		Con_Printf ("bad filter name\n");
		return;
	}

	gl_filter_min = modes[i].minimize;
	gl_filter_max = modes[i].maximize;

	// gltexture_t objects
	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		GL_TextureMode (glt->texnum, glt->mipmap ? gl_filter_min : gl_filter_max, gl_filter_max);

	// scraps
	for (i=0 ; i<MAX_SCRAPS ; i++)
		GL_TextureMode (scrap_texnum + i, gl_filter_max, gl_filter_max);

	// sky
	GL_TextureMode (solidskytexture, gl_filter_max, gl_filter_max);
	GL_TextureMode (alphaskytexture, gl_filter_max, gl_filter_max);

	// skybox
	if (skybox_texnum != -1)
		for (i = 0; i < 6; i++)
			GL_TextureMode (skybox_texnum + i, gl_filter_max, gl_filter_max);

	// player skins
	for (i = 0; i < cl.maxclients; i++)
		GL_TextureMode (playertextures + i, gl_filter_max, gl_filter_max);
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&gl_nobind, NULL);
	Cvar_RegisterVariable (&gl_max_size, NULL);
	Cvar_RegisterVariable (&gl_picmip, NULL);
	Cvar_RegisterVariable (&scr_conalpha, NULL); //johnfitz
	Cvar_RegisterVariable (&scr_stretch_menus, NULL); //johnfitz

	Cmd_AddCommand ("gl_texturemode", &Draw_TextureMode_f);
	
	//poll max size from hardware
	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &gl_hardware_maxsize);

#if 0
	//johnfitz -- confirm presence of stencil buffer
	glGetIntegerv(GL_STENCIL_BITS, &gl_stencilbits);
	if(!gl_stencilbits)
		Con_Printf ("WARNING: Could not create stencil buffer\n");
	else
		Con_Printf ("%i bit stencil buffer\n", gl_stencilbits);
#endif

	// save a texture slot for translated picture (for the setup menu)
	translate_texture = texture_extension_number++;

	// save slots for scraps
	scrap_texnum = texture_extension_number;
	texture_extension_number += MAX_SCRAPS;

	// load pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

extern cvar_t	scr_stretch_menus;

/*
================
Draw_OverlayCharacter -- johnfitz -- stretched menu
================
*/
void Draw_OverlayCharacter (int x, int y, int num)
{
	int				xofs, yofs;
	float			stretch;	
	int				row, col;
	float			frow, fcol, size;

	if (num == 32)
		return;		// space

	num &= 255;
	
	if (y <= -8)
		return; // totally off screen //FIXME: johnfitz -- if char is stretched, it is taller than 8 pixels

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	if (scr_stretch_menus.value)
		stretch = min(vid.width / 320.0, vid.height / 200.0);
	else
		stretch = 1;
	xofs = (int)(vid.width - stretch*320) / 2;
	yofs = (int)(vid.height - stretch*200) / 2;

	glColor4f (1,1,1,1);
	GL_Bind (char_texture);
	glBegin (GL_QUADS);
	glTexCoord2f (fcol, frow);
	glVertex2f (stretch * x + xofs, stretch * y + yofs);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (stretch * (x+8) + xofs, stretch * y + yofs);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (stretch * (x+8) + xofs, stretch * (y+8) + yofs);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (stretch * x + xofs, stretch * (y+8) + yofs);
	glEnd ();
}

/*
================
Draw_Character

Draws one 8*8 graphics character with 0 being transparent.
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Character (int x, int y, int num)
{	
	int				row, col;
	float			frow, fcol, size;

	if (num == 32)
		return;		// space

	num &= 255;
	
	if (y <= -8)
		return;			// totally off screen

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	GL_Bind (char_texture);

	glBegin (GL_QUADS);
	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y+8);
	glEnd ();
}

/*
================
Draw_String
================
*/
void Draw_String (int x, int y, char *str)
{
	while (*str)
	{
		Draw_Character (x, y, *str);
		str++;
		x += 8;
	}
}

/*
=============
Draw_AlphaPic
=============
*/
void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{
	glpic_t			*gl;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	glDisable(GL_ALPHA_TEST);
	glEnable (GL_BLEND);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); //johnfitz -- fix con alpha
//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	glCullFace(GL_FRONT);
	glColor4f (1,1,1,alpha);
	GL_Bind (gl->texnum);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();
	glColor4f (1,1,1,1);
	glEnable(GL_ALPHA_TEST);
	glDisable (GL_BLEND);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); //johnfitz -- fix con alpha
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t			*gl;
	
//	if (x < 0 || (unsigned)(x + pic->width) > vid.width || y < 0 || (unsigned)(y + pic->height) > vid.height)
//		Sys_Error ("Draw_Pic: bad coordinates");

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	glColor4f (1,1,1,1);
	GL_Bind (gl->texnum);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (x, y);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (x, y+pic->height);
	glEnd ();
}

/*
=============
Draw_OverlayPic -- johnfitz -- for menu and intermission overlays
all coordinates must be in 320x200
if scr_stretch_menus = 1, will scale all coords up to fill screen
otherwise, will translate all coordinates into a centered 320x200 area
=============
*/
void Draw_OverlayPic (int x, int y, qpic_t *pic)
{
	glpic_t			*gl;
	int				xofs, yofs;
	float			stretch;
	
//	if (x < 0 || (unsigned)(x + pic->width) > 320 || y < 0 || (unsigned)(y + pic->height) > 240)
//		Con_Printf ("Draw_OverlayPic: bad coordinates");

	if (scr_stretch_menus.value)
		stretch = min(vid.width / 320.0, vid.height / 200.0);
	else
		stretch = 1;
	xofs = (int)(vid.width - stretch*320) / 2;
	yofs = (int)(vid.height - stretch*200) / 2;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	glColor4f (1,1,1,1);
	GL_Bind (gl->texnum);
	glBegin (GL_QUADS);
	glTexCoord2f (gl->sl, gl->tl);
	glVertex2f (stretch * x + xofs, stretch * y + yofs);
	glTexCoord2f (gl->sh, gl->tl);
	glVertex2f (stretch * (x+pic->width) + xofs, stretch * y + yofs);
	glTexCoord2f (gl->sh, gl->th);
	glVertex2f (stretch * (x+pic->width) + xofs, stretch * (y+pic->height) + yofs);
	glTexCoord2f (gl->sl, gl->th);
	glVertex2f (stretch * x + xofs, stretch * (y+pic->height) + yofs);
	glEnd ();
}


/*
=============
Draw_TransPicTranslate

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, byte *translation)
{
	int				v, u, c;
	unsigned		trans[64*64], *dest;
	byte			*src;
	int				p;
	int				xofs, yofs; //johnfitz -- stretched menus
	float			stretch; //johnfitz -- stretched menus

	GL_Bind (translate_texture);

	c = pic->width * pic->height;

	dest = trans;
	for (v=0 ; v<64 ; v++, dest += 64)
	{
		src = &menuplyr_pixels[ ((v*pic->height)>>6) *pic->width];
		for (u=0 ; u<64 ; u++)
		{
			p = src[(u*pic->width)>>6];
			if (p == 255)
				dest[u] = 0; //johnfitz -- bad, no pink edges, bad!
			else
				dest[u] =  d_8to24table[translation[p]];
		}
	}

	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max); //johnfitz -- obey texturemode damnit!
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max); //johnfitz -- ditto

	 //johnfitz -- stretched menus
	if (scr_stretch_menus.value)
		stretch = min(vid.width / 320.0, vid.height / 200.0);
	else
		stretch = 1;
	xofs = (int)(vid.width - stretch*320) / 2;
	yofs = (int)(vid.height - stretch*200) / 2;

	glColor4f (1,1,1,1);
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (stretch * x + xofs, stretch * y + yofs);
	glTexCoord2f (1, 0);
	glVertex2f (stretch * (x+pic->width) + xofs, stretch * y + yofs);
	glTexCoord2f (1, 1);
	glVertex2f (stretch * (x+pic->width) + xofs, stretch * (y+pic->height) + yofs);
	glTexCoord2f (0, 1);
	glVertex2f (stretch * x + xofs, stretch * (y+pic->height) + yofs);
	glEnd ();
	//johnfitz
}


/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (int lines)
{
	qpic_t *pic;

	pic = Draw_CachePic ("gfx/conback.lmp");

	pic->width = vid.width;
	pic->height = vid.height;

	if (con_forcedup)
	{
		glDisable (GL_ALPHA_TEST);
		Draw_Pic (0, 0, pic);
		glEnable (GL_ALPHA_TEST);
	}
	else if (scr_conalpha.value > 0.99)
		Draw_Pic (0, lines - vid.height, pic);
	else
		Draw_AlphaPic (0, lines - vid.height, pic, scr_conalpha.value);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glColor3f (1,1,1);
	GL_Bind (*(int *)draw_backtile->data);
	glBegin (GL_QUADS);
	glTexCoord2f (x/64.0, y/64.0);
	glVertex2f (x, y);
	glTexCoord2f ( (x+w)/64.0, y/64.0);
	glVertex2f (x+w, y);
	glTexCoord2f ( (x+w)/64.0, (y+h)/64.0);
	glVertex2f (x+w, y+h);
	glTexCoord2f ( x/64.0, (y+h)/64.0 );
	glVertex2f (x, y+h);
	glEnd ();
}

/*
=============
Draw_OverlayFill -- johnfitz -- stretched overlays

Fills a box of pixels with a single color
=============
*/
void Draw_OverlayFill (int x, int y, int w, int h, int c)
{
	float			stretch;
	int				xofs, yofs;

	if (scr_stretch_menus.value)
		stretch = min(vid.width / 320.0, vid.height / 200.0);
	else
		stretch = 1;
	xofs = (int)(vid.width - stretch*320) / 2;
	yofs = (int)(vid.height - stretch*200) / 2;

	glDisable (GL_TEXTURE_2D);
	glColor3f (host_basepal[c*3]/255.0,
		host_basepal[c*3+1]/255.0,
		host_basepal[c*3+2]/255.0);

	glBegin (GL_QUADS);

	glVertex2f (stretch * x + xofs, stretch * y + yofs);
	glVertex2f (stretch * (x+w) + xofs, stretch * y + yofs);
	glVertex2f (stretch * (x+w) + xofs, stretch * (y+h) + yofs);
	glVertex2f (stretch * x + xofs, stretch * (y+h) + yofs);

	glEnd ();
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	glDisable (GL_TEXTURE_2D);
	glColor3f (host_basepal[c*3]/255.0,
		host_basepal[c*3+1]/255.0,
		host_basepal[c*3+2]/255.0);

	glBegin (GL_QUADS);

	glVertex2f (x,y);
	glVertex2f (x+w, y);
	glVertex2f (x+w, y+h);
	glVertex2f (x, y+h);

	glEnd ();
	glColor3f (1,1,1);
	glEnable (GL_TEXTURE_2D);
}

/*
================
Draw_FadeScreen -- johnfitz -- major changes
================
*/
void Draw_FadeScreen (void)
{
	extern int screentexture;

	if (0) //stipple
	{
		float s,t;

		GL_Bind(screentexture);
		glBegin (GL_QUADS);

		s = vid.width / 8.0;
		t = vid.height / 8.0;

		glTexCoord2f (0,0);
		glVertex2f (0,0);
		glTexCoord2f (s,0);
		glVertex2f (vid.width, 0);
		glTexCoord2f (s,t);
		glVertex2f (vid.width, vid.height);
		glTexCoord2f (0,t);
		glVertex2f (0, vid.height);

		glEnd ();
	}
	else //blend
	{
		glEnable (GL_BLEND);
		glDisable (GL_ALPHA_TEST);
		glDisable (GL_TEXTURE_2D);
		glColor4f (0, 0, 0, 0.5);
		glBegin (GL_QUADS);

		glVertex2f (0,0);
		glVertex2f (vid.width, 0);
		glVertex2f (vid.width, vid.height);
		glVertex2f (0, vid.height);

		glEnd ();
		glColor4f (1,1,1,1);
		glEnable (GL_TEXTURE_2D);
		glEnable (GL_ALPHA_TEST);
		glDisable (GL_BLEND);
	}
	Sbar_Changed();
}

/*
================
Draw_BeginDisc

Draws the little blue disc in the corner of the screen.
Call before beginning any disc IO.
================
*/
void Draw_BeginDisc (void)
{
	if (!draw_disc)
		return;
	glDrawBuffer  (GL_FRONT);
	Draw_Pic (vid.width - 24, 0, draw_disc);
	glDrawBuffer  (GL_BACK);
}


/*
================
Draw_EndDisc

Erases the disc icon.
Call after completing any disc IO
================
*/
void Draw_EndDisc (void)
{
}

/*
================
GL_Set2D

Setup as if the screen was 320*200
================
*/
void GL_Set2D (void)
{
	glViewport (glx, gly, glwidth, glheight);

	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();
	glOrtho  (0, vid.width, vid.height, 0, -99999, 99999);

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
//	glEnable (GL_BLEND);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);
//	glDisable (GL_ALPHA_TEST);

	glColor4f (1,1,1,1);
}

//==============================================================================
//
//  TEXTURE LOADING
//
//==============================================================================

/*
================
GL_FindTexture
================
*/
int GL_FindTexture (char *identifier)
{
	int		i;
	gltexture_t	*glt;

	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (!strcmp (identifier, glt->identifier))
			return gltextures[i].texnum;
	}

	return -1;
}

/*
================
GL_ResampleTexture -- johnfitz -- rewritten to do use bilinear resample
================
*/
void GL_ResampleTexture (unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight, qboolean alpha)
{
	byte *nwpx, *nepx, *swpx, *sepx, *dest; //don't tell maj about this
	unsigned xfrac, yfrac, x, y, modx, mody, imodx, imody, injump, outjump;
	int i, j;

//double time1, time2;
//time1 = Sys_FloatTime();

	xfrac = (inwidth << 8) / outwidth;
	yfrac = (inheight << 8) / outheight;
	y = outjump = 0;

	for (i=0; i<outheight; i++)
	{
		mody = y & 0xFF;
		imody = 256 - mody;
		injump = (y>>8) * inwidth;
		x = 0;

		for (j=0; j<outwidth; j++)
		{
			modx = x & 0xFF;
			imodx = 256 - modx;

			nwpx = (byte *)(in + (x>>8) + injump);
			nepx = nwpx + 4; 
			swpx = nwpx + inwidth*4;
			sepx = swpx + 4;

			dest = (byte *)(out + outjump + j);

			dest[0] = (nwpx[0]*imodx*imody + nepx[0]*modx*imody + swpx[0]*imodx*mody + sepx[0]*modx*mody)>>16;
			dest[1] = (nwpx[1]*imodx*imody + nepx[1]*modx*imody + swpx[1]*imodx*mody + sepx[1]*modx*mody)>>16;
			dest[2] = (nwpx[2]*imodx*imody + nepx[2]*modx*imody + swpx[2]*imodx*mody + sepx[2]*modx*mody)>>16;
			if (alpha)
				dest[3] = (nwpx[3]*imodx*imody + nepx[3]*modx*imody + swpx[3]*imodx*mody + sepx[3]*modx*mody)>>16;
			else
				dest[3] = 255;
			
			x += xfrac;
		}
		outjump += outwidth;
		y += yfrac;
	}

//time2 = Sys_FloatTime();
//Con_SafePrintf ("GL_ResampleTexture: %i pixels in %f ms\n", outwidth*outheight,(time2-time1)*1000);
}

/*
================
GL_Resample8BitTexture -- JACK
================
*/
void GL_Resample8BitTexture (unsigned char *in, int inwidth, int inheight, unsigned char *out,  int outwidth, int outheight)
{
	int		i, j;
	unsigned	char *inrow;
	unsigned	frac, fracstep;

	fracstep = inwidth*0x10000/outwidth;
	for (i=0 ; i<outheight ; i++, out += outwidth)
	{
		inrow = in + inwidth*(i*inheight/outheight);
		frac = fracstep >> 1;
		for (j=0 ; j<outwidth ; j+=4)
		{
			out[j] = inrow[frac>>16];
			frac += fracstep;
			out[j+1] = inrow[frac>>16];
			frac += fracstep;
			out[j+2] = inrow[frac>>16];
			frac += fracstep;
			out[j+3] = inrow[frac>>16];
			frac += fracstep;
		}
	}
}


/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
void GL_MipMap (byte *in, int width, int height)
{
	int		i, j;
	byte	*out;

	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=8, out+=4, in+=8)
		{
			out[0] = (in[0] + in[4] + in[width+0] + in[width+4])>>2;
			out[1] = (in[1] + in[5] + in[width+1] + in[width+5])>>2;
			out[2] = (in[2] + in[6] + in[width+2] + in[width+6])>>2;
			out[3] = (in[3] + in[7] + in[width+3] + in[width+7])>>2;
		}
	}
}

/*
================
GL_MipMap8Bit

Mipping for 8 bit textures
================
*/
void GL_MipMap8Bit (byte *in, int width, int height)
{
	int		i, j;
	unsigned short     r,g,b;
	byte	*out, *at1, *at2, *at3, *at4;

//	width <<=2;
	height >>= 1;
	out = in;
	for (i=0 ; i<height ; i++, in+=width)
	{
		for (j=0 ; j<width ; j+=2, out+=1, in+=2)
		{
			at1 = (byte *) (d_8to24table + in[0]);
			at2 = (byte *) (d_8to24table + in[1]);
			at3 = (byte *) (d_8to24table + in[width+0]);
			at4 = (byte *) (d_8to24table + in[width+1]);

 			r = (at1[0]+at2[0]+at3[0]+at4[0]); r>>=5;
 			g = (at1[1]+at2[1]+at3[1]+at4[1]); g>>=5;
 			b = (at1[2]+at2[2]+at3[2]+at4[2]); b>>=5;

			out[0] = d_15to8table[(r<<0) + (g<<5) + (b<<10)];
		}
	}
}

/*
===============
Neighbor -- johnfitz -- wrap lookup coords for AlphaEdgeFix

TODO: make this a #define
===============
*/
int Neighbor(int x, int y, int width, int height)
{
	if (x < 0)
		x += width;
	else if (x > width-1)
		x -= width;
	if (y < 0)
		y += height;
	else if (y > height-1)
		y -= height;
	return y * width + x;
}

/*
===============
AlphaEdgeFix -- johnfitz

eliminate pink edges on sprites
operates in place on 32bit data
===============
*/
void AlphaEdgeFix(byte *data, int width, int height)
{
	int i,j,k,ii,jj,p,c,n,b;
	for (i=0; i<height; i++) //for each row
	{
		for (j=0; j<width; j++) //for each pixel
		{
			p = (i*width+j)<<2; //current pixel position in data
			if (data[p+3] == 0) //if pixel is transparent
			{
				for (k=0; k<3; k++) //for each color byte
				{
					n = 9; //number of non-transparent neighbors (include self)
					c = 0; //running total
					for (ii=-1; ii<2; ii++) //for each row of neighbors
					{
						for (jj=-1; jj<2; jj++) //for each pixel in this row of neighbors
						{
							b = Neighbor(j+jj,i+ii,width,height) * 4;
							data[b+3] ? c += data[b+k] : n-- ;
						}
					}
					data[p+k] = n ? (byte)(c/n) : 0; //average of all non-transparent neighbors
				}
			}
		}
	}
}

/*
===============
GL_SafeTextureSize -- johnfitz -- return a size with hardware and user prefs in mind
===============
*/
int GL_SafeTextureSize (int s)
{
	s = Pad(s);
	s >>= (int)gl_picmip.value;
	if ((int)gl_max_size.value > 0)
		s = min((int)gl_max_size.value, s);
	s = min(gl_hardware_maxsize, s);
	return s;
}

/*
===============
GL_Upload32
===============
*/
void GL_Upload32 (unsigned *data, int width, int height,  qboolean mipmap, qboolean alpha)
{
	unsigned	*scaled; //johnfitz -- now dynamic
	int			samples, scaled_width, scaled_height, mark;

	//johnfitz -- use GL_SafeTextureSize
	scaled_width = GL_SafeTextureSize (width); 
	scaled_height = GL_SafeTextureSize (height);
	//johnfitz
	
	//johnfitz -- dynamically alloc scaled
	mark = Hunk_LowMark();
	scaled = Hunk_Alloc(scaled_width * scaled_height * 4);
	//johnfitz

	samples = alpha ? gl_alpha_format : gl_solid_format;

	//johnfitz -- eliminate pink edges on sprites, etc
	if (alpha)
		AlphaEdgeFix((byte *)data, width, height);
	//johnfitz

	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			goto done;
		}
		memcpy (scaled, data, width*height*4);
	}
	else
		GL_ResampleTexture (data, width, height, scaled, scaled_width, scaled_height, alpha); //johnfitz -- extra parameter

	glTexImage2D (GL_TEXTURE_2D, 0, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			glTexImage2D (GL_TEXTURE_2D, miplevel, samples, scaled_width, scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
		}
	}
	done: ;

	if (mipmap)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	Hunk_FreeToLowMark(mark); //johnfitz
}

/*
===============
GL_Upload8_EXT
===============
*/
void GL_Upload8_EXT (byte *data, int width, int height,  qboolean mipmap, qboolean alpha) 
{
	int			i, p, samples, scaled_width, scaled_height, mark;
    byte		*scaled; //johnfitz -- now dynamic

	//johnfitz -- use GL_SafeTextureSize
	scaled_width = GL_SafeTextureSize (width); 
	scaled_height = GL_SafeTextureSize (height);
	//johnfitz
	
	//johnfitz -- dynamically alloc scaled
	mark = Hunk_LowMark();
	scaled = Hunk_Alloc(scaled_width * scaled_height * 4);
	//johnfitz

	samples = 1; // alpha ? gl_alpha_format : gl_solid_format;

	texels += scaled_width * scaled_height;

	if (scaled_width == width && scaled_height == height)
	{
		if (!mipmap)
		{
			glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX , GL_UNSIGNED_BYTE, data);
			goto done;
		}
		memcpy (scaled, data, width*height);
	}
	else
		GL_Resample8BitTexture (data, width, height, scaled, scaled_width, scaled_height);

	glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
	if (mipmap)
	{
		int		miplevel;

		miplevel = 0;
		while (scaled_width > 1 || scaled_height > 1)
		{
			GL_MipMap8Bit ((byte *)scaled, scaled_width, scaled_height);
			scaled_width >>= 1;
			scaled_height >>= 1;
			if (scaled_width < 1)
				scaled_width = 1;
			if (scaled_height < 1)
				scaled_height = 1;
			miplevel++;
			glTexImage2D (GL_TEXTURE_2D, miplevel, GL_COLOR_INDEX8_EXT, scaled_width, scaled_height, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, scaled);
		}
	}
done: ;


	if (mipmap)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
	}

	Hunk_FreeToLowMark(mark); //johnfitz
}

/*
===============
GL_Upload8
===============
*/
void GL_Upload8 (byte *data, int width, int height,  qboolean mipmap, qboolean alpha)
{
	unsigned	*trans; //johnfitz -- now dynamically allocated
	int			i, s, p, mark;
	qboolean	noalpha;

	s = width*height;

	mark = Hunk_LowMark(); //johnfitz
	trans = Hunk_Alloc (s*4); //johnfitz

	// if there are no transparent pixels, make it a 3 component
	// texture even if it was specified as otherwise
	if (alpha)
	{
		noalpha = true;
		for (i=0 ; i<s ; i++)
		{
			p = data[i];
			if (p == 255)
				noalpha = false;
			trans[i] = d_8to24table[p];
		}

		if (alpha && noalpha)
			alpha = false;
	}
	else
	{
		if (s&3)
			Sys_Error ("GL_Upload8: s&3");

		for (i=0 ; i<s ; i+=4)
		{
			trans[i] = d_8to24table[data[i]];
			trans[i+1] = d_8to24table[data[i+1]];
			trans[i+2] = d_8to24table[data[i+2]];
			trans[i+3] = d_8to24table[data[i+3]];
		}
	}

	if (VID_Is8bit() && !alpha && (data!=scrap_texels[0]))
 		GL_Upload8_EXT (data, width, height, mipmap, alpha);
	else
		GL_Upload32 (trans, width, height, mipmap, alpha);

	Hunk_FreeToLowMark (mark); //johnfitz
}

/*
================
GL_LoadTexture  -- johnfitz -- lots of little changes
================
*/
int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)
{
	static int		argh, argh2; //static instead of global
	qboolean		noalpha;
	int				i, p, s;
	gltexture_t		*glt;
	unsigned short	crc;
	char			buffer[64];

	if (!identifier[0])
	{
		Con_DPrintf("GL_LoadTexture: no identifier\n");
		sprintf (buffer, "%s_%i", "argh", argh); //becuase writing to identifier is not safe
		identifier = buffer;
		argh++;
	}

	crc = CRC_Block(data, width*height*1);

	for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
	{
		if (!strcmp (identifier, glt->identifier))
		{
			if (crc != glt->crc || width != glt->width || height != glt->height)
			{
				Con_DPrintf("GL_LoadTexture: cache mismatch\n");
				sprintf (buffer, "%s_%i", identifier, argh2); //becuase writing to identifier is not safe
				identifier = buffer;
				argh2++;
				goto GL_LoadTexture_setup;
			}
			return glt->texnum;
		}
	}

GL_LoadTexture_setup:

	if (numgltextures == MAX_GLTEXTURES)
		Sys_Error ("GL_LoadTexture: numgltextures == MAX_GLTEXTURES");

	glt = &gltextures[numgltextures];
	numgltextures++;

	strcpy (glt->identifier, identifier);
	glt->texnum = texture_extension_number;
	texture_extension_number++;

	glt->crc			= crc;
	glt->width			= width;
	glt->height			= height;
	glt->mipmap			= mipmap;

	if (!isDedicated)
	{
			 GL_Bind(glt->texnum);
			 GL_Upload8 (data, width, height, mipmap, alpha);
	}

	return glt->texnum;
}

/*
================
Pad -- johnfitz -- return smallest power of two greater than or equal to x
================
*/
int Pad(int x)
{
	int i;
	for (i = 1; i < x; i<<=1);
	return i;
}

/*
================
GL_PadImage -- johnfitz -- return image padded up to power-of-two dimentions
================
*/
void GL_PadImage(byte *in, int width, int height, byte *out)
{
	int i,j,w,h;
	w = Pad(width);
	h = Pad(height);

	for (i = 0; i < h; i++) //each row
	{
		for (j = 0; j < w; j++) //each pixel in that row
		{
			if (i < height && j < width)
				out[i*w+j] = in[i*width+j];
			else
				out[i*w+j] = 255;
		}
	}
}

/*
================
GL_LoadPaddedTexture -- johnfitz -- pad image before continuing
================
*/
int GL_LoadPaddedTexture (char *name, int width, int height, byte *in, qboolean mipmap, qboolean alpha)
{
	int texnum, mark;
	byte *padded;

	mark = Hunk_LowMark();
	padded = Hunk_Alloc(Pad(width) * Pad(height));

	GL_PadImage(in, width, height, padded);
	texnum = GL_LoadTexture (name, Pad(width), Pad(height), padded, mipmap, alpha);
	
	Hunk_FreeToLowMark(mark);

	return texnum;
}

/*
================
GL_LoadPicTexture -- johnfitz
================
*/
int GL_LoadPicTexture (qpic_t *pic)
{
	return GL_LoadPaddedTexture ("", pic->width, pic->height, pic->data, false, true);
}