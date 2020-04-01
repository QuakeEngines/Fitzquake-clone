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
//image.c -- image loading

#include "quakedef.h"

char loadfilename[MAX_OSPATH]; //file scope so that error messages can use it

/*
============
Image_LoadImage

returns a pointer to hunk allocated RGBA data

TODO: search order: tga png jpg pcx lmp
============
*/
byte *Image_LoadImage (char *name, int *width, int *height)
{
	FILE	*f;

	sprintf (loadfilename, "%s.tga", name);
	COM_FOpenFile (loadfilename, &f);
	if (f)
		return Image_LoadTGA (f, width, height);

	sprintf (loadfilename, "%s.pcx", name);
	COM_FOpenFile (loadfilename, &f);
	if (f)
		return Image_LoadPCX (f, width, height);

	return NULL;
}

//==============================================================================
//
//  TGA
//
//==============================================================================

typedef struct targaheader_s {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} targaheader_t;

targaheader_t targa_header;

int fgetLittleShort (FILE *f)
{
	byte	b1, b2;

	b1 = fgetc(f);
	b2 = fgetc(f);

	return (short)(b1 + b2*256);
}

int fgetLittleLong (FILE *f)
{
	byte	b1, b2, b3, b4;

	b1 = fgetc(f);
	b2 = fgetc(f);
	b3 = fgetc(f);
	b4 = fgetc(f);

	return b1 + (b2<<8) + (b3<<16) + (b4<<24);
}

/*
=============
Image_LoadTGA
=============
*/
byte *Image_LoadTGA (FILE *fin, int *width, int *height)
{
	int				columns, rows, numPixels;
	byte			*pixbuf;
	int				row, column;
	byte			*targa_rgba;

	targa_header.id_length = fgetc(fin);
	targa_header.colormap_type = fgetc(fin);
	targa_header.image_type = fgetc(fin);
	
	targa_header.colormap_index = fgetLittleShort(fin);
	targa_header.colormap_length = fgetLittleShort(fin);
	targa_header.colormap_size = fgetc(fin);
	targa_header.x_origin = fgetLittleShort(fin);
	targa_header.y_origin = fgetLittleShort(fin);
	targa_header.width = fgetLittleShort(fin);
	targa_header.height = fgetLittleShort(fin);
	targa_header.pixel_size = fgetc(fin);
	targa_header.attributes = fgetc(fin);

	if (targa_header.image_type!=2 && targa_header.image_type!=10) 
		Sys_Error ("Image_LoadTGA: %s is not a type 2 or type 10 targa\n", loadfilename);

	if (targa_header.colormap_type !=0 || (targa_header.pixel_size!=32 && targa_header.pixel_size!=24))
		Sys_Error ("Image_LoadTGA: %s is not a 24bit or 32bit targa\n", loadfilename);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;

	targa_rgba = Hunk_Alloc (numPixels*4);
	
	if (targa_header.id_length != 0)
		fseek(fin, targa_header.id_length, SEEK_CUR);  // skip TARGA image comment
	
	if (targa_header.image_type==2) // Uncompressed, RGB images
	{
		for(row=rows-1; row>=0; row--)
		{
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; column++)
			{
				unsigned char red,green,blue,alphabyte;
				switch (targa_header.pixel_size)
				{
				case 24:
						blue = getc(fin);
						green = getc(fin);
						red = getc(fin);
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = 255;
						break;
				case 32:
						blue = getc(fin);
						green = getc(fin);
						red = getc(fin);
						alphabyte = getc(fin);
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						break;
				}
			}
		}
	}
	else if (targa_header.image_type==10) // Runlength encoded RGB images
	{
		unsigned char red,green,blue,alphabyte,packetHeader,packetSize,j;
		for(row=rows-1; row>=0; row--)
		{
			pixbuf = targa_rgba + row*columns*4;
			for(column=0; column<columns; )
			{
				packetHeader=getc(fin);
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) // run-length packet
				{
					switch (targa_header.pixel_size)
					{
					case 24:
							blue = getc(fin);
							green = getc(fin);
							red = getc(fin);
							alphabyte = 255;
							break;
					case 32:
							blue = getc(fin);
							green = getc(fin);
							red = getc(fin);
							alphabyte = getc(fin);
							break;
					}
	
					for(j=0;j<packetSize;j++)
					{
						*pixbuf++=red;
						*pixbuf++=green;
						*pixbuf++=blue;
						*pixbuf++=alphabyte;
						column++;
						if (column==columns) // run spans across rows
						{
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}
					}
				}
				else // non run-length packet
				{
					for(j=0;j<packetSize;j++)
					{
						switch (targa_header.pixel_size)
						{
						case 24:
								blue = getc(fin);
								green = getc(fin);
								red = getc(fin);
								*pixbuf++ = red;
								*pixbuf++ = green;
								*pixbuf++ = blue;
								*pixbuf++ = 255;
								break;
						case 32:
								blue = getc(fin);
								green = getc(fin);
								red = getc(fin);
								alphabyte = getc(fin);
								*pixbuf++ = red;
								*pixbuf++ = green;
								*pixbuf++ = blue;
								*pixbuf++ = alphabyte;
								break;
						}
						column++;
						if (column==columns) // pixel packet run spans across rows
						{
							column=0;
							if (row>0)
								row--;
							else
								goto breakOut;
							pixbuf = targa_rgba + row*columns*4;
						}						
					}
				}
			}
			breakOut:;
		}
	}
	
	fclose(fin);

	*width = (int)(targa_header.width);
	*height = (int)(targa_header.height);
	return targa_rgba;
}

