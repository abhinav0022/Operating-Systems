#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
	point_set(p, point_X(p)+x, point_Y(p)+y);
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	return sqrt(pow(point_X(p2) - point_X(p1),2) + pow(point_Y(p2) - point_Y(p1),2));
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double dist_p1 = sqrt(pow(point_X(p1),2) + pow(point_Y(p1),2));
	double dist_p2 = sqrt(pow(point_X(p2),2) + pow(point_Y(p2),2));
	if (dist_p1 == dist_p2)
		return 0;
	else if (dist_p1 > dist_p2)
		return 1;
	return -1;
}
