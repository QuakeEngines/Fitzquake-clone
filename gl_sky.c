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
//gl_sky.c -- new fitzquake sky code

#include "quakedef.h"

#define	MAX_CLIP_VERTS 64

extern	model_t	*loadmodel;
extern	int gl_filter_max;
int		c_sky_polys; //for r_speeds readout
int		solidskytexture, alphaskytexture;
float	skyflatcolor[3];
float	speedscale;		//for sky scrolling
float	skymins[2][6], skymaxs[2][6];
int		skybox_texnum = -1; //if -1, no skybox has been loaded since quake started
char	skybox_name[32] = ""; //name of current skybox, or "" if no skybox

extern cvar_t r_drawflat;
extern cvar_t gl_farclip;
cvar_t r_fastsky = {"r_fastsky", "0"};
cvar_t r_sky_quality = {"r_sky_quality", "8"};

int		skytexorder[6] = {0,2,1,3,4,5}; //for skybox

vec3_t	skyclip[6] = {
	{1,1,0},
	{1,-1,0},
	{0,-1,1},
	{0,1,1},
	{1,0,1},
	{-1,0,1} 
};

int	st_to_vec[6][3] =
{
	{3,-1,2},
	{-3,1,2},
	{1,3,2},
	{-1,-3,2},
 	{-2,-1,3},		// straight up
 	{2,-1,-3}		// straight down
};

int	vec_to_st[6][3] =
{
	{-2,3,1},
	{2,3,-1},
	{1,3,2},
	{-1,3,-2},
	{-2,-1,3},
	{-2,1,-3}
};

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
=============
Sky_LoadTexture

A sky texture is 256*128, with the left side being a masked overlay
==============
*/
void Sky_LoadTexture (texture_t *mt)
{
	int			i, j, p, r, g, b, count;
	byte		*src;
	unsigned	data[128*128];
	unsigned	backcolor;
	unsigned	*rgba;

	src = (byte *)mt + mt->offsets[0];

	//translate back texture 
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
			data[(i*128) + j] = d_8to24table[src[i*256 + j + 128]];

	//upload back texture
	if (!solidskytexture)
		solidskytexture = texture_extension_number++;
	GL_Bind (solidskytexture );
	glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	//translate front texture
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j];
			if (p == 0)
				p = 255;
			data[(i*128) + j] = d_8to24table[p];
		}
	AlphaEdgeFix(&data, 128, 128);

	//upload front texture
	if (!alphaskytexture)
		alphaskytexture = texture_extension_number++;
	GL_Bind(alphaskytexture);
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

	//calculate r_fastsky color based on average of all opaque foreground colors
	r = g = b = count = 0;
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j];
			if (p != 0)
			{
				rgba = &d_8to24table[p];
				r += ((byte *)rgba)[0];
				g += ((byte *)rgba)[1];
				b += ((byte *)rgba)[2];
				count++;
			}
		}
	skyflatcolor[0] = (float)r/(count*255);
	skyflatcolor[1] = (float)g/(count*255);
	skyflatcolor[2] = (float)b/(count*255);
}

/*
==================
Sky_LoadSkyBox
==================
*/
char	*suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
void Sky_LoadSkyBox (char *name)
{
	int		i, mark, width, height;
	FILE	*f;
	char	filename[MAX_OSPATH];
	byte	*data;

	if (strcmp(skybox_name, name) == 0) //no change
		return;

	if (name[0] == 0) //turn off skybox if sky is set to ""
	{
		skybox_name[0] = 0;
		return;
	}

	if (skybox_texnum == -1)
	{
		skybox_texnum = texture_extension_number;
		texture_extension_number+=6;
	}

	for (i=0 ; i<6 ; i++) //load textures
	{
		mark = Hunk_LowMark ();
		sprintf (filename, "gfx/env/%s%s", name, suf[i]);
		data = Image_LoadImage (filename, &width, &height);
		if (data)
		{		
			GL_Bind (skybox_texnum + i);
			glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);

			Con_Printf ("Loaded %s - %i x %i\n", filename, width, height); //TEST
		}
		else
			Con_Printf ("Couldn't load %s\n", filename);
		Hunk_FreeToLowMark (mark);
	}

	strcpy(skybox_name, name);
}

