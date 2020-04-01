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

//r_alias.c -- alias model rendering

#include "quakedef.h"

extern qboolean mtexenabled; //johnfitz
extern cvar_t r_drawflat, gl_overbright_models, gl_fullbrights; //johnfitz

//up to 16 color translated skins 
gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t	shadevector;

extern vec3_t	lightcolor; //johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;


/*
=============
GL_DrawAliasShadow -- johnfitz -- moved some code here from other places
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

	//FIXME: orient shadow onto "lightplane" (a global mplane_t*)

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
	glColor3f (1,1,1);
	glPopMatrix ();
}

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame (aliashdr_t *paliashdr, int posenum)
{
	float	s, t;
	vec3_t 	vertcolor; //johnfitz -- replaces "float l" for lit support
	int		i, j;
	int		index;
	trivertx_t	*v, *verts;
	int		list;
	int		*commands; //johnfitz -- renamed from "*order" to reduce confusion
	vec3_t	point;
	float	*normal;
	int		count;

	lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	commands = (int *)((byte *)paliashdr + paliashdr->commands);

	while (1)
	{
		// get the vertex count and primitive type
		count = *commands++;
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
			//johnfitz -- multitexture
			if(mtexenabled)
			{
				GL_MTexCoord2fFunc (TEXTURE0, ((float *)commands)[0], ((float *)commands)[1]);
				GL_MTexCoord2fFunc (TEXTURE1, ((float *)commands)[0], ((float *)commands)[1]);
			}
			else
				glTexCoord2f (((float *)commands)[0], ((float *)commands)[1]);
			//johnfitz

			commands += 2;
			
			//johnfitz -- r_drawflat, lit support
			if (r_drawflat_cheatsafe)
			{
				srand(count * (unsigned int) commands);
				glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
			}
			else if (r_fullbright_cheatsafe || r_lightmap_cheatsafe)
			{
				glColor3f(1,1,1);
			}
			else
			{
				vertcolor[0] = shadedots[verts->lightnormalindex] * lightcolor[0]; 
				vertcolor[1] = shadedots[verts->lightnormalindex] * lightcolor[1];
				vertcolor[2] = shadedots[verts->lightnormalindex] * lightcolor[2];
				glColor3fv (vertcolor);
			}
			//johnfitz

			glVertex3f (verts->v[0], verts->v[1], verts->v[2]);
			verts++;
		} while (--count);

		glEnd ();
	}

	rs_aliaspasses += paliashdr->numtris; //johnfitz
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
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;

	R_LightPoint (e->origin);

	//add dlights
	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (currententity->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - Length(dist);
			if (add > 0)
				VectorMA (lightcolor, add, cl_dlights[i].color, lightcolor);
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (currententity > cl_entities && currententity <= cl_entities + cl.maxclients)
	{
		add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	if (gl_overbright_models.value)
	{
		add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add < 1.0f)
			VectorScale(lightcolor, add, lightcolor);
	}

	//hack up the brightness when fullbrights but no overbrights (256)
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (!strcmp (e->model->name, "progs/flame2.mdl") ||
			!strcmp (e->model->name, "progs/flame.mdl") ||
			!strcmp (e->model->name, "progs/boss.mdl"))
		{
			lightcolor[0] = 256.0f;
			lightcolor[1] = 256.0f;
			lightcolor[2] = 256.0f;
		}

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	VectorScale(lightcolor, 1.0f / 200.0f, lightcolor);
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

	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
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
	if (!gl_fullbrights.value)
		fb = NULL;

	//
	// draw it
	//
	if (r_drawflat_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		R_SetupAliasFrame (currententity->frame, paliashdr);
		glEnable (GL_TEXTURE_2D);
		srand((int) (cl.time * 1000)); //restore randomness
		goto cleanup;
	}

	if (r_fullbright_cheatsafe || !cl.worldmodel->lightdata)
	{
		GL_Bind (tx);
		R_SetupAliasFrame (currententity->frame, paliashdr);
		goto cleanup;
	}

	if (r_lightmap_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		R_SetupAliasFrame (currententity->frame, paliashdr);
		glEnable (GL_TEXTURE_2D);
		goto cleanup;
	}

	if (gl_overbright_models.value && !r_drawflat_cheatsafe)
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

cleanup:
	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel (GL_FLAT);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	glPopMatrix ();

	if (r_shadows.value)
	{
		glDepthMask(GL_FALSE);
		GL_DrawAliasShadow (paliashdr, lastposenum);
		glDepthMask(GL_TRUE);
	}
}