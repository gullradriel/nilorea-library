/**
 *@file n_aabb.h
 *@brief AABB3D module headers
 *@author Castagnier Mickael
 *@version 1.0
 *@date 07/08/2024
 */

#ifndef __NILOREA_AABB3D__
#define __NILOREA_AABB3D__

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_common.h"
#include "nilorea/n_str.h"
#include "nilorea/n_log.h"

/**@defgroup AABB3D AABB3D: simple space area management API
  @addtogroup AABB3D
  @{
  */

/*! type of a AABB component */
typedef double AABB_VALUE;

/*! Axis-Aligned Bounding Box (AABB) algorithm */
typedef struct AABB3D {
    AABB_VALUE /*! first edge x coordinate */
        xMin,
        /*! first edge y coordinate */
        yMin,
        /*! first edge z coordinate */
        zMin;
    AABB_VALUE /*! second edge x coordinate */
        xMax,
        /*! second edge y coordinate */
        yMax,
        /*! second edge z coordinate */
        zMax;
} AABB3D;

// Function to create a 3D AABB
AABB3D createAABB3D(AABB_VALUE xMin, AABB_VALUE yMin, AABB_VALUE zMin, AABB_VALUE xMax, AABB_VALUE yMax, AABB_VALUE zMax);

// Function to check if a point is inside a 3D AABB
bool isPointInsideAABB3D(AABB3D box, AABB_VALUE x, AABB_VALUE y, AABB_VALUE z);

// Function to check if two 3D AABBs intersect
bool doAABB3DsIntersect(AABB3D box1, AABB3D box2);

#ifdef __cplusplus
}
#endif
/**
  @}
  */

#endif  // header guard
