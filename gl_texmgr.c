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

//gl_texmgr.c -- fitzquake's texture manager. manages opengl texture images

#include "quakedef.h"

//#include "imdebug-0.931b-bin/imdebug.h" //TEST

cvar_t		gl_max_size = {"gl_max_size", "0"};
cvar_t		gl_picmip = {"gl_picmip", "0"};
int			gl_hardware_maxsize;
int			gl_solid_format = 3;
int			gl_alpha_format = 4;

#define	MAX_GLTEXTURES	1024
gltexture_t	*active_gltextures, *free_gltextures;
int numgltextures;

/*
================================================================================

	COMMANDS

================================================================================
*/

typedef struct
{
	int	magfilter;
	int minfilter;
	char *name;
} glmode_t;
glmode_t modes[] = {
	{GL_NEAREST, GL_NEAREST,				"GL_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST,	"GL_NEAREST_MIPMAP_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_LINEAR,	"GL_NEAREST_MIPMAP_LINEAR"},
	{GL_LINEAR,  GL_LINEAR,					"GL_LINEAR"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_NEAREST,	"GL_LINEAR_MIPMAP_NEAREST"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_LINEAR,	"GL_LINEAR_MIPMAP_LINEAR"},
};
#define NUM_GLMODES 6
int gl_texturemode = 5; // bilinear

/*
===============
TexMgr_SetFilterModes
===============
*/
void TexMgr_SetFilterModes (gltexture_t *glt)
{
	GL_Bind (glt);

	if (glt->flags & TEXPREF_NEAREST)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else if (glt->flags & TEXPREF_LINEAR)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	else if (glt->flags & TEXPREF_MIPMAP)
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, modes[gl_texturemode].magfilter);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, modes[gl_texturemode].minfilter);
	}
	else
	{
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, modes[gl_texturemode].magfilter);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, modes[gl_texturemode].magfilter);
	}
}

/*
===============
TexMgr_TextureMode_f
===============
*/
void TexMgr_TextureMode_f (void)
{
	gltexture_t	*glt;
	char *arg;
	int i;

	switch (Cmd_Argc())
	{
	case 1:
		Con_Printf ("\"gl_texturemode\" is \"%s\"\n", modes[gl_texturemode].name);
		break;
	case 2:
		arg = Cmd_Argv(1);
		if (arg[0] == 'G' || arg[0] == 'g')
		{
			for (i=0; i<NUM_GLMODES; i++)
				if (!stricmp (modes[i].name, arg))
				{
					gl_texturemode = i;
					goto stuff;
				}
			Con_Printf ("\"%s\" is not a valid texturemode\n", arg);
			return;
		}
		else
		{
			i = atoi(arg);
			if (i > NUM_GLMODES || i < 1)
			{
				Con_Printf ("\"%s\" is not a valid texturemode\n", arg);
				return;
			}
			gl_texturemode = i - 1;
		}

stuff:
		for (glt=active_gltextures; glt; glt=glt->next)
			TexMgr_SetFilterModes (glt);

		Sbar_Changed (); //sbar graphics need to be redrawn with new filter mode

		break;
	}
}

/*
===============
TexMgr_DescribeTextureModes_f -- report available texturemodes
===============
*/
void TexMgr_DescribeTextureModes_f (void)
{
	int i;

	for (i=0; i<NUM_GLMODES; i++)
		Con_SafePrintf ("   %2i: %s\n", i + 1, modes[i].name);

	Con_Printf ("%i modes\n", i);
}

/*
===============
TexMgr_Imagelist_f -- report loaded textures
===============
*/
void TexMgr_Imagelist_f (void)
{
	float mb;
	float texels = 0;
	gltexture_t	*glt;

	for (glt=active_gltextures; glt; glt=glt->next)
	{
		Con_SafePrintf ("   %4i x%4i %s\n", glt->width, glt->height, glt->name);
		if (glt->flags & TEXPREF_MIPMAP)
			texels += glt->width * glt->height * 4.0f / 3.0f;
		else
			texels += (glt->width * glt->height);
	}

	mb = texels * 4.0f / 0x100000; //FIXME: assumes 32bpp
	Con_Printf ("%i textures %i pixels %1.1f megabytes\n", numgltextures, (int)texels, mb);
}

