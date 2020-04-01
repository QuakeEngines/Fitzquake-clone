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
// r_main.c

#include "quakedef.h"

entity_t	r_worldentity;

qboolean	r_cache_thrash;		// compatability

extern qboolean mtexenabled; //johnfitz

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

//johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;

qboolean	envmap;				// true during envmap command capture 

//up to 16 color translated skins 
gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz -- changed to an array of pointers

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float	r_world_matrix[16];
float	r_base_world_matrix[16];

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0"};
cvar_t	r_wateralpha = {"r_wateralpha","1"};
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

cvar_t	gl_finish = {"gl_finish","0"};
cvar_t	gl_clear = {"gl_clear","0"};
cvar_t	gl_cull = {"gl_cull","1"};
cvar_t	gl_texsort = {"gl_texsort","0"}; //johnfitz -- was 1
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","1"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","1"}; //johnfitz -- was 0

//johnfitz -- new cvars
cvar_t	r_stereo = {"r_stereo","0"};
cvar_t	r_stereodepth = {"r_stereodepth","128"};
cvar_t	r_clearcolor = {"r_clearcolor","2", true};
cvar_t	r_drawflat = {"r_drawflat","0"};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0"};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", true};
cvar_t	gl_farclip = {"gl_farclip", "8192", true};
cvar_t	gl_overbright_models = {"gl_overbright_models", "1", true};
//johnfitz