/*
=================
Sky_NewMap
=================
*/
void Sky_NewMap (void)
{
	char key[128], value[4096];
	char *data;

	//initially no skybox
	skybox_name[0] = 0;

	data = cl.worldmodel->entities;
	if (!data)
		return;

	data = COM_Parse(data);
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

		if (!strcmp("sky", key))
			Sky_LoadSkyBox(value);
		else if (!strcmp("skyname", key)) // non-standard, introduced by QuakeForge... sigh.
			Sky_LoadSkyBox(value);
		else if (!strcmp("qlsky", key)) // non-standard, introduced by QuakeLives (EEK)
			Sky_LoadSkyBox(value);
	}
}

/*
=================
Sky_SkyCommand_f
=================
*/
void Sky_SkyCommand_f (void)
{
	switch (Cmd_Argc())
	{
	case 1:
		Con_Printf("\"sky\" is \"%s\"\n", skybox_name);
		break;
	case 2:
		Sky_LoadSkyBox(Cmd_Argv(1));
		break;
	default:
		Con_Printf("usage: sky <skyname>\n");
	}
}

/*
=============
Sky_Init
=============
*/
void Sky_Init (void)
{
	Cvar_RegisterVariable (&r_fastsky, NULL);
	Cvar_RegisterVariable (&r_sky_quality, NULL);

	Cmd_AddCommand ("sky",Sky_SkyCommand_f);
}

//==============================================================================
//
//  PROCESS SKY SURFS
//
//==============================================================================

/*
=================
Sky_ProjectPoly

update sky bounds
=================
*/
void Sky_ProjectPoly (int nump, vec3_t vecs)
{
	int		i,j;
	vec3_t	v, av;
	float	s, t, dv;
	int		axis;
	float	*vp;

	// decide which face it maps to
	VectorCopy (vec3_origin, v);
	for (i=0, vp=vecs ; i<nump ; i++, vp+=3)
	{
		VectorAdd (vp, v, v);
	}
	av[0] = fabs(v[0]);
	av[1] = fabs(v[1]);
	av[2] = fabs(v[2]);
	if (av[0] > av[1] && av[0] > av[2])
	{
		if (v[0] < 0)
			axis = 1;
		else
			axis = 0;
	}
	else if (av[1] > av[2] && av[1] > av[0])
	{
		if (v[1] < 0)
			axis = 3;
		else
			axis = 2;
	}
	else
	{
		if (v[2] < 0)
			axis = 5;
		else
			axis = 4;
	}

	// project new texture coords
	for (i=0 ; i<nump ; i++, vecs+=3)
	{
		j = vec_to_st[axis][2];
		if (j > 0)
			dv = vecs[j - 1];
		else
			dv = -vecs[-j - 1];

		j = vec_to_st[axis][0];
		if (j < 0)
			s = -vecs[-j -1] / dv;
		else
			s = vecs[j-1] / dv;
		j = vec_to_st[axis][1];
		if (j < 0)
			t = -vecs[-j -1] / dv;
		else
			t = vecs[j-1] / dv;

		if (s < skymins[0][axis])
			skymins[0][axis] = s;
		if (t < skymins[1][axis])
			skymins[1][axis] = t;
		if (s > skymaxs[0][axis])
			skymaxs[0][axis] = s;
		if (t > skymaxs[1][axis])
			skymaxs[1][axis] = t;
	}
}