/*
===============
TexMgr_FrameUsage -- report texture memory usage for this frame
===============
*/
float TexMgr_FrameUsage (void)
{
	float mb;
	float texels = 0;
	gltexture_t	*glt;

	for (glt=active_gltextures; glt; glt=glt->next)
		if (glt->visframe == r_framecount)
		{
			if (glt->flags & TEXPREF_MIPMAP)
				texels += glt->width * glt->height * 4.0f / 3.0f;
			else
				texels += (glt->width * glt->height);
		}

	mb = texels * 4.0f / 0x100000; //FIXME: assumes 32bpp
	return mb;
}

/*
================================================================================

	TEXTURE MANAGER

================================================================================
*/

/*
================
TexMgr_FindTexture
================
*/
gltexture_t *TexMgr_FindTexture (model_t *owner, char *name)
{
	gltexture_t	*glt;

	if (name)
	{
		for (glt=active_gltextures; glt; glt=glt->next)
			if (glt->owner == owner && !strcmp (glt->name, name))
				return glt;
	}

	return NULL;
}

/*
================
TexMgr_NewTexture
================
*/
gltexture_t *TexMgr_NewTexture (void)
{
	gltexture_t *glt;

	if (numgltextures == MAX_GLTEXTURES)
		Sys_Error("numgltextures == MAX_GLTEXTURES\n");

	glt = free_gltextures;
	free_gltextures = glt->next;
	glt->next = active_gltextures;
	active_gltextures = glt;

	glGenTextures(1, &glt->texnum);
	numgltextures++;
	return glt;
}

/*
================
TexMgr_FreeTexture
================
*/
void TexMgr_FreeTexture (gltexture_t *kill)
{
	gltexture_t *glt;

	if (kill == NULL)
		return;

	glDeleteTextures(1, &kill->texnum);

	if (active_gltextures == kill)
	{
		active_gltextures = kill->next;
		kill->next = free_gltextures;
		free_gltextures = kill;
		numgltextures--;
		return;
	}

	for (glt = active_gltextures; glt; glt = glt->next)
		if (glt->next == kill)
		{
			glt->next = kill->next;
			kill->next = free_gltextures;
			free_gltextures = kill;
			numgltextures--;
			return;
		}
}

/*
================
TexMgr_FreeTextures

compares each bit in "flags" to the one in glt->flags only if that bit is active in "mask"
================
*/
void TexMgr_FreeTextures (int flags, int mask)
{
	gltexture_t *glt, *kill;

	//clear out the front of the list
	while (active_gltextures && (active_gltextures->flags & mask) == (flags & mask))
	{
		kill = active_gltextures;

		numgltextures--;
		glDeleteTextures(1, &kill->texnum);

		active_gltextures = active_gltextures->next;
		kill->next = free_gltextures;
		free_gltextures = kill;
	}

	//clear out the rest of the list (if any are left)
	for (glt = active_gltextures; glt; )
	{
		kill = glt->next;
		if (kill && (kill->flags & mask) == (flags & mask))
		{
			numgltextures--;
			glDeleteTextures(1, &kill->texnum);

			glt->next = kill->next;
			kill->next = free_gltextures;
			free_gltextures = kill;
		}
		else
			glt = glt->next;
	}
}

/*
================
TexMgr_FreeTexturesForOwner
================
*/
void TexMgr_FreeTexturesForOwner (model_t *owner)
{
	gltexture_t *glt, *kill;

	//clear out the front of the list
	while (active_gltextures && active_gltextures->owner == owner)
	{
		kill = active_gltextures;

		numgltextures--;
		glDeleteTextures(1, &kill->texnum);

		active_gltextures = active_gltextures->next;
		kill->next = free_gltextures;
		free_gltextures = kill;
	}

	//clear out the rest of the list (if any are left)
	for (glt = active_gltextures; glt; )
	{
		kill = glt->next;
		if (kill && kill->owner == owner)
		{
			numgltextures--;
			glDeleteTextures(1, &kill->texnum);

			glt->next = kill->next;
			kill->next = free_gltextures;
			free_gltextures = kill;
		}
		else
			glt = glt->next;
	}
}

