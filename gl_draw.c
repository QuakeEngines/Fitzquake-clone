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

// draw.c -- 2d drawing

#include "quakedef.h"

//extern unsigned char d_15to8table[65536]; //johnfitz -- never used

cvar_t		scr_conalpha = {"scr_conalpha", "1"}; //johnfitz

qpic_t		*draw_disc;
qpic_t		*draw_backtile;

gltexture_t *translate_texture, *char_texture; //johnfitz
gltexture_t *pic_stipple_texture, *pic_ins_texture, *pic_ovr_texture, *pic_crosshair_texture; //johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float		sl, tl, sh, th;
} glpic_t;

int currentcanvas = -1; //johnfitz -- for GL_SetCanvas

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

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

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH*BLOCK_HEIGHT]; //johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	scrap_dirty;
gltexture_t	*scrap_textures[MAX_SCRAPS]; //johnfitz

/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/ 
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

	Sys_Error ("Scrap_AllocBlock: full"); //johnfitz -- correct function name
	return 0; //johnfitz -- shut up compiler
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr_LoadImage8
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int	i;
	
	for (i=0; i<MAX_SCRAPS; i++)
	{
		sprintf (name, "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage8 (NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, scrap_texels[i], 
			TEXPREF_ALPHA | TEXPREF_UNIQUE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
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
		gl->gltexture = scrap_textures[texnum]; //johnfitz -- changed to an array
		//johnfitz -- no longer go from 0.01 to 0.99
		gl->sl = x/(float)BLOCK_WIDTH;
		gl->sh = (x+p->width)/(float)BLOCK_WIDTH;
		gl->tl = y/(float)BLOCK_WIDTH;
		gl->th = (y+p->height)/(float)BLOCK_WIDTH;
	}
	else
	{
		gl->gltexture = TexMgr_LoadImage8 (NULL, name, p->width, p->height, p->data, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP); //johnfitz -- TexMgr_LoadImage8
		gl->sl = 0;
		gl->sh = (float)p->width/(float)TexMgr_PadConditional(p->width); //johnfitz
		gl->tl = 0;
		gl->th = (float)p->height/(float)TexMgr_PadConditional(p->height); //johnfitz
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
	gl->gltexture = TexMgr_LoadImage8 (NULL, path, dat->width, dat->height, dat->data, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP); //johnfitz -- TexMgr_LoadImage8
	gl->sl = 0;
	gl->sh = (float)dat->width/(float)TexMgr_PadConditional(dat->width); //johnfitz
	gl->tl = 0;
	gl->th = (float)dat->height/(float)TexMgr_PadConditional(dat->height); //johnfitz

	return &pic->pic;
}

/*
===============
Draw_LoadConchars -- johnfitz -- load specially becuase the transparent color is black instead of pink
===============
*/
void Draw_LoadConchars (void)
{
	int			i;
	byte		*data;

	data = W_GetLumpName ("conchars");

	//correct transparent color
	for (i = 0; i < 128*128; i++)
		if (data[i] == 0)
			data[i] = 255;

	char_texture = TexMgr_LoadImage8 (NULL, "conchars", 128, 128, data, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP);
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

//johnfitz -- new pics
byte pic_stipple[8][8] =
{
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
	{255,  0,  0,  0,255,  0,  0,  0},
	{  0,  0,255,  0,  0,  0,255,  0},
};

byte pic_ovr[8][8] =
{	
	{255,255,255,255,255,255,255,255},
	{255,  7,  7,  7,  7,  7,  7,255},
	{255,  8,  8,  8,  8,  8,  8,  2},
	{255,  9,  9,  9,  9,  9,  9,  2},
	{255, 10, 10, 10, 10, 10, 10,  2},
	{255, 11, 11, 11, 11, 11, 11,  2},
	{255, 12, 12, 12, 12, 12, 12,  2},
	{255,255,  2,  2,  2,  2,  2,  2},
};

byte pic_ins[8][8] =
{	
	{255,255,255,255,255,255,255,255},
	{255,255,255,255,255,255,255,255},
	{255,255,255,255,255,255,255,255},
	{255,255,255,255,255,255,255,255},
	{255,255,255,255,255,255,255,255},
	{255,255,255,255,255,255,255,255},
	{255, 12, 12, 12, 12, 12, 12,255},
	{255,255,  2,  2,  2,  2,  2,  2},
};

byte pic_crosshair[8][8] =
{	
	{255,255,255,255,255,255,255,255},
	{255,255,255,  8,  9,255,255,255},
	{255,255,255,  6,  8,  2,255,255},
	{255,  6,  8,  8,  6,  8,  8,255},
	{255,255,  2,  8,  8,  2,  2,  2},
	{255,255,255,  7,  8,  2,255,255},
	{255,255,255,255,  2,  2,255,255},
	{255,255,255,255,255,255,255,255},
};
//johnfitz

/*
===============
Draw_InitPics -- johnfitz -- init internal pics
===============
*/
void Draw_InitPics (void)
{
	int flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP;

	// stipple texture
	pic_stipple_texture = TexMgr_LoadImage8 (NULL, "pic_stipple_texture", 8, 8, &pic_stipple[0][0], flags);

	// text insert cursor
	pic_ins_texture = TexMgr_LoadImage8 (NULL, "pic_ins_texture", 8, 8, &pic_ins[0][0], flags);

	// text overwrite cursor
	pic_ovr_texture = TexMgr_LoadImage8 (NULL, "pic_ovr_texture", 8, 8, &pic_ovr[0][0], flags);

	// crosshair
	pic_crosshair_texture = TexMgr_LoadImage8 (NULL, "pic_crosshair_texture", 8, 8, &pic_crosshair[0][0], flags);

	// mouse cursor
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	gltexture_t	*glt;
	cachepic_t	*pic;
	int			i;

	// empty scrap and reallocate gltextures
	memset(&scrap_allocated, 0, sizeof(scrap_allocated));
	memset(&scrap_texels, 255, sizeof(scrap_texels));
	Scrap_Upload (); //creates 2 empty gltextures

	// reload wad pics
	W_LoadWadFile ("gfx.wad");
	Draw_LoadPics ();
//	Draw_InitPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&scr_conalpha, NULL);
	
	// clear scrap and allocate gltextures
	memset(&scrap_allocated, 0, sizeof(scrap_allocated));
	memset(&scrap_texels, 255, sizeof(scrap_texels));
	Scrap_Upload (); //creates 2 empty textures

	// load game pics
	Draw_LoadPics ();

	// init internal pics
//	Draw_InitPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

/*
================
Draw_CharacterQuad -- johnfitz -- seperate function to spit out verts
================
*/
void Draw_CharacterQuad (int x, int y, char num)
{	
	int				row, col;
	float			frow, fcol, size;

	row = num>>4;
	col = num&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	glTexCoord2f (fcol, frow);
	glVertex2f (x, y);
	glTexCoord2f (fcol + size, frow);
	glVertex2f (x+8, y);
	glTexCoord2f (fcol + size, frow + size);
	glVertex2f (x+8, y+8);
	glTexCoord2f (fcol, frow + size);
	glVertex2f (x, y+8);
}

/*
================
Draw_Character -- johnfitz -- modified to call Draw_CharacterQuad
================
*/
void Draw_Character (int x, int y, int num)
{	
	if (y <= -8)
		return;			// totally off screen
	
	num &= 255;

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	Draw_CharacterQuad (x, y, (char) num);

	glEnd ();
}

/*
================
Draw_String -- johnfitz -- modified to call Draw_CharacterQuad
================
*/
void Draw_String (int x, int y, char *str)
{
	if (y <= -8)
		return;			// totally off screen

	GL_Bind (char_texture);
	glBegin (GL_QUADS);

	while (*str)
	{
		Draw_CharacterQuad (x, y, *str);
		str++;
		x += 8;
	}

	glEnd ();
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	glpic_t			*gl;
	
	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *)pic->data;
	GL_Bind (gl->gltexture);
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

	c = pic->width * pic->height;
	
	//FIXME: shouldn't need to upload this every frame

	dest = trans;
	for (v=0 ; v<64 ; v++, dest += 64)
	{
		src = &menuplyr_pixels[ ((v*pic->height)>>6) *pic->width];
		for (u=0 ; u<64 ; u++)
		{
			p = src[(u*pic->width)>>6];
			if (p == 255)
				dest[u] = 0; //johnfitz -- black edges are better than pink
			else
				dest[u] = d_8to24table[translation[p]];
		}
	}

	translate_texture = TexMgr_LoadImage32 (NULL, "translate_texture", 64, 64, trans, TEXPREF_ALPHA); //johnfitz
	GL_Bind (translate_texture);

	glColor4f (1,1,1,1);
	glBegin (GL_QUADS);
	glTexCoord2f (0, 0);
	glVertex2f (x, y);
	glTexCoord2f (1, 0);
	glVertex2f (x+pic->width, y);
	glTexCoord2f (1, 1);
	glVertex2f (x+pic->width, y+pic->height);
	glTexCoord2f (0, 1);
	glVertex2f (x, y+pic->height);
	glEnd ();
	//johnfitz
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (void)
{
	qpic_t *pic;
	float alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

//	GL_SetCanvas (CANVAS_CONSOLE); //in case this is called from weird places

	if (alpha > 0.0)
	{
		if (alpha < 1.0)
		{
			glEnable (GL_BLEND);
			glColor4f (1,1,1,alpha);
			glDisable (GL_ALPHA_TEST);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		}

		Draw_Pic (0, 0, pic);

		if (alpha < 1.0)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glEnable (GL_ALPHA_TEST);
			glDisable (GL_BLEND);
			glColor4f (1,1,1,1);
		}
	}
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
	glpic_t	*gl;

	gl = (glpic_t *)draw_backtile->data;

	glColor3f (1,1,1);
	GL_Bind (gl->gltexture);
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
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	byte *pal = (byte *)d_8to24table; //johnfitz -- use d_8to24table instead of host_basepal

	glDisable (GL_TEXTURE_2D);
	glColor3f (pal[c*4]/255.0, pal[c*4+1]/255.0, pal[c*4+2]/255.0);

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
Draw_FadeScreen -- johnfitz -- revised
================
*/
void Draw_FadeScreen (void)
{
	GL_SetCanvas (CANVAS_DEFAULT);

	glEnable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_TEXTURE_2D);
	glColor4f (0, 0, 0, 0.5);
	glBegin (GL_QUADS);

	glVertex2f (0,0);
	glVertex2f (glwidth, 0);
	glVertex2f (glwidth, glheight);
	glVertex2f (0, glheight);

	glEnd ();
	glColor4f (1,1,1,1);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
	glDisable (GL_BLEND);

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
	int previous_canvas = currentcanvas; //johnfitz

	if (!draw_disc)
		return;

	GL_SetCanvas (CANVAS_DEFAULT); //johnfitz
	glDrawBuffer  (GL_FRONT);
	Draw_Pic (glwidth - 24, 0, draw_disc);
	glDrawBuffer  (GL_BACK);
	GL_SetCanvas (previous_canvas); //johnfitz
}

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (int canvastype)
{
	float s, w;
	int lines;

	if (canvastype == currentcanvas)
		return;
	
	currentcanvas = canvastype;

	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();

	switch(canvastype)
	{
	case CANVAS_DEFAULT:
		glOrtho (0, glwidth, glheight, 0, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		glOrtho (0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;
	case CANVAS_MENU:
		s = min ((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP (1.0, scr_menuscale.value, s);
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx + (glwidth - 320*s) / 2, gly + (glheight - 200*s) / 2, 320*s, 200*s);
		break;
	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		if (cl.gametype == GAME_DEATHMATCH)
		{
			glOrtho (0, glwidth / s, 48, 0, -99999, 99999);
			glViewport (glx, gly, glwidth, 48*s);
		}
		else
		{
			glOrtho (0, 320, 48, 0, -99999, 99999);
			glViewport (glx + (glwidth - 320*s) / 2, gly, 320*s, 48*s);
		}
		break;
	case CANVAS_WARPIMAGE:
		glOrtho (0, 128, 0, 128, -99999, 99999);
		glViewport (glx, gly+glheight-gl_warpimagesize, gl_warpimagesize, gl_warpimagesize);
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();
}

/*
================
GL_Set2D -- johnfitz -- rewritten
================
*/
void GL_Set2D (void)
{
	currentcanvas = -1;
	GL_SetCanvas (CANVAS_DEFAULT);

	glDisable (GL_DEPTH_TEST);
	glDisable (GL_CULL_FACE);
	glDisable (GL_BLEND);
	glEnable (GL_ALPHA_TEST);
	glColor4f (1,1,1,1);
}