/*
=================
Sky_ClipPoly
=================
*/
void Sky_ClipPoly (int nump, vec3_t vecs, int stage)
{
	float	*norm;
	float	*v;
	qboolean	front, back;
	float	d, e;
	float	dists[MAX_CLIP_VERTS];
	int		sides[MAX_CLIP_VERTS];
	vec3_t	newv[2][MAX_CLIP_VERTS];
	int		newc[2];
	int		i, j;

	if (nump > MAX_CLIP_VERTS-2)
		Sys_Error ("ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6) // fully clipped
	{	
		Sky_ProjectPoly (nump, vecs);
		return;
	}

	front = back = false;
	norm = skyclip[stage];
	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		d = DotProduct (v, norm);
		if (d > ON_EPSILON)
		{
			front = true;
			sides[i] = SIDE_FRONT;
		}
		else if (d < ON_EPSILON)
		{
			back = true;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		Sky_ClipPoly (nump, vecs, stage+1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy (vecs, (vecs+(i*3)) );
	newc[0] = newc[1] = 0;

	for (i=0, v = vecs ; i<nump ; i++, v+=3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy (v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy (v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i+1] == SIDE_ON || sides[i+1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i+1]);
		for (j=0 ; j<3 ; j++)
		{
			e = v[j] + d*(v[j+3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	Sky_ClipPoly (newc[0], newv[0][0], stage+1);
	Sky_ClipPoly (newc[1], newv[1][0], stage+1);
}

/*
================
Sky_ProcessPoly
================
*/
void Sky_ProcessPoly (msurface_t *s)
{
	int			i;
	float		*v;
	glpoly_t	*p;
	vec3_t		verts[MAX_CLIP_VERTS];

	p = s->polys;

	//set drawflat color if appropriate
	if (r_drawflat.value)
	{
		srand((unsigned int) p);
		glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
	}

	//draw it
	glBegin (GL_POLYGON);
	for (i=0,v=p->verts[0] ; i<p->numverts ; i++, v+=VERTEXSIZE)
		glVertex3fv (v);
	glEnd ();

	//update sky bounds
	if (!r_fastsky.value)
	{
		for (i=0 ; i<p->numverts ; i++)
			VectorSubtract (p->verts[i], r_origin, verts[i]);
		Sky_ClipPoly (p->numverts, verts[0], 0);
	}
}

/*
=================
Sky_ProcessBrushModel
=================
*/
void Sky_ProcessBrushModel (entity_t *e)
{
	int			j, k;
	vec3_t		mins, maxs;
	int			i, numsurfaces;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	model_t		*clmodel;
	qboolean	rotated;

	currententity = e;
	currenttexture = -1;

	clmodel = e->model;

	if (clmodel == cl.worldmodel) //does this ever happen?
		return;

	if (r_speeds.value)
		Con_Printf ("bmodel in view\n"); //TEST

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i=0 ; i<3 ; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

	//memset (lightmap_polys, 0, sizeof(lightmap_polys));

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	glPushMatrix ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	R_RotateForEntity (e);
	e->angles[0] = -e->angles[0];	// stupid quake bug

	//
	// draw texture
	//
	for (i=0 ; i<clmodel->nummodelsurfaces ; i++, psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		// draw the polygon
		if (psurf->flags & SURF_DRAWSKY) 
			if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				Sky_ProcessPoly (psurf);
	}

	glPopMatrix ();
}

/*
================
Sky_ProcessWorld
================
*/
void Sky_ProcessWorld (mnode_t *node)
{
	int			i, c, side, *pindex;
	vec3_t		acceptpt, rejectpt;
	mplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	double		d, dot;
	vec3_t		mins, maxs;

	if (node->contents == CONTENTS_SOLID)
		return; //solid
	if (node->visframe != r_visframecount)
		return; //not in PVS
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return; //not in frustum
	
	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;
		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;
		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}
		return;
	}

	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
		side = 0;
	else
		side = 1;

	// recurse down the children, front side first
	Sky_ProcessWorld (node->children[side]);

	// draw stuff
	c = node->numsurfaces;
	if (c)
	{
		surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;
		{
			for ( ; c ; c--, surf++)
			{
				if (surf->flags & SURF_DRAWSKY && surf->visframe == r_framecount)
				{
					if ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
						continue; // backface cull

					Sky_ProcessPoly (surf);
				}
			}
		}
	}

	// recurse down the back side
	Sky_ProcessWorld (node->children[!side]);
}

//==============================================================================
//
//  RENDER SKYBOX
//
//==============================================================================

/*
==============
Sky_EmitSkyBoxVertex
==============
*/
void Sky_EmitSkyBoxVertex (float s, float t, int axis)
{
	vec3_t		v, b;
	int			j, k;

	b[0] = s * gl_farclip.value / sqrt(3.0);
	b[1] = t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
		v[j] += r_origin[j];
	}

	// convert from range [-1,1] to [0,1]
	s = (s+1)*0.5;
	t = (t+1)*0.5;

	// avoid bilerp seam
	s = s * 510.0/512 + 1.0/512;
	t = t * 510.0/512 + 1.0/512;

	t = 1.0 - t;
	glTexCoord2f (s, t);
	glVertex3fv (v);
}

/*
==============
Sky_DrawSkyBox

TODO: eliminate cracks by adding an extra vert on tjuncs
==============
*/
void Sky_DrawSkyBox (void)
{
	int		i, j, k;
	vec3_t	v;
	float	s, t;

	for (i=0 ; i<6 ; i++)
	{
		if (skymins[0][i] >= skymaxs[0][i] || skymins[1][i] >= skymaxs[1][i])
			continue;

		GL_Bind (skybox_texnum+skytexorder[i]);

#if 1 //TEST
		skymins[0][i] = -1;
		skymins[1][i] = -1;
		skymaxs[0][i] = 1;
		skymaxs[1][i] = 1;
#endif
		glBegin (GL_QUADS);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymins[1][i], i);
		Sky_EmitSkyBoxVertex (skymins[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymaxs[1][i], i);
		Sky_EmitSkyBoxVertex (skymaxs[0][i], skymins[1][i], i);
		glEnd ();

		c_sky_polys++;
	}
}

//==============================================================================
//
//  RENDER CLOUDS
//
//==============================================================================

/*
=============
Sky_DrawFaceQuadLayer
=============
*/
void Sky_DrawFaceQuadLayer (glpoly_t *p)
{
	float	*v;
	int		i;
	float	s, t;
	vec3_t	dir;
	float	length;

	glBegin (GL_QUADS);
	for (i=0, v=p->verts[0] ; i<4 ; i++, v+=VERTEXSIZE)
	{
		VectorSubtract (v, r_origin, dir);
		dir[2] *= 3;	// flatten the sphere

		length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
		length = sqrt (length);
		length = 6*63/length;

		dir[0] *= length;
		dir[1] *= length;

		s = (speedscale + dir[0]) * (1.0/128);
		t = (speedscale + dir[1]) * (1.0/128);

		glTexCoord2f (s, t);
		glVertex3fv (v);
	}
	glEnd ();
}

/*
===============
Sky_DrawFaceQuad

TODO: use multitexture
===============
*/
void Sky_DrawFaceQuad (glpoly_t *p)
{
	GL_DisableMultitexture();

	GL_Bind (solidskytexture);
	speedscale = cl.time*8;
	speedscale -= (int)speedscale & ~127;
	Sky_DrawFaceQuadLayer (p);

//	glColor4f(1,1,1,0.666);
//	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable (GL_BLEND); //GL_ALPHA_TEST for hard edge

	GL_Bind (alphaskytexture);
	speedscale = cl.time*16;
	speedscale -= (int)speedscale & ~127;
	Sky_DrawFaceQuadLayer (p);

//	glColor3f(1,1,1);
//	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glDisable (GL_BLEND); //GL_ALPHA_TEST for hard edge
}

/*
==============
Sky_SetFaceVert
==============
*/
void Sky_SetFaceVert (float s, float t, int axis, vec3_t v)
{
	vec3_t		b;
	int			j, k;

	b[0] = s * gl_farclip.value / sqrt(3.0);
	b[1] = t * gl_farclip.value / sqrt(3.0);
	b[2] = gl_farclip.value / sqrt(3.0);

	for (j=0 ; j<3 ; j++)
	{
		k = st_to_vec[axis][j];
		if (k < 0)
			v[j] = -b[-k - 1];
		else
			v[j] = b[k - 1];
		v[j] += r_origin[j];
	}
}

/*
==============
Sky_DrawFace
==============
*/

void Sky_DrawFace (int axis)
{
	glpoly_t	*p;
	vec3_t		verts[4];
	int			i, j, start;
	float		di,qi,dj,qj;
	vec3_t		vup, vright, temp, temp2;

	Sky_SetFaceVert(-1.0, -1.0, axis, verts[0]);
	Sky_SetFaceVert(-1.0,  1.0, axis, verts[1]);
	Sky_SetFaceVert(1.0,   1.0, axis, verts[2]);
	Sky_SetFaceVert(1.0,  -1.0, axis, verts[3]);

	start = Hunk_LowMark ();
	p = Hunk_Alloc(sizeof(glpoly_t));

	VectorSubtract(verts[2],verts[3],vup);
	VectorSubtract(verts[2],verts[1],vright);

	di = max((int)r_sky_quality.value, 1);
	qi = 1.0 / di;
	dj = (axis < 4) ? di*2 : di; //subdivide vertically more than horizontally on skybox sides
	qj = 1.0 / dj;

	for (i=0; i<di; i++)
	{
		for (j=0; j<dj; j++)
		{
			if (i*qi >= skymins[0][axis]/2+0.5 - qi && 
				i*qi <= skymaxs[0][axis]/2+0.5 && 
				j*qj >= skymins[1][axis]/2+0.5 - qj && 
				j*qj <= skymaxs[1][axis]/2+0.5)
			{
				//if (i%2 ^ j%2) continue; //checkerboard test
				VectorScale (vright, qi*i, temp);
				VectorScale (vup, qj*j, temp2); 
				VectorAdd(temp,temp2,temp);
				VectorAdd(verts[0],temp,p->verts[0]);

				VectorScale (vup, qj, temp);
				VectorAdd (p->verts[0],temp,p->verts[1]);

				VectorScale (vright, qi, temp);
				VectorAdd (p->verts[1],temp,p->verts[2]);

				VectorAdd (p->verts[0],temp,p->verts[3]);

				Sky_DrawFaceQuad (p);
				c_sky_polys++;
			}
		}
	}
	Hunk_FreeToLowMark (start);
}

/*
==============
Sky_DrawSky

called once per frame before drawing anything else
==============
*/
void Sky_DrawSky (void)
{
	extern int		ztrickframe;
	extern cvar_t	gl_ztrick;
	int				i;
	extern int		fog_density;

	//
	// reset sky bounds
	//
	for (i=0 ; i<6 ; i++)
	{
		skymins[0][i] = skymins[1][i] = 9999;
		skymaxs[0][i] = skymaxs[1][i] = -9999;
	}

	//
	// process world and bmodels: draw flat-shaded sky surfs, and update skybounds
	//
	glDisable (GL_TEXTURE_2D);
	glColor3f (skyflatcolor[0], skyflatcolor[1], skyflatcolor[2]);
	Fog_ColorForSky ();
	Fog_DisableGFog ();

	Sky_ProcessWorld (cl.worldmodel->nodes);
//		for (i=0 ; i<cl_numvisedicts ; i++)
//			if (cl_visedicts[i]->model->type == mod_brush)
//				Sky_ProcessBrushModel (cl_visedicts[i]);
	
	glEnable (GL_TEXTURE_2D);
	glColor3f (1, 1, 1);
	Fog_EnableGFog ();

	//
	// render slow sky: cloud layers and/or skybox
	//
	if (!r_fastsky.value && fog_density <= 0 && !r_drawflat.value)
	{
		if (gl_ztrick.value && !(ztrickframe & 1))
			glDepthFunc(GL_LEQUAL);
		else
			glDepthFunc(GL_GEQUAL);
		glDepthMask(0);

		if (strlen(skybox_name))
			Sky_DrawSkyBox ();
		else
		{
			for (i=0 ; i<6 ; i++)
				if (skymins[0][i] < skymaxs[0][i] && skymins[1][i] < skymaxs[1][i])
					Sky_DrawFace (i);
		}
		if (gl_ztrick.value && !(ztrickframe & 1))
			glDepthFunc(GL_GEQUAL);
		else
			glDepthFunc(GL_LEQUAL);
		glDepthMask(1);
	}
}