extern	cvar_t	gl_ztrick;

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		switch(p->signbits)
		{
		default:
		case 0:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 1:
			if (p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 2:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 3:
			if (p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emaxs[2] < p->dist)
				return true;
			break;
		case 4:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 5:
			if (p->normal[0]*emins[0] + p->normal[1]*emaxs[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 6:
			if (p->normal[0]*emaxs[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		case 7:
			if (p->normal[0]*emins[0] + p->normal[1]*emins[1] + p->normal[2]*emins[2] < p->dist)
				return true;
			break;
		}
	}
	return false;
}

/*
===============
R_RotateForEntity
===============
*/
void R_RotateForEntity (entity_t *e)
{
    glTranslatef (e->origin[0],  e->origin[1],  e->origin[2]);

    glRotatef (e->angles[1],  0, 0, 1);
    glRotatef (-e->angles[0],  0, 1, 0);
    glRotatef (e->angles[2],  1, 0, 0);
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1, offset);
	}
	else
		glDisable (GL_POLYGON_OFFSET_FILL);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = currententity->model->cache.data;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + currententity->syncbase;

	// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
	// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}

/*
=================
R_DrawSpriteModel -- johnfitz -- rewritten: now supports all orientations
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t			point, v_forward, v_right, v_up;
	msprite_t		*psprite;
	mspriteframe_t	*frame;
	float			*s_up, *s_right;
	float			angle, sr, cr, sh, th, len;

	// don't even bother culling, because it's just a single polygon without a surface cache
	frame = R_GetSpriteFrame (e);
	psprite = currententity->model->cache.data;

	switch(psprite->type)
	{
	case SPR_VP_PARALLEL_UPRIGHT: //faces view plane, up is towards the heavens
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		s_up = v_up;
		s_right = vright;
		break;
	case SPR_FACING_UPRIGHT: //faces camera origin, up is towards the heavens
		VectorSubtract(currententity->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalizeFast(v_forward);
		v_right[0] = v_forward[1];
		v_right[1] = -v_forward[0];
		v_right[2] = 0;
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL: //faces view plane, up is towards the top of the screen
		s_up = vup;
		s_right = vright;
		break;
	case SPR_ORIENTED: //pitch yaw roll are independent of camera
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL_ORIENTED: //faces view plane, but obeys roll value
		angle = currententity->angles[ROLL] * M_PI_DIV_180;
		sr = sin(angle);
		cr = cos(angle);
		v_right[0] = vright[0] * cr + vup[0] * sr;
		v_up[0] = vright[0] * -sr + vup[0] * cr;
		v_right[1] = vright[1] * cr + vup[1] * sr;
		v_up[1] = vright[1] * -sr + vup[1] * cr;
		v_right[2] = vright[2] * cr + vup[2] * sr;
		v_up[2] = vright[2] * -sr + vup[2] * cr;
		s_up = v_up;
		s_right = v_right;
		break;
	default:
		return;
	}

	//johnfitz: offset decals
	if (psprite->type == SPR_ORIENTED)
		GL_PolygonOffset (OFFSET_DECAL);

	//johnfitz: since texture dimentions are padded to 2^n, tex coords will not be 0-1
	//FIXME: these should not be calculated each frame
	sh = (float)frame->width/(float)Pad(frame->width);
	th = (float)frame->height/(float)Pad(frame->height);

	glColor3f (1,1,1);

	GL_DisableMultitexture();

    GL_Bind(frame->gltexture);

	glEnable (GL_ALPHA_TEST);
	glBegin (GL_QUADS);

	glTexCoord2f (0, th);
	VectorMA (e->origin, frame->down, s_up, point);
	VectorMA (point, frame->left, s_right, point);
	glVertex3fv (point);

	glTexCoord2f (0, 0);
	VectorMA (e->origin, frame->up, s_up, point);
	VectorMA (point, frame->left, s_right, point);
	glVertex3fv (point);

	glTexCoord2f (sh, 0);
	VectorMA (e->origin, frame->up, s_up, point);
	VectorMA (point, frame->right, s_right, point);
	glVertex3fv (point);

	glTexCoord2f (sh, th);
	VectorMA (e->origin, frame->down, s_up, point);
	VectorMA (point, frame->right, s_right, point);
	glVertex3fv (point);
	
	glEnd ();
	glDisable (GL_ALPHA_TEST);

	//johnfitz: offset decals
	if (psprite->type == SPR_ORIENTED)
		GL_PolygonOffset (OFFSET_NONE);
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/


#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t	shadevector;
float	shadelight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;

int Pad(int x); //johnfitz

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame (aliashdr_t *paliashdr, int posenum)
{
	float	s, t;
	float 	l;
	int		i, j;
	int		index;
	trivertx_t	*v, *verts;
	int		list;
	int		*order;
	vec3_t	point;
	float	*normal;
	int		count;
	float	hscale, vscale; //johnfitz -- padded skins

	//johnfitz -- padded skins
	hscale = (float)paliashdr->skinwidth/(float)Pad(paliashdr->skinwidth);
	vscale = (float)paliashdr->skinheight/(float)Pad(paliashdr->skinheight);
	//johnfitz

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	if (r_drawflat.value)
		glDisable (GL_TEXTURE_2D); //johnfitz -- drawflat

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done

		if (count < 0)
		{
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		}
		else
			glBegin (GL_TRIANGLE_STRIP);

		do
		{
			// texture coordinates come from the draw list

			//johnfitz -- multitexture
			if(mtexenabled)
			{
				GL_MTexCoord2fFunc (TEXTURE0, hscale*((float *)order)[0], vscale*((float *)order)[1]);
				GL_MTexCoord2fFunc (TEXTURE1, hscale*((float *)order)[0], vscale*((float *)order)[1]);
			}
			else
				glTexCoord2f (hscale*((float *)order)[0], vscale*((float *)order)[1]);
			//johnfitz

			order += 2;

			//johnfitz -- fullbright models
			if (!r_fullbright.value) 
				l = shadedots[verts->lightnormalindex] * shadelight; // normals and vertexes come from the frame list
			else
				l = 1.0;
			//johnfitz
			
			//johnfitz -- r_drawflat
			if (r_drawflat.value)
			{
				srand(count * (unsigned int) order);
				glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
			}
			else
				glColor3f (l, l, l);
			//johnfitz

			glVertex3f (verts->v[0], verts->v[1], verts->v[2]);
			verts++;
		} while (--count);

		glEnd ();
	}

	//johnfitz -- drawflat
	if (r_drawflat.value)
	{
		glEnable (GL_TEXTURE_2D);
		srand((int) (cl.time * 1000)); //restore randomness
	}
	//johnfitz

	rs_aliaspasses += paliashdr->numtris; //johnfitz
}


/*
=============
GL_DrawAliasShadow --johnfitz -- moved some code here from other places
=============
*/
extern	vec3_t			lightspot;

void GL_DrawAliasShadow (aliashdr_t *paliashdr, int posenum)
{
	trivertx_t	*v, *verts;
	vec3_t	point;
	float	s, t, l, height, lheight, an;
	float	*normal;
	int		i, j, list, count, index;
	int		*order;

	GL_DisableMultitexture ();

	glPushMatrix ();
	R_RotateForEntity (currententity);
	glDisable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glColor4f (0,0,0,0.5);

	an = currententity->angles[1]/180*M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);

	lheight = currententity->origin[2] - lightspot[2];

	height = 0;
	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	height = -lheight + 1.0; //FIXME: use GL_PolygonOffset instead or manually raising it 1.0?

	//FIXME: orient shadow onto mplane_t *lightplane

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		}
		else
			glBegin (GL_TRIANGLE_STRIP);

		do
		{
			order += 2;

			// normals and vertexes come from the frame list
			point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;
			glVertex3fv (point);

			verts++;
		} while (--count);

		glEnd ();
	}	

	rs_aliaspasses += paliashdr->numtris; //johnfitz

	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glColor4f (1,1,1,1);
	glPopMatrix ();
}