/*
================================================================================

	INIT

================================================================================
*/

/*
=================
TexMgr_LoadPalette -- johnfitz -- was VID_SetPalette, moved here, renamed, gutted
=================
*/
void TexMgr_LoadPalette (void)
{
	unsigned v;
	unsigned *table;
	byte r,g,b;
	byte *pal;
	int i, mark;
	FILE *f;

	COM_FOpenFile ("gfx/palette.lmp", &f);
	if (!f)
		Sys_Error ("Couldn't load gfx/palette.lmp");

	mark = Hunk_LowMark ();
	pal = Hunk_Alloc (768);
	fread (pal, 1, 768, f);
	fclose(f);

	table = d_8to24table;
	for (i=0 ; i<256 ; i++, pal+=3)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		v = (r<<0) + (g<<8) + (b<<16) + (255<<24);
		*table++ = v;
	}
	d_8to24table[255] &= 0xffffff;	// 255 is transparent

	Hunk_FreeToLowMark (mark);
}

/*
================
TexMgr_NewGame
================
*/
void TexMgr_NewGame (void)
{
	TexMgr_FreeTextures (0, TEXPREF_PERSIST); //deletes all textures where TEXPREF_PERSIST is unset
	TexMgr_LoadPalette ();
}

/*
================
TexMgr_Init

must be called before any texture loading
================
*/
void TexMgr_Init (void)
{
	int i, mark;
	byte *data;

	// init texture list
	free_gltextures = (gltexture_t *) Hunk_AllocName (MAX_GLTEXTURES * sizeof(gltexture_t), "gltextures");
	active_gltextures = NULL;
	for (i=0; i<MAX_GLTEXTURES; i++)
		free_gltextures[i].next = &free_gltextures[i+1];
	free_gltextures[i].next = NULL;
	numgltextures = 0;

	// palette
	TexMgr_LoadPalette ();
	
	Cvar_RegisterVariable (&gl_max_size, NULL);
	Cvar_RegisterVariable (&gl_picmip, NULL);
	Cmd_AddCommand ("gl_texturemode", &TexMgr_TextureMode_f);
	Cmd_AddCommand ("gl_describetexturemodes", &TexMgr_DescribeTextureModes_f);
	Cmd_AddCommand ("imagelist", &TexMgr_Imagelist_f);

	// poll max size from hardware
	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &gl_hardware_maxsize);

	// load notexture image
	mark = Hunk_LowMark ();
	data = Hunk_Alloc (32*32);
	for (i=0; i<512; i++) data[i] = (i&16) ? 255 : 0;
	for (   ; i<1024; i++) data[i] = (i&16) ? 0 : 255;
	notexture = TexMgr_LoadImage8 (NULL, "notexture", 32, 32, data, TEXPREF_NEAREST | TEXPREF_PERSIST);
	Hunk_FreeToLowMark (mark);

	//set safe size for warpimages
	gl_warpimagesize = TexMgr_SafeTextureSize (512);
	while (gl_warpimagesize > vid.width)
		gl_warpimagesize >>= 1;
	while (gl_warpimagesize > vid.height)
		gl_warpimagesize >>= 1;
}

/*
================================================================================

	IMAGE LOADING

================================================================================
*/

/*
================
TexMgr_Pad -- return smallest power of two greater than or equal to s
================
*/
int TexMgr_Pad (int s)
{
	int i;
	for (i = 1; i < s; i<<=1);
	return i;
}

/*
===============
TexMgr_SafeTextureSize -- return a size with hardware and user prefs in mind
===============
*/
int TexMgr_SafeTextureSize (int s)
{
	s = TexMgr_Pad(s);
	if ((int)gl_max_size.value > 0)
		s = min(TexMgr_Pad((int)gl_max_size.value), s);
	s = min(gl_hardware_maxsize, s);
	return s;
}

/*
================
TexMgr_PadConditional -- only pad if a texture of that size would be padded. (used for tex coords)
================
*/
int TexMgr_PadConditional (int s)
{	
	if (s < TexMgr_SafeTextureSize(s))
		return TexMgr_Pad(s);
	else
		return s;
}

