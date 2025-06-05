/**\file n_aabb.c
 *  AABB3D functions
 *\author Castagnier Mickael
 *\version 1.0
 *\date 07/08/2024
 */

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_aabb.h"

/*!\fn AABB3D createAABB3D(AABB_VALUE xMin, AABB_VALUE yMin, AABB_VALUE zMin, AABB_VALUE xMax, AABB_VALUE yMax, AABB_VALUE zMax)
 * \brief create a new AABB3D box
 * \param xMin minimum x value for the box
 * \param yMin minimum y value for the box
 * \param zMin minimum z value for the box
 * \param xMax maximum x value for the box
 * \param yMax maximum y value for the box
 * \param zMax maximum z value for the box
 * \return a new AABB3D box
 */
AABB3D createAABB3D(AABB_VALUE xMin, AABB_VALUE yMin, AABB_VALUE zMin, AABB_VALUE xMax, AABB_VALUE yMax, AABB_VALUE zMax) {
    AABB3D box;
    box.xMin = xMin;
    box.yMin = yMin;
    box.zMin = zMin;
    box.xMax = xMax;
    box.yMax = yMax;
    box.zMax = zMax;
    return box;
}

/*!\fn bool isPointInsideAABB3D(AABB3D box, AABB_VALUE x, AABB_VALUE y, AABB_VALUE z)
 * \brief check if a point is inside a 3D AABB
 * \param box the box to use
 * \param x x coordinate of the point to test
 * \param y y coordinate of the point to test
 * \param z z coordinate of the point to test
 * \return 0 or 1
 */
bool isPointInsideAABB3D(AABB3D box, AABB_VALUE x, AABB_VALUE y, AABB_VALUE z) {
    return (x >= box.xMin && x <= box.xMax &&
            y >= box.yMin && y <= box.yMax &&
            z >= box.zMin && z <= box.zMax);
}

/*!\fn bool doAABB3DsIntersect(AABB3D box1, AABB3D box2)
 * \brief check if two 3D AABBs intersect
 * \param box1 the first box
 * \param box2 the second box
 * \return 0 or 1
 */
bool doAABB3DsIntersect(AABB3D box1, AABB3D box2) {
    return (box1.xMin <= box2.xMax && box1.xMax >= box2.xMin &&
            box1.yMin <= box2.yMax && box1.yMax >= box2.yMin &&
            box1.zMin <= box2.zMax && box1.zMax >= box2.zMin);
}