/*
=================
R_SetupAliasFrame
=================
*/
void R_SetupAliasFrame (int frame, aliashdr_t *paliashdr)
{
	int				pose, numposes;
	float			interval;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = paliashdr->frames[frame].interval;
		pose += (int)(cl.time / interval) % numposes;
	}

	GL_DrawAliasFrame (paliashdr, pose);
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	int			i;

	shadelight = R_LightPoint (e->origin);

	//add dlights
	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (currententity->origin, cl_dlights[i].origin, dist);
			shadelight += max (0, cl_dlights[i].radius - Length(dist));
		}
	}

	// minimum light value on gun
	if (e == &cl.viewent)
		shadelight = max (shadelight, 24);

	// minimum light value on players
	if (currententity > cl_entities && currententity <= cl_entities + cl.maxclients)
		shadelight = max (shadelight, 8);

	// clamp lighting so it doesn't overbright as much
	if (gl_overbright_models.value)
		shadelight = min (shadelight, 96);

	//johnfitz -- hack up the brightness when fullbrights but no overbrights
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (!strcmp (e->model->name, "progs/flame2.mdl") ||
			!strcmp (e->model->name, "progs/flame.mdl") ||
			!strcmp (e->model->name, "progs/boss.mdl"))
			shadelight = 200;

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	shadelight = shadelight / 200.0;
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliashdr_t	*paliashdr;
	trivertx_t	*verts, *v;
	vec3_t		mins, maxs;
	float		s, t;
	int			i, anim, index;
	gltexture_t	*tx, *fb;

	VectorAdd (currententity->origin, currententity->model->mins, mins);
	VectorAdd (currententity->origin, currententity->model->maxs, maxs);
	if (R_CullBox (mins, maxs))
		return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	R_SetupAliasLighting (e);

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	rs_aliaspolys += paliashdr->numtris;

    glPushMatrix ();
	R_RotateForEntity (e);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	if (gl_smoothmodels.value && !r_drawflat.value)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	GL_DisableMultitexture();

	//
	// set up textures
	//
	anim = (int)(cl.time*10) & 3;
	tx = paliashdr->gltextures[currententity->skinnum][anim];
	fb = paliashdr->fbtextures[currententity->skinnum][anim];
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;
		if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		    tx = playertextures[i - 1];
	}
	if (!gl_fullbrights.value || r_drawflat.value || r_fullbright.value)
		fb = NULL;

	//
	// draw it
	//
	if (gl_overbright_models.value && !r_drawflat.value)
	{
		if  (gl_texture_env_combine && gl_mtexable && fb) //case 1: everything in one pass
		{
			GL_Bind (tx);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT); 
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind (fb);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
			glEnable(GL_BLEND);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glDisable(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	
		}
		else if (gl_texture_env_combine) //case 2: overbright in one pass, then fullbright pass
		{
		// first pass
			GL_Bind(tx);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT); 
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		// second pass
			if (fb)
			{
				GL_Bind(fb);
				glEnable(GL_BLEND);
				glDepthMask(GL_FALSE);
				R_SetupAliasFrame (currententity->frame, paliashdr);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);
			}
		}
		else //case 3: overbright in two passes, then fullbright pass
		{
		// first pass
			GL_Bind(tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
		// second pass
			glEnable(GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glDepthMask(GL_TRUE);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_BLEND);
		// third pass
			if (fb)
			{
				GL_Bind(fb);
				glEnable(GL_BLEND);
				glDepthMask(GL_FALSE);
				R_SetupAliasFrame (currententity->frame, paliashdr);
				glDepthMask(GL_TRUE);
				glDisable(GL_BLEND);
			}
		}
	}
	else
	{
		if (!fb) //case 4: no fullbright mask
		{		
			GL_Bind(tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (gl_mtexable) //case 5: fullbright mask using multitexture
		{
			GL_DisableMultitexture(); // selects TEXTURE0
			GL_Bind (tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_EnableMultitexture(); // selects TEXTURE1 
			GL_Bind (fb);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
			glEnable(GL_BLEND);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glDisable(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	
		}
		else //case 6: fullbright mask without multitexture
		{
		// first pass
			GL_Bind(tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
		// second pass
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	
			GL_Bind(fb);
			glEnable(GL_BLEND);
			glDepthMask(GL_FALSE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
	}

	if (gl_smoothmodels.value && !r_drawflat.value)
		glShadeModel (GL_FLAT);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	glPopMatrix ();

	if (r_shadows.value)
		GL_DrawAliasShadow (paliashdr, lastposenum);
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos( DEG2RAD( angle ) );
	scale_side = sin( DEG2RAD( angle ) );

	out[0] = scale_forward*forward[0] + scale_side*side[0];
	out[1] = scale_forward*forward[1] + scale_side*side[1];
	out[2] = scale_forward*forward[2] + scale_side*side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (void)
{
	int		i;

	TurnVector(frustum[0].normal, vpn, vright, r_refdef.fov_x/2 - 90); //left plane
	TurnVector(frustum[1].normal, vpn, vright, 90 - r_refdef.fov_x/2); //right plane
	TurnVector(frustum[2].normal, vpn, vup, 90 - r_refdef.fov_y/2); //bottom plane
	TurnVector(frustum[3].normal, vpn, vup, r_refdef.fov_y/2 - 90); //top plane

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); //FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
#define NEARCLIP 4
float frustum_skew = 0.0; //used by r_stereo
void GL_SetFrustum(float fovx, float fovy)
{
   float xmax, ymax;
   xmax = NEARCLIP * tan( fovx * M_PI / 360.0 );
   ymax = NEARCLIP * tan( fovy * M_PI / 360.0 );
   glFrustum(-xmax + frustum_skew, xmax + frustum_skew, -ymax, ymax, NEARCLIP, gl_farclip.value);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	int		i;
	extern	int glwidth, glheight;
	float fovx, fovy; //johnfitz
	int contents; //johnfitz

	//johnfitz -- rewrote this section
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();
	glViewport (glx + r_refdef.vrect.x, 
				gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height, 
				r_refdef.vrect.width, 
				r_refdef.vrect.height);
	//johnfitz

	//johnfitz -- warp view for underwater
	fovx = r_refdef.fov_x;
	fovy = r_refdef.fov_y;
	if (r_waterwarp.value)
	{
		contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			//variance should be a percentage of width, where width = 2 * tan(fov / 2)
			//otherwise the effect is too dramatic at high FOV and too subtle at low FOV
			//what a mess!
			fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
		}
	}
    GL_SetFrustum (fovx, fovy);
	//johnfitz

	glCullFace(GL_FRONT);

	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up
    glRotatef (-r_refdef.viewangles[2],  1, 0, 0);
    glRotatef (-r_refdef.viewangles[0],  0, 1, 0);
    glRotatef (-r_refdef.viewangles[1],  0, 0, 1);
    glTranslatef (-r_refdef.vieworg[0],  -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);

	glGetFloatv (GL_MODELVIEW_MATRIX, r_world_matrix);

	//
	// set drawing parms
	//
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_DEPTH_TEST);
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
	int				edgecount;
	vrect_t			vrect;
	float			w, h;

	//johnfitz -- disable cheats in multiplayer
	//TODO: find a less hacky way to do this.
	if (cl.maxclients > 1)
	{
		Cvar_Set ("r_fullbright", "0");
		Cvar_Set ("r_drawflat", "0");
	}
	//johnfitz

	R_PushDlights (); //johnfitz -- moved here from V_RenderView

	Fog_SetupFrame (); //johnfitz

	R_AnimateLight ();

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	R_SetFrustum (); //johnfitz -- moved here from R_RenderScene

	R_SetupGL (); //johnfitz -- moved here from R_RenderScene
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_Clear -- johnfitz -- rewritten
=============
*/
int ztrickframe = 0;
void R_Clear (void)
{
	extern int gl_stencilbits;
	unsigned int clearbits = 0;

	// clear buffers

	if (gl_clear.value)
		clearbits |= GL_COLOR_BUFFER_BIT;
	if (!gl_ztrick.value)
		clearbits |= GL_DEPTH_BUFFER_BIT;
	if (gl_stencilbits)
		clearbits |= GL_STENCIL_BUFFER_BIT;
	glClear (clearbits);

	// set depthrange

	if (gl_ztrick.value)
	{
		ztrickframe++;
		if (ztrickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			glDepthFunc (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			glDepthFunc (GL_GEQUAL);
		}
	}
	else
	{
		gldepthmin = 0;
		gldepthmax = 1;
		glDepthFunc (GL_LEQUAL);
	}
	glDepthRange (gldepthmin, gldepthmax);
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	extern cvar_t r_vfog; //johnfitz
	int		i;

	if (!r_drawentities.value)
		return;

	//johnfitz -- sprites are not a special case
	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		//johnfitz -- chasecam
		if (currententity == &cl_entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		//johnfitz

		switch (currententity->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (currententity);
			break;
#if 0
		//johnfitz -- if vfog disabled, treat fog as a normal bmodel
		case mod_fog:
			if (!r_vfog.value)
				R_DrawBrushModel (currententity);
			break;
#endif
		case mod_brush:
			R_DrawBrushModel (currententity);
			break;

		case mod_sprite:
			R_DrawSpriteModel (currententity);
			break;

		default:
			break;
		}
	}
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value || envmap)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	if (currententity->model->type != mod_alias) //johnfitz -- this fixes a crash
		return;

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));
	R_DrawAliasModel (currententity);
	glDepthRange (gldepthmin, gldepthmax);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_MarkLeaves (); // done here so we know if we're in water

	Fog_EnableGFog (); //johnfitz

	R_DrawWorld (); // adds static entities to the list

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	GL_DisableMultitexture();

	R_DrawWaterSurfaces (); //johnfitz -- moved here from R_RenderView() -- particle z-buffer bug

	R_RenderDlights (); //triangle fan dlights -- johnfitz -- moved after water

	Fog_DrawVFog (); //johnfitz

	if (r_drawflat.value)
		srand ((int) (cl.time * 1000)); //johnfitz -- reset rand() after abusing it for r_drawflat

	R_DrawParticles ();

	Fog_DisableGFog (); //johnfitz

#ifdef GLTEST
	Test_Draw ();
#endif
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_FloatTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = 
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_Clear ();

	//johnfitz -- stereo rendering -- full of hacky goodness
	if (r_stereo.value)
	{
		float eyesep = CLAMP(-8.0f, r_stereo.value, 8.0f);
		float fdepth = CLAMP(32.0f, r_stereodepth.value, 1024.0f);

		AngleVectors (r_refdef.viewangles, vpn, vright, vup);

		//render left eye (red)
		glColorMask(1, 0, 0, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.5 * eyesep * NEARCLIP / fdepth;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();
		R_DrawViewModel ();

		//render right eye (cyan)
		glClear (GL_DEPTH_BUFFER_BIT);
		glColorMask(0, 1, 1, 1);
		VectorMA (r_refdef.vieworg, 1.0f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = -frustum_skew;
		srand((int) (cl.time * 1000)); //sync random stuff between eyes

		R_RenderScene ();
		R_DrawViewModel ();

		//restore
		glColorMask(1, 1, 1, 1);
		VectorMA (r_refdef.vieworg, -0.5f * eyesep, vright, r_refdef.vieworg);
		frustum_skew = 0.0f;
	}
	else
	{
		R_RenderScene ();
		R_DrawViewModel ();
	}
	//johnfitz

	//johnfitz -- modified r_speeds output
	time2 = Sys_FloatTime ();
	if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky\n", 
					(int)((time2-time1)*1000), 
					rs_brushpolys, 
					rs_brushpasses, 
					rs_aliaspolys, 
					rs_aliaspasses, 
					rs_dynamiclightmaps, 
					rs_skypolys, 
					rs_skypasses);
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n", 
					(int)((time2-time1)*1000), 
					rs_brushpolys, 
					rs_aliaspolys, 
					rs_dynamiclightmaps);
	//johnfitz
}