/*
================
TexMgr_MipMapW
================
*/
unsigned *TexMgr_MipMapW (unsigned *data, int width, int height)
{
	int		i, size;
	byte	*out, *in;

	out = in = (byte *)data;
	size = (width*height)>>1;

	for (i=0; i<size; i++, out+=4, in+=8)
	{
		out[0] = (in[0] + in[4])>>1;
		out[1] = (in[1] + in[5])>>1;
		out[2] = (in[2] + in[6])>>1;
		out[3] = (in[3] + in[7])>>1;
	}

	return data;
}

/*
================
TexMgr_MipMapH
================
*/
unsigned *TexMgr_MipMapH (unsigned *data, int width, int height)
{
	int		i, j;
	byte	*out, *in;

	out = in = (byte *)data;
	height>>=1;
	width<<=2;

	for (i=0; i<height; i++, in+=width)
		for (j=0; j<width; j+=4, out+=4, in+=4)
		{
			out[0] = (in[0] + in[width+0])>>1;
			out[1] = (in[1] + in[width+1])>>1;
			out[2] = (in[2] + in[width+2])>>1;
			out[3] = (in[3] + in[width+3])>>1;
		}

	return data;
}

/*
================
TexMgr_ResampleTexture -- bilinear resample
================
*/
unsigned *TexMgr_ResampleTexture (unsigned *in, int inwidth, int inheight, qboolean alpha)
{
	byte *nwpx, *nepx, *swpx, *sepx, *dest; //don't tell maj about this
	unsigned xfrac, yfrac, x, y, modx, mody, imodx, imody, injump, outjump;
	unsigned *out;
	int i, j, outwidth, outheight;

	if (inwidth == TexMgr_Pad(inwidth) && inheight == TexMgr_Pad(inheight))
		return in;

	outwidth = TexMgr_Pad(inwidth);
	outheight = TexMgr_Pad(inheight);
	out = Hunk_Alloc(outwidth*outheight*4);

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

	return out;
}