//==============================================================================
//
//  PCX
//
//==============================================================================

typedef struct
{
    char	manufacturer;
    char	version;
    char	encoding;
    char	bits_per_pixel;
    unsigned short	xmin,ymin,xmax,ymax;
    unsigned short	hres,vres;
    unsigned char	palette[48];
    char	reserved;
    char	color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    char	filler[58];
    unsigned 	data;			// unbounded
} pcxheader_t;

/*
============
Image_LoadPCX
============
*/
byte *Image_LoadPCX (FILE *f, int *width, int *height)
{
	pcxheader_t	*pcx, pcxbuf;
	byte	palette[768];
	byte	*pix;
	int		x, y;
	int		dataByte, runLength;
	int		count;
	byte	*pcx_rgb;

	fread (&pcxbuf, 1, sizeof(pcxbuf), f);

	pcx = &pcxbuf;

	if (pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| pcx->xmax >= 320
		|| pcx->ymax >= 256)
		Sys_Error ("Image_LoadPCX: bad pcx file: %s\n", loadfilename);

	// seek to palette
	fseek (f, -768, SEEK_END);
	fread (palette, 1, 768, f);

	fseek (f, sizeof(pcxbuf) - 4, SEEK_SET);

	count = (pcx->xmax+1) * (pcx->ymax+1);

	pcx_rgb = Hunk_Alloc ( count * 4);

	for (y=0 ; y<=pcx->ymax ; y++)
	{
		pix = pcx_rgb + 4*y*(pcx->xmax+1);
		for (x=0 ; x<=pcx->ymax ; ) //FIXME -- should this be xmax?
		{
			dataByte = fgetc(f);

			if((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = fgetc(f);
			}
			else
				runLength = 1;

			while(runLength-- > 0)
			{
				pix[0] = palette[dataByte*3];
				pix[1] = palette[dataByte*3+1];
				pix[2] = palette[dataByte*3+2];
				pix[3] = 255;
				pix += 4;
				x++;
			}
		}
	}

	*width = (int)(pcx->xmax) + 1;
	*height = (int)(pcx->ymax) + 1;
	return pcx_rgb;
}
