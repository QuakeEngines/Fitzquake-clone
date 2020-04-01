int RayPlaneIntersection (const mplane_t *p, const vec3_t start, const vec3_t dir, vec3_t out)
//returns true if successful
//note: modified to eliminate the pointless vector normalization.  not tested!
{
	double scale, dot, dist;

	dot = DotProduct(p->normal, dir);

	if (fabs(dot) < 0.0000001)
		return false; // ray is parallel to plane

	dist = p->dist - DotProduct(p->normal, start);
	scale = dist / dot;

	VectorMA (start, scale, dir, out);

	return true;
}

void PlaneFromPoints (const vec3_t p1, const vec3_t p2, const vec3_t p3, mplane_t *plane)
//note: this probably should make sure the points aren't colinear
{
	vec3_t v1, v2;

	VectorSubtract (p3, p2, v1);
	VectorSubtract (p1, p2, v2);
	CrossProduct (v2, v1, plane->normal);
	VectorNormalize (plane->normal);
	plane->dist = DotProduct (p1, plane->normal);
}

#define SIDE_EPSILON 0.00001
int PointOnPlaneSide (float *v, mplane_t *p) //TODO: fast checks for axial planes
{
	vec3_t temp;
	float d;

	VectorScale(p->normal, p->dist, temp);
	VectorSubtract(v, temp, temp);

	if (VectorNormalize(temp) < 0.5)
		return SIDE_ON;

	d = DotProduct(p->normal, temp);

	if (d > SIDE_EPSILON)
		return SIDE_FRONT; //0
	if (d < -SIDE_EPSILON)
		return SIDE_BACK; //1
	return SIDE_ON; //2
}

#define	MAX_CLIP_VERTS 64
int ClipPolyToPlane (glpoly_t *p, mplane_t *clipplane)
//returns false if polygon was completely clipped away
{
	vec3_t	temp;
	float	*v, verts[MAX_CLIP_VERTS][3];
	int		i, j;
	byte	sideflags[MAX_CLIP_VERTS];

	//loop through points, testing for planeside
	for (i=0, v=p->verts[0]; i<p->numverts; i++, v+=VERTEXSIZE)
		sideflags[i] = PointOnPlaneSide (v, clipplane);

	//if all behind plane, return false
	for (i=0; i<p->numverts; i++)
		if (sideflags[i] != SIDE_BACK)
			goto clip;
	return false;

clip:

	//loop through points, inserting ON points and deleting BACK points
	for (i=0, j=0; i<p->numverts; i++)
	{
		//copy this vert if not behind plane
		if (sideflags[i] != SIDE_BACK)
		{
			VectorCopy(p->verts[i], verts[j]);
			j++;
		}

		//if this edge crosses the plane, insert a new vert
		if ((sideflags[i] == SIDE_BACK && sideflags[(i+1)%p->numverts] == SIDE_FRONT) ||
			(sideflags[i] == SIDE_FRONT && sideflags[(i+1)%p->numverts] == SIDE_BACK))
		{
			VectorSubtract(p->verts[i], p->verts[(i+1)%p->numverts], temp);
			if (RayPlaneIntersection (clipplane, p->verts[i], temp, verts[j]))
				j++;
		}

		//see if we have room for at least 2 more verts
		if (j+1 > sizeof(verts))
			Sys_Error("ClipPolyToPlane: too many verts");
	}

	//copy points back to glpoly
	for (i=0; i<j; i++)
		VectorCopy(verts[i], p->verts[i]); //WTF? how can this be safe????????
	p->numverts = j;

	return true;
}