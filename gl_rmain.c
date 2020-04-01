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
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys, rs_dynamiclightmaps;

qboolean	envmap;				// true during envmap command capture 

int			currenttexture = -1;		// to avoid unnecessary texture sets
int			cnttextures[2] = {-1, -1};     // cached
int			particletexture; // little dot for particles
int			particletexture1, particletexture2, particletexture3; //johnfitz
int			screentexture;		// johnfitz
int			playertextures;		// up to 16 color translated skins

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


void R_MarkLeaves (void);

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
cvar_t	gl_texsort = {"gl_texsort","1"};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","1"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","0"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeys", "1"};

//johnfitz -- new cvars
cvar_t	r_clearcolor = {"r_clearcolor","2", true};
cvar_t	r_particles = {"r_particles","1", true};
cvar_t	r_drawflat = {"r_drawflat","0"};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0"};
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", true};
cvar_t	gl_farclip = {"gl_farclip", "8192", true};
cvar_t	gl_overbright_models = {"gl_overbright_models", "0", true};
//cvar_t	_gl_texturemode = {"_gl_texturemode", "GL_LINEAR_MIPMAP_LINEAR", true};
//johnfitz

extern	cvar_t	gl_ztrick;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int i;
	for (i=0 ; i<4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
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

    GL_Bind(frame->gl_texturenum);

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
float	shadelight, ambientlight;

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
	extern cvar_t chase_alpha; //johnfitz -- chasecam

	//johnfitz -- padded skins
	hscale = (float)paliashdr->skinwidth/(float)Pad(paliashdr->skinwidth);
	vscale = (float)paliashdr->skinheight/(float)Pad(paliashdr->skinheight);
	//johnfitz

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	//johnfitz -- chasecam
	if (currententity == &cl_entities[cl.viewentity] && chase_alpha.value < 1.0)
	{
		glEnable (GL_BLEND);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		//glDepthMask(0);
	}
	//johnfitz

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

			//johnfitz -- fullbrights in multitexture
			if(mtexenabled)
			{
				qglMTexCoord2fSGIS (TEXTURE0_SGIS, hscale*((float *)order)[0], vscale*((float *)order)[1]);
				qglMTexCoord2fSGIS (TEXTURE1_SGIS, hscale*((float *)order)[0], vscale*((float *)order)[1]);
			}
			else
				glTexCoord2f (hscale*((float *)order)[0], vscale*((float *)order)[1]);
			//johnfitz

			order += 2;

			//johnfitz -- fullbright models
			if (!r_fullbright.value /* && !r_fullbright_models.value */) 
				l = shadedots[verts->lightnormalindex] * shadelight; // normals and vertexes come from the frame list
			else
				l = 1.0;
			//johnfitz
			
			//johnfitz -- chase_alpha and r_drawflat support
			if (r_drawflat.value)
			{
				srand(count * (unsigned int) order);
				if (currententity == &cl_entities[cl.viewentity] && chase_alpha.value < 1.0)
					glColor4f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0, chase_alpha.value);
				else
					glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
			}
			else 
			{
				if (currententity == &cl_entities[cl.viewentity] && chase_alpha.value < 1.0)
					glColor4f (l, l, l, chase_alpha.value);
				else
					glColor3f (l, l, l);
			}
			//johnfitz

			glVertex3f (verts->v[0], verts->v[1], verts->v[2]);
			verts++;
		} while (--count);

		glEnd ();
	}

	//johnfitz -- chasecam
	if (currententity == &cl_entities[cl.viewentity] && chase_alpha.value < 1.0)
	{
		glDisable (GL_BLEND);	
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		//glDepthMask(1);
	}
	//johnfitz

	//johnfitz -- drawflat
	if (r_drawflat.value)
	{
		glEnable (GL_TEXTURE_2D);
		srand((int) (cl.time * 1000)); //restore randomness
	}
	//johnfitz
}


/*
=============
GL_DrawAliasShadow
=============
*/
extern	vec3_t			lightspot;

