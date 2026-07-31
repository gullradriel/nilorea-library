/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

/*! create a 3D AABB */
AABB3D createAABB3D(AABB_VALUE xMin, AABB_VALUE yMin, AABB_VALUE zMin, AABB_VALUE xMax, AABB_VALUE yMax, AABB_VALUE zMax);

/*! check if a point is inside a 3D AABB */
bool isPointInsideAABB3D(AABB3D box, AABB_VALUE x, AABB_VALUE y, AABB_VALUE z);

/*! check if two 3D AABBs intersect */
bool doAABB3DsIntersect(AABB3D box1, AABB3D box2);

#ifdef __cplusplus
}
#endif
/**
  @}
  */

#endif  // header guard