/*
================
TexMgr_LoadImagePixels
================
*/
void TexMgr_LoadImagePixels (gltexture_t *glt, unsigned *data)
{
	int	internalformat,	miplevel, mipwidth, mipheight, picmip;

	// resample up
	data = TexMgr_ResampleTexture (data, glt->width, glt->height, glt->flags & TEXPREF_ALPHA);
	glt->width = TexMgr_Pad(glt->width);
	glt->height = TexMgr_Pad(glt->height);

	// mipmap down
	picmip = (!(glt->flags & TEXPREF_NOPICMIP)) ? (int)gl_picmip.value : 0;
	picmip = max (picmip, 0);
	mipwidth = TexMgr_SafeTextureSize (glt->width >> picmip);
	mipheight = TexMgr_SafeTextureSize (glt->height >> picmip);
	while (glt->width > mipwidth)
	{
		TexMgr_MipMapW (data, glt->width, glt->height);
		glt->width >>= 1;
	}
	while (glt->height > mipheight)
	{
		TexMgr_MipMapH (data, glt->width, glt->height);
		glt->height >>= 1;
	}

	// upload
	GL_Bind (glt);
	internalformat = (glt->flags & TEXPREF_ALPHA) ? gl_alpha_format : gl_solid_format;
	glTexImage2D (GL_TEXTURE_2D, 0, internalformat, glt->width, glt->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	// upload mipmaps
	if (glt->flags & TEXPREF_MIPMAP)
	{
		mipwidth = glt->width;
		mipheight = glt->height;

		for (miplevel=1; mipwidth > 1 || mipheight > 1; miplevel++)
		{
			if (mipwidth > 1)
			{
				TexMgr_MipMapW (data, mipwidth, mipheight);
				mipwidth >>= 1;
			}
			if (mipheight > 1)
			{
				TexMgr_MipMapH (data, mipwidth, mipheight);
				mipheight >>= 1;
			}
			glTexImage2D (GL_TEXTURE_2D, miplevel, internalformat, mipwidth, mipheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
	}
	
	// set filter modes
	TexMgr_SetFilterModes (glt);
}

/*
================
TexMgr_LoadImage32 -- handles 32bit source data
================
*/
gltexture_t *TexMgr_LoadImage32 (model_t *owner, char *name, int width, int height, unsigned *data, int flags)
{
	unsigned short crc;
	gltexture_t *glt;
	int mark;

	if (isDedicated)
		return NULL;

	// cache check
	crc = CRC_Block((byte *)data, width*height*4);
	if ((flags & TEXPREF_UNIQUE) && (glt = TexMgr_FindTexture (owner, name))) 
	{
		if (glt->crc == crc)
			return glt;
	}
	else
		glt = TexMgr_NewTexture ();

	// copy data
	glt->owner = owner;
	strcpy (glt->name, name);
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->crc = crc;

	mark = Hunk_LowMark();

	// upload it
	TexMgr_LoadImagePixels (glt, data);

	Hunk_FreeToLowMark(mark);

	return glt;
}

/*
===============
WrapCoords -- wrap lookup coords for AlphaEdgeFix

TODO: make this a macro
===============
*/
int WrapCoords(int x, int y, int width, int height)
{
	if (x < 0)
		x += width;
	else if (x >= width)
		x -= width;
	if (y < 0)
		y += height;
	else if (y >= height)
		y -= height;
	return y * width + x;
}

/*
===============
TexMgr_AlphaEdgeFix

eliminate pink edges on sprites
operates in place on 32bit data
===============
*/
void TexMgr_AlphaEdgeFix(byte *data, int width, int height)
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
							b = WrapCoords(j+jj,i+ii,width,height) * 4;
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
================
TexMgr_8to32
================
*/
unsigned *TexMgr_8to32 (byte *in, int pixels)
{
	int i;
	unsigned *out, *data;

	out = data = Hunk_Alloc(pixels*4);

	for (i=0 ; i<pixels ; i++)
		*out++ = d_8to24table[*in++];

	return data;
}

/*
================
TexMgr_PadImageW -- return image with width padded up to power-of-two dimentions
================
*/
byte *TexMgr_PadImageW (byte *in, int width, int height)
{
	int i, j, outwidth;
	byte *out, *data;

	if (width == TexMgr_Pad(width))
		return in;

	outwidth = TexMgr_Pad(width);

	out = data = Hunk_Alloc(outwidth*height);
	
	for (i=0; i<height; i++)
	{
		for (j=0; j<width; j++)
			*out++ = *in++;
		for (   ; j<outwidth; j++)
			*out++ = 255;
	}

	return data;
}

/*
================
TexMgr_PadImageH -- return image with height padded up to power-of-two dimentions
================
*/
byte *TexMgr_PadImageH (byte *in, int width, int height)
{
	int i, srcpix, dstpix;
	byte *data, *out;

	if (height == TexMgr_Pad(height))
		return in;

	srcpix = width * height;
	dstpix = width * TexMgr_Pad(height);

	out = data = Hunk_Alloc(dstpix);

	for (i=0; i<srcpix; i++)
		*out++ = *in++;
	for (   ; i<dstpix; i++)
		*out++ = 255;

	return data;
}

/*
================
TexMgr_LoadImage8 -- handles 8bit source data
================
*/
gltexture_t *TexMgr_LoadImage8 (model_t *owner, char *name, int width, int height, byte *data, int flags)
{
	unsigned short crc;
	gltexture_t *glt;
	int mark, i;
	qboolean edgefix;

	if (isDedicated)
		return NULL;

	// cache check
	crc = CRC_Block(data, width*height);
	if ((flags & TEXPREF_UNIQUE) && (glt = TexMgr_FindTexture (owner, name))) 
	{
		if (glt->crc == crc)
			return glt;
	}
	else
		glt = TexMgr_NewTexture ();

	// detect false alpha cases
	if (flags & TEXPREF_ALPHA)
	{
		for (i = 0; i < width*height; i++)
			if (data[i] == 255)
				break;
		if (i == width*height)
			flags -= TEXPREF_ALPHA;
	}
	edgefix = flags & TEXPREF_ALPHA;

	// copy data
	glt->owner = owner;
	strcpy (glt->name, name);
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->crc = crc;

	mark = Hunk_LowMark();

	// pad each dimention, but only if it's not going to be downsampled later
	if (flags & TEXPREF_PAD)
	{
		if (glt->width < TexMgr_SafeTextureSize(glt->width))
		{
			data = TexMgr_PadImageW (data, glt->width, glt->height);
			glt->width = TexMgr_Pad(width);
			edgefix = true;
		}
		if (glt->height < TexMgr_SafeTextureSize(glt->height))
		{
			data = TexMgr_PadImageH (data, glt->width, glt->height);
			glt->height = TexMgr_Pad(height);
			edgefix = true;
		}
	}
		
	// convert to 32bit
	data = (byte *)TexMgr_8to32(data, glt->width * glt->height);

	// fix edges
	if (edgefix)
		TexMgr_AlphaEdgeFix (data, glt->width, glt->height);

	// upload it
	TexMgr_LoadImagePixels (glt, (unsigned *)data);

	Hunk_FreeToLowMark(mark);

	return glt;
}

/*
================
TexMgr_LoadLightmap -- handles lightmap data (always 24-bit, currently)
================
*/
extern int gl_lightmap_format;
gltexture_t *TexMgr_LoadLightmap (model_t *owner, char *name, int width, int height, byte *data, int flags)
{
	byte *src, *dst;
	unsigned int *data32;
	gltexture_t *glt;
	int mark, i;

	if (isDedicated)
		return NULL;

	// cache check
	if ((flags & TEXPREF_UNIQUE) && (glt = TexMgr_FindTexture (owner, name))) 
		return glt;

	glt = TexMgr_NewTexture ();

	// copy data
	glt->owner = owner;
	strcpy (glt->name, name);
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->crc = 0; //lightmaps don't get a CRC check

	mark = Hunk_LowMark();

	// translate to 32-bit so that i can upload it the same as other images
	switch (gl_lightmap_format)
	{
	case GL_RGB:
		data32 = Hunk_Alloc (height*width*4);
		src = data;
		dst = (byte *)data32;
		for (i=0; i<height*width; i++)
		{
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++ = *src++;
			*dst++; //alpha can contain garbage
		}
		break;
	default:
		Sys_Error ("TexMgr_LoadLightmap: bad lightmap format");
	}

	// upload it
	TexMgr_LoadImagePixels (glt, data32);

	Hunk_FreeToLowMark(mark);

	return glt;
}

/*
================================================================================

	TEXTURE BINDING / TEXTURE UNIT SWITCHING

================================================================================
*/

int	currenttexture = -1; // to avoid unnecessary texture sets
GLenum TEXTURE0, TEXTURE1; //johnfitz
qboolean mtexenabled = false;

/*
================
GL_SelectTexture -- johnfitz -- rewritten
================
*/
void GL_SelectTexture (GLenum target) 
{
	static GLenum currenttarget;
	static int ct0, ct1;

	if (target == currenttarget)
		return;

	GL_SelectTextureFunc(target);

	if (target == TEXTURE0)
	{
		ct1 = currenttexture;
		currenttexture = ct0;
	}
	else //target == TEXTURE1
	{
		ct0 = currenttexture;
		currenttexture = ct1;
	}

	currenttarget = target;
}

/*
================
GL_DisableMultitexture -- selects texture unit 0
================
*/
void GL_DisableMultitexture(void) 
{
	if (mtexenabled)
	{
		glDisable(GL_TEXTURE_2D);
		GL_SelectTexture(TEXTURE0); //johnfitz -- no longer SGIS specific
		mtexenabled = false;
	}
}

/*
================
GL_EnableMultitexture -- selects texture unit 1
================
*/
void GL_EnableMultitexture(void) 
{
	if (gl_mtexable)
	{
		GL_SelectTexture(TEXTURE1); //johnfitz -- no longer SGIS specific
		glEnable(GL_TEXTURE_2D);
		mtexenabled = true;
	}
}

/*
================
GL_Bind
================
*/
void GL_Bind (gltexture_t *texture)
{
	if (!texture)
		texture = notexture;

	if (texture->texnum != currenttexture)
	{
		currenttexture = texture->texnum;
		glBindTexture (GL_TEXTURE_2D, currenttexture);
		texture->visframe = r_framecount;
	}
}