void GL_DrawAliasShadow (aliashdr_t *paliashdr, int posenum)
{
	float	s, t, l;
	int		i, j;
	int		index;
	trivertx_t	*v, *verts;
	int		list;
	int		*order;
	vec3_t	point;
	float	*normal;
	float	height, lheight;
	int		count;

	lheight = currententity->origin[2] - lightspot[2];

	height = 0;
	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	height = -lheight + 1.0;

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
			// (skipped for shadows) glTexCoord2fv ((float *)order);
			order += 2;

			// normals and vertexes come from the frame list
			point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;
//			height -= 0.001;
			glVertex3fv (point);

			verts++;
		} while (--count);

		glEnd ();
	}	
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
void R_SetupAliasLighting ()
{
	entity_t	*e;
	model_t		*mod;
	vec3_t		dist;
	float		add, an;
	int			lnum, i;

	e = currententity;
	mod = e->model;

	ambientlight = shadelight = R_LightPoint (e->origin);

	// allways give the gun some light
	if (e == &cl.viewent && ambientlight < 24)
		ambientlight = shadelight = 24;

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		if (cl_dlights[lnum].die >= cl.time)
		{
			VectorSubtract (currententity->origin,
							cl_dlights[lnum].origin,
							dist);
			add = cl_dlights[lnum].radius - Length(dist);

			if (add > 0) {
				ambientlight += add;
				//ZOID models should be affected by dlights as well
				shadelight += add;
			}
		}
	}

	// clamp lighting so it doesn't overbright as much
	if (ambientlight > 128)
		ambientlight = 128;
	if (ambientlight + shadelight > 192)
		shadelight = 192 - ambientlight;

	// ZOID: never allow players to go totally black
	i = currententity - cl_entities;
	if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		if (ambientlight < 8)
			ambientlight = shadelight = 8;

	//johnfitz -- hack up the brightness when fullbrights but no overbrights
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (!strcmp (mod->name, "progs/flame2.mdl") ||
			!strcmp (mod->name, "progs/flame.mdl") ||
		//	!strcmp (mod->name, "progs/bolt.mdl") ||
		//	!strcmp (mod->name, "progs/bolt2.mdl") ||
		//	!strcmp (mod->name, "progs/bolt3.mdl") ||
		//	!strcmp (mod->name, "progs/laser.mdl") ||
		//	!strcmp (mod->name, "progs/k_spike.mdl") ||
		//	!strcmp (mod->name, "progs/w_spike.mdl") ||
		//	!strcmp (mod->name, "progs/lavaball.mdl") ||
			!strcmp (mod->name, "progs/boss.mdl"))
			ambientlight = shadelight = 256;

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	shadelight = shadelight / 200.0;
	
	an = e->angles[1]/180*M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);
}

/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliashdr_t	*paliashdr;
	trivertx_t	*verts, *v;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	float		s, t;
	int			i, anim, index, texnum, fb; //johnfitz;

	clmodel = currententity->model;

	VectorAdd (currententity->origin, clmodel->mins, mins);
	VectorAdd (currententity->origin, clmodel->maxs, maxs);

	if (R_CullBox (mins, maxs))
		return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	R_SetupAliasLighting (); //johnfitz

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	rs_aliaspolys += paliashdr->numtris;

    glPushMatrix ();
	R_RotateForEntity (e);

	if (!strcmp (clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
		glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
// double size of eyes, since they are really hard to see in gl
		glScalef (paliashdr->scale[0]*2, paliashdr->scale[1]*2, paliashdr->scale[2]*2);
	} else {
		glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
		glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
	}

// johnfitz -- draw model with possible fullbright mask and overbrightening
	if (gl_smoothmodels.value && !r_drawflat.value)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	if (mtexenabled)
		GL_DisableMultitexture();

	// set up textures
	anim = (int)(cl.time*10) & 3;
	texnum = paliashdr->gl_texturenum[currententity->skinnum][anim];
	fb = paliashdr->fullbrightmasks[currententity->skinnum][anim];
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;
		if (i >= 1 && i<=cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		    texnum = playertextures - 1 + i;
	}
	if (!gl_fullbrights.value || r_drawflat.value || r_fullbright.value)
		fb = -1;

	//
	// draw it
	//
	if (gl_overbright_models.value)
	{
		//FIXME: add path for overbright using multitexture

		//overbright without multitexture
		GL_Bind(texnum);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		R_SetupAliasFrame (currententity->frame, paliashdr);

		glEnable(GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE);
		R_SetupAliasFrame (currententity->frame, paliashdr);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);

		if (fb != -1) //fullbright mask seperately
		{
			GL_Bind(fb);
			glEnable(GL_BLEND);
			glDepthMask(GL_FALSE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
	}
	else
	{
		if (fb == -1) //case 1: no fullbright mask
		{		
			GL_Bind(texnum);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if(gl_mtexable) //case 2: fullbright mask using multitexture
		{
			GL_SelectTexture(TEXTURE0_SGIS);
			GL_Bind (texnum);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			GL_EnableMultitexture(); // selects TEXTURE1_SGIS 
			GL_Bind (fb);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
			glEnable(GL_BLEND);

			R_SetupAliasFrame (currententity->frame, paliashdr);

			glDisable(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);	
		}
		else //case 3: fullbright mask without multitexture
		{
			GL_Bind(texnum);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			R_SetupAliasFrame (currententity->frame, paliashdr);
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
// johnfitz


	glPopMatrix ();

	if (r_shadows.value)
	{
		glPushMatrix ();
		R_RotateForEntity (e);
		glDisable (GL_TEXTURE_2D);
		glEnable (GL_BLEND);
		glColor4f (0,0,0,0.5);
		GL_DrawAliasShadow (paliashdr, lastposenum);
		glEnable (GL_TEXTURE_2D);
		glDisable (GL_BLEND);
		glColor4f (1,1,1,1);
		glPopMatrix ();
	}

}

//==================================================================================

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
			continue;
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
R_DrawViewEntity -- johnfitz -- used for drawing the playermodel when chasecam is active
=============
*/
void R_DrawViewEntity (void)
{
	if (!chase_active.value)
		return;

	currententity = &cl_entities[cl.viewentity];
	currententity->angles[0] *= 0.3;

	R_DrawAliasModel (currententity);
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	float		ambient[4], diffuse[4];
	int			j;
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	int			ambientlight, shadelight;

	if (!r_drawviewmodel.value)
		return;

	if (chase_active.value)
		return;

	if (envmap)
		return;

	if (!r_drawentities.value)
		return;

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		// allways give some light on gun
	ambientlight = j;
	shadelight = j;

// add dynamic lights		
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length(dist);
		if (add > 0)
			ambientlight += add;
	}

	ambient[0] = ambient[1] = ambient[2] = ambient[3] = (float)ambientlight / 128;
	diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = (float)shadelight / 128;

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));
	R_DrawAliasModel (currententity);
	glDepthRange (gldepthmin, gldepthmax);
}


/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (!gl_polyblend.value || cl.intermission) //johnfitz -- don't show blend during intermission
		return;
	if (!v_blend[3])
		return;

	GL_DisableMultitexture();

	glDisable (GL_ALPHA_TEST);
	glEnable (GL_BLEND);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_TEXTURE_2D);

    glLoadIdentity ();

    glRotatef (-90,  1, 0, 0);	    // put Z going up
    glRotatef (90,  0, 0, 1);	    // put Z going up

	glColor4fv (v_blend);

	glBegin (GL_QUADS);

	//johnfitz 100 changed to 120 to cover entire screen even when fov = 170
	glVertex3f (10, 120, 100);
	glVertex3f (10, -120, 100);
	glVertex3f (10, -120, -100);
	glVertex3f (10, 120, -100);
	//johnfitz
	glEnd ();

	glDisable (GL_BLEND);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_ALPHA_TEST);
}

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

	//johnfitz -- rendering statistics
	rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_dynamiclightmaps = 0;
}

/*
=============
GL_SetFrustum
johnfitz -- written to replace MYgluPerspective
=============
*/
void GL_SetFrustum(float fovy, float fovx, float zNear, float zFar )
{
   float xmax, ymax;
   ymax = zNear * tan( fovy * M_PI / 360.0 );
   xmax = zNear * tan( fovx * M_PI / 360.0 );
   glFrustum( -xmax, xmax, -ymax, ymax, zNear, zFar );
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
	int		x, x2, y2, y, w, h;
	float fovx, fovy; //johnfitz
	int contents; //johnfitz

	//
	// set up viewpoint
	//
	glMatrixMode(GL_PROJECTION);
    glLoadIdentity ();
	x = r_refdef.vrect.x * glwidth/vid.width;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth/vid.width;
	y = (vid.height-r_refdef.vrect.y) * glheight/vid.height;
	y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight/vid.height;

	// fudge around because of frac screen scale
	if (x > 0)
		x--;
	if (x2 < glwidth)
		x2++;
	if (y2 < 0)
		y2--;
	if (y < glheight)
		y++;

	w = x2 - x;
	h = y - y2;

	if (envmap)
	{
		x = y2 = 0;
		w = h = 256;
	}

	glViewport (glx + x, gly + y2, w, h);

	//johnfitz -- warp view for underwater
	fovx = r_refdef.fov_x;
	fovy = r_refdef.fov_y;
	if (r_waterwarp.value)
	{
		contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER ||
			contents == CONTENTS_SLIME ||
			contents == CONTENTS_LAVA)
		{
			//variance should be a percentage of width, where width = 2 * tan(fov / 2)
			//otherwise the effect is too dramatic at high FOV and too subtle at low FOV
			//what a mess!
			fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;

			//old method where variance was a percentage of fov
			//fovx = r_refdef.fov_x * (0.98 + sin(cl.time * 1.5) * 0.02);
			//fovy = r_refdef.fov_y * (1.02 - sin(cl.time * 1.5) * 0.02);
		}
	}
    GL_SetFrustum (fovy, fovx, 4, gl_farclip.value);
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
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	Fog_EnableGFog (); //johnfitz

	R_DrawWorld ();		// adds static entities to the list

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	GL_DisableMultitexture();

	R_RenderDlights (); //triangle fan dlights

	R_DrawViewEntity (); //johnfitz -- player model for chasecam

	R_DrawWaterSurfaces (); //johnfitz -- moved here from R_RenderView() -- particle z-buffer bug

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
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (offset)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);

		if (offset > 0)
			glPolygonOffset(1, offset);
		else
			glPolygonOffset(-1, offset);
	}
	else
		glDisable (GL_POLYGON_OFFSET_FILL);
}

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
	}
	else if (gl_finish.value)
		glFinish ();

	R_Clear ();

	R_RenderScene ();

	R_DrawViewModel ();

	R_PolyBlend ();

	if (r_speeds.value)
	{
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %4i lmap %4i sky\n", (int)((time2-time1)*1000), rs_brushpolys, rs_aliaspolys, rs_dynamiclightmaps, rs_skypolys);
	}
}

