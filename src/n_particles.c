/*
 * Nilorea Library
 * Copyright (C) 2005-2026 Castagnier Mickael
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 *@file n_particles.c
 *@brief particle function file for SantaHack 2012
 *@author Castagnier Mickael aka Gull Ra Driel
 *@version 1.0
 *@date 20/12/2012
 */

#include "nilorea/n_particles.h"
#include "math.h"

/**
 *@brief initialize a particle system
 *@param psys a pointer to a NULL initialized PARTICLE_SYSTEM pointer
 *@param max maximum number of particles in the system. Set to zero or negative to disable the limitation
 *@param x x position of the emitter
 *@param y y position of the emitter
 *@param z z position of the emitter
 *@param max_sprites maximum number of sprites used in the system. Value from UNLIMITED_LIST_ITEMS (0) to MAX_LIST_ITEMS (SIZE_MAX).
 *@return TRUE or FALSE
 */
int init_particle_system(PARTICLE_SYSTEM** psys, int max, double x, double y, double z, int max_sprites) {
    __n_assert(!(*psys), n_log(LOG_ERR, "particle system %p already initialized", (*psys)); return FALSE);

    Malloc((*psys), PARTICLE_SYSTEM, 1);
    __n_assert((*psys), return FALSE);

    start_HiTimer(&(*psys)->timer);

    size_t list_max = (max <= 0) ? UNLIMITED_LIST_ITEMS : (size_t)max;
    (*psys)->list = new_generic_list(list_max);

    (*psys)->source[0] = x;
    (*psys)->source[1] = y;
    (*psys)->source[2] = z;

    if (max_sprites > 0)
        (*psys)->sprites = (ALLEGRO_BITMAP**)calloc((size_t)max_sprites, sizeof(ALLEGRO_BITMAP*));
    else
        (*psys)->sprites = NULL;

    (*psys)->max_sprites = max_sprites;

    (*psys)->emitters = new_generic_list(UNLIMITED_LIST_ITEMS);

    return TRUE;
} /* init_particle_system() */

/**
 *@brief add a particle to a particle system
 *@param psys targeted particle system
 *@param spr sprite id of the particle, if any. Set to negative if there is no sprite for the particle
 *@param mode particle mode, NORMAL_PART:if sprite id then use sprite, else draw a pixel,SINUS_PART: snow moving effect,PIXEL_PART: rectfill with size
 *@param lifetime duration of the particle in msecs
 *@param size size of the particle, in pixels
 *@param color color of the particle
 *@param object PHYSICS object in which you set the particle initial position, speed, acceleration, rotation
 *@return TRUE or FALSE
 */
int add_particle(PARTICLE_SYSTEM* psys, int spr, int mode, int lifetime, int size, ALLEGRO_COLOR color, PHYSICS object) {
    int it = 0;

    PARTICLE* new_p = NULL;

    /* nb_max_items == 0 means unlimited (matches list_push behavior) */
    if (psys->list->nb_max_items > 0 && psys->list->nb_items >= psys->list->nb_max_items)
        return FALSE;

    for (it = 0; it < 3; it++) {
        object.position[it] += psys->source[it];
    }

    Malloc(new_p, PARTICLE, 1);
    __n_assert(new_p, return FALSE);

    new_p->depth_sort = 1; /* default: depth-sorted */
    new_p->spr_id = spr;
    new_p->mode = mode;
    new_p->lifetime = lifetime;
    new_p->color = color;
    new_p->size = size;

    /* Initialize extended fields for backward compatibility:
     * color_start == color_end means no lerp, age starts at 0 */
    new_p->color_start = color;
    new_p->color_end = color;
    new_p->age = 0;
    new_p->lifetime_max = lifetime;
    new_p->size_start = size;
    new_p->size_end = size;

    memcpy(&new_p->object, &object, sizeof(PHYSICS));

    return list_push(psys->list, new_p, &free);
} /* add_particle() */

/**
 *@brief add a particle to a particle system, all in line version (you have to set the PHYSICS object parameter in the function parameter instead of providing a PHYSICS object)
 *@param psys targeted particle system
 *@param spr sprite id of the particle, if any. Set to negative if there is no sprite for the particle
 *@param mode particle mode, NORMAL_PART:if sprite id then use sprite, else draw a pixel,SINUS_PART: snow moving effect,PIXEL_PART: rectfill with size
 *@param off_x x offset from particle source x position
 *@param off_y y offset from particle source y position
 *@param lifetime duration of the particle in msecs
 *@param size size of the particle, in pixels
 *@param color color of the particle
 *@param vx x speed of the particle
 *@param vy vy speed of the particle
 *@param vz z speed of the particle
 *@param ax x acceleration of the particle
 *@param ay y acceleration of the particle
 *@param az z acceleration of the particle
 *@return TRUE or FALSE
 */
int add_particle_ex(PARTICLE_SYSTEM* psys, int spr, int mode, int off_x, int off_y, int lifetime, int size, ALLEGRO_COLOR color, double vx, double vy, double vz, double ax, double ay, double az) {
    PHYSICS object;
    memset(&object, 0, sizeof(PHYSICS));
    VECTOR3D_SET(object.position, off_x, off_y, 0.0);
    VECTOR3D_SET(object.speed, vx, vy, vz);
    VECTOR3D_SET(object.acceleration, ax, ay, az);
    VECTOR3D_SET(object.orientation, 0.0, 0.0, 0.0);
    VECTOR3D_SET(object.angular_speed, 0.0, 0.0, 0.0);
    VECTOR3D_SET(object.angular_acceleration, 0.0, 0.0, 0.0);

    return add_particle(psys, spr, mode, lifetime, size, color, object);
} /* add_particle_ex () */

/* NOTE: manage_particle_ex delta_t is in MICROSECONDS, matching
 * update_physics_position() and the internal get_usec() timer.
 * The header doc says "msecs" but the actual unit is microseconds.
 * Lifetime and age fields on PARTICLE are in milliseconds. */

/**
 *@brief compute shape-offset spawn position for an emitter
 *@param em the emitter
 *@param out_x output x offset
 *@param out_y output y offset
 */
static void _emitter_shape_offset(const PARTICLE_EMITTER* em, double* out_x, double* out_y) {
    double ox = 0.0, oy = 0.0;
    switch (em->shape) {
        case 1: {                                                 /* line: random point along line of shape_w length */
            double t = ((double)rand() / (double)RAND_MAX) - 0.5; /* -0.5..0.5 */
            ox = t * em->shape_w;
            oy = 0.0;
            break;
        }
        case 2: { /* rect: random point in rectangle shape_w x shape_h */
            double tx = ((double)rand() / (double)RAND_MAX) - 0.5;
            double ty = ((double)rand() / (double)RAND_MAX) - 0.5;
            ox = tx * em->shape_w;
            oy = ty * em->shape_h;
            break;
        }
        case 3: { /* circle: random point in circle of radius shape_w */
            double angle = ((double)rand() / (double)RAND_MAX) * 2.0 * 3.14159265358979;
            double r = sqrt((double)rand() / (double)RAND_MAX) * em->shape_w;
            ox = cos(angle) * r;
            oy = sin(angle) * r;
            break;
        }
        default: /* point: no offset */
            break;
    }
    /* Apply rotation */
    if (em->shape_rotation != 0.0) {
        double cr = cos(em->shape_rotation);
        double sr = sin(em->shape_rotation);
        double rx = ox * cr - oy * sr;
        double ry = ox * sr + oy * cr;
        ox = rx;
        oy = ry;
    }
    *out_x = ox;
    *out_y = oy;
}

/**
 *@brief helper to generate a random double between min and max
 *@param vmin minimum value
 *@param vmax maximum value
 *@return random value in [vmin, vmax]
 */
static double _particle_rand_range(double vmin, double vmax) {
    if (vmin >= vmax) return vmin;
    return vmin + ((double)rand() / (double)RAND_MAX) * (vmax - vmin);
}

/**
 *@brief helper to generate a random int between min and max (inclusive)
 *@param imin minimum value
 *@param imax maximum value
 *@return random value in [imin, imax]
 */
static int _particle_rand_range_i(int imin, int imax) {
    if (imin >= imax) return imin;
    return imin + (rand() % (imax - imin + 1));
}

/**
 *@brief helper to linearly interpolate a color
 *@param a start color
 *@param b end color
 *@param t interpolation factor [0..1]
 *@return interpolated color
 */
static ALLEGRO_COLOR _color_lerp(ALLEGRO_COLOR a, ALLEGRO_COLOR b, float t) {
    float ar, ag, ab, aa, br, bg, bb, ba;
    al_unmap_rgba_f(a, &ar, &ag, &ab, &aa);
    al_unmap_rgba_f(b, &br, &bg, &bb, &ba);
    return al_map_rgba_f(ar + (br - ar) * t,
                         ag + (bg - ag) * t,
                         ab + (bb - ab) * t,
                         aa + (ba - aa) * t);
}

/**
 *@brief update particles positions using provided delta time
 *@param psys the targeted particle system
 *@param delta_t delta time to use, in microseconds
 *@return TRUE or FALSE
 */
int manage_particle_ex(PARTICLE_SYSTEM* psys, double delta_t) {
    __n_assert(psys, return FALSE);

    /* --- Emitter spawning phase --- */
    if (psys->emitters) {
        LIST_NODE* em_node = psys->emitters->start;
        while (em_node) {
            PARTICLE_EMITTER* em = (PARTICLE_EMITTER*)em_node->ptr;
            if (!em || !em->active) {
                em_node = em_node->next;
                continue;
            }

            if (em->burst_count > 0) {
                /* Burst mode: emit all at once then deactivate */
                for (int bi = 0; bi < em->burst_count; bi++) {
                    /* Enforce max_particles limit */
                    if (em->max_particles > 0 && em->live_count >= em->max_particles) break;
                    int lt = _particle_rand_range_i(em->lifetime_min, em->lifetime_max);
                    double vx = _particle_rand_range(em->velocity_min[0], em->velocity_max[0]);
                    double vy = _particle_rand_range(em->velocity_min[1], em->velocity_max[1]);
                    double vz = _particle_rand_range(em->velocity_min[2], em->velocity_max[2]);
                    double sox = 0.0, soy = 0.0;
                    _emitter_shape_offset(em, &sox, &soy);
                    add_particle_at(psys, em->spr_id, em->particle_mode,
                                    em->position[0] + sox, em->position[1] + soy, em->position[2],
                                    lt, em->size_start, em->size_end,
                                    em->color_start, em->color_end,
                                    vx, vy, vz,
                                    em->acceleration[0], em->acceleration[1], em->acceleration[2]);
                    /* Set per-particle flags and back-pointer from emitter */
                    if (psys->list->end) {
                        PARTICLE* np = (PARTICLE*)psys->list->end->ptr;
                        if (np) {
                            np->additive = em->additive_blend;
                            np->depth_sort = em->depth_sort;
                            np->emitter = em;
                            em->live_count++;
                        }
                    }
                }
                em->active = 0;
            } else {
                /* Continuous mode: accumulate particles over time.
                 * delta_t is in microseconds, emit_rate is particles/second */
                em->emit_accumulator += em->emit_rate * delta_t / 1000000.0;
                while (em->emit_accumulator >= 1.0) {
                    /* Enforce max_particles limit */
                    if (em->max_particles > 0 && em->live_count >= em->max_particles) {
                        em->emit_accumulator = 0.0;
                        break;
                    }
                    em->emit_accumulator -= 1.0;
                    int lt = _particle_rand_range_i(em->lifetime_min, em->lifetime_max);
                    double vx = _particle_rand_range(em->velocity_min[0], em->velocity_max[0]);
                    double vy = _particle_rand_range(em->velocity_min[1], em->velocity_max[1]);
                    double vz = _particle_rand_range(em->velocity_min[2], em->velocity_max[2]);
                    double sox = 0.0, soy = 0.0;
                    _emitter_shape_offset(em, &sox, &soy);
                    add_particle_at(psys, em->spr_id, em->particle_mode,
                                    em->position[0] + sox, em->position[1] + soy, em->position[2],
                                    lt, em->size_start, em->size_end,
                                    em->color_start, em->color_end,
                                    vx, vy, vz,
                                    em->acceleration[0], em->acceleration[1], em->acceleration[2]);
                    if (psys->list->end) {
                        PARTICLE* np = (PARTICLE*)psys->list->end->ptr;
                        if (np) {
                            np->additive = em->additive_blend;
                            np->depth_sort = em->depth_sort;
                            np->emitter = em;
                            em->live_count++;
                        }
                    }
                }
            }
            em_node = em_node->next;
        }
    }

    /* --- Particle tick phase --- */
    LIST_NODE* node = psys->list->start;

    while (node) {
        PARTICLE* ptr = (PARTICLE*)node->ptr;
        if (ptr->lifetime == -1) {
            /* infinite lifetime: just update position */
            update_physics_position(&ptr->object, delta_t);
            node = node->next;
        } else {
            ptr->lifetime = (int)(ptr->lifetime - (delta_t / 1000.0));
            if (ptr->lifetime > 0) {
                update_physics_position(&ptr->object, delta_t);

                /* Update age and lerp color/size.
                 * delta_t is in microseconds; age/lifetime are in milliseconds */
                ptr->age += (int)(delta_t / 1000.0);
                if (ptr->lifetime_max > 0) {
                    double ratio = (double)ptr->age / (double)ptr->lifetime_max;
                    if (ratio > 1.0) ratio = 1.0;
                    ptr->color = _color_lerp(ptr->color_start, ptr->color_end, (float)ratio);
                    ptr->size = ptr->size_start + (int)((double)(ptr->size_end - ptr->size_start) * ratio);
                }

                node = node->next;
            } else {
                LIST_NODE* node_to_kill = node;
                node = node->next;
                /* Decrement owning emitter's live count */
                if (ptr->emitter) {
                    ptr->emitter->live_count--;
                    if (ptr->emitter->live_count < 0) ptr->emitter->live_count = 0;
                }
                ptr = remove_list_node(psys->list, node_to_kill, PARTICLE);
                Free(ptr);
            }
        }
    }

    return TRUE;
} /* manage_particle_ex() */

/**
 *@brief update particles positions using particle system internal timer
 *@param psys the targeted particle system
 *@return TRUE or FALSE
 */
int manage_particle(PARTICLE_SYSTEM* psys) {
    __n_assert(psys, return FALSE);

    double delta_t = (double)get_usec(&psys->timer);
    return manage_particle_ex(psys, delta_t);
} /* manage_particle() */

/**
 *@brief draw particles of a particle system
 *@param psys the targeted particle system
 *@param xpos camera x position
 *@param ypos camera y position
 *@param w width of the current display
 *@param h height of the current display
 *@param range display border tolerance, if( ( x < -range ) || ( x > ( w + range ) ) || ( y< -range ) || ( y > ( h + range ) ) ) next ;
 *@return TRUE or FALSE
 */
int draw_particle(PARTICLE_SYSTEM* psys, double xpos, double ypos, int w, int h, double range) {
    __n_assert(psys, return FALSE);

    LIST_NODE* node = NULL;
    PARTICLE* ptr = NULL;

    node = psys->list->start;

    while (node) {
        double x = 0, y = 0;

        ptr = (PARTICLE*)node->ptr;
        x = ptr->object.position[0] - xpos;
        y = ptr->object.position[1] - ypos;

        if ((x < -range) || (x > (w + range)) || (y < -range) || (y > (h + range))) {
            node = node->next;
            continue;
        }

        for (int it = 0; it < 3; it++) {
            while (ptr->object.orientation[it] < 0.0)
                ptr->object.orientation[it] += 256.0;

            if (ptr->object.orientation[it] >= 256.0)
                ptr->object.orientation[it] = fmod(ptr->object.orientation[it], 256.0);
        }

        if (ptr->mode == SINUS_PART) {
            if (ptr->object.speed[0] != 0)
                x = x + ptr->object.speed[0] * sin((ptr->object.position[0] / ptr->object.speed[0]));
            else
                x = x + ptr->object.speed[0] * sin(ptr->object.position[0]);

            if (ptr->object.speed[1] != 0)
                y = y + ptr->object.speed[1] * cos((ptr->object.position[1] / ptr->object.speed[1]));
            else
                y = y + ptr->object.speed[1] * sin(ptr->object.position[1]);

            if (ptr->spr_id >= 0 && ptr->spr_id < psys->max_sprites && psys->sprites[ptr->spr_id]) {
                int spr_w = al_get_bitmap_width(psys->sprites[ptr->spr_id]);
                int spr_h = al_get_bitmap_height(psys->sprites[ptr->spr_id]);

                al_draw_rotated_bitmap(psys->sprites[ptr->spr_id], (float)(spr_w / 2), (float)(spr_h / 2), (float)(x - spr_w / 2), (float)(y - spr_h / 2), (float)(ptr->object.orientation[2] * (2.0 * M_PI / 256.0)), 0);
            } else
                al_draw_circle((float)x, (float)y, (float)ptr->size, ptr->color, 1);
        }

        if (ptr->mode & NORMAL_PART) {
            if (ptr->spr_id >= 0 && ptr->spr_id < psys->max_sprites && psys->sprites[ptr->spr_id]) {
                int bmp_w = al_get_bitmap_width(psys->sprites[ptr->spr_id]);
                int bmp_h = al_get_bitmap_height(psys->sprites[ptr->spr_id]);

                al_draw_rotated_bitmap(psys->sprites[ptr->spr_id], (float)(bmp_w / 2), (float)(bmp_h / 2), (float)(x - bmp_w / 2), (float)(y - bmp_h / 2), (float)(ptr->object.orientation[2] * (2.0 * M_PI / 256.0)), 0);
            } else
                al_draw_circle((float)x, (float)y, (float)ptr->size, ptr->color, 1);
        } else if (ptr->mode & PIXEL_PART) {
            al_draw_filled_rectangle((float)(x - ptr->size), (float)(y - ptr->size), (float)(x + ptr->size), (float)(y + ptr->size), ptr->color);
        } else
            al_draw_circle((float)x, (float)y, (float)ptr->size, ptr->color, 1);
        node = node->next;
    }

    return TRUE;
} /* draw_particle() */

/**
 *@brief destroy and free a particle system
 *@param psys a pointer to the particle system to destroy
 *@return TRUE or FALSE
 */
int free_particle_system(PARTICLE_SYSTEM** psys) {
    __n_assert((*psys), return FALSE);

    PARTICLE* particle = NULL;
    while ((*psys)->list->start) {
        particle = remove_list_node((*psys)->list, (*psys)->list->start, PARTICLE);
        Free(particle);
    }
    list_destroy(&(*psys)->list);
    free_emitters((*psys));
    list_destroy(&(*psys)->emitters);
    FreeNoLog((*psys)->sprites);
    Free((*psys));

    return TRUE;
} /* free_particle_system() */

/**
 *@brief move all particles of a particle system by a given offset
 *@param psys the targeted particle system
 *@param vx x move
 *@param vy y move
 *@param vz z move
 *@return TRUE or FALSE
 */
int move_particles(PARTICLE_SYSTEM* psys, double vx, double vy, double vz) {
    __n_assert(psys, return FALSE);

    LIST_NODE* node = NULL;
    PARTICLE* ptr = NULL;

    node = psys->list->start;

    while (node) {
        ptr = (PARTICLE*)node->ptr;
        ptr->object.position[0] = ptr->object.position[0] + vx;
        ptr->object.position[1] = ptr->object.position[1] + vy;
        ptr->object.position[2] = ptr->object.position[2] + vz;
        node = node->next;
    }
    return TRUE;
} /* move_particles() */

/**
 *@brief add a new emitter to the particle system
 *@param psys the targeted particle system
 *@return pointer to the new emitter for caller to configure, or NULL on error
 */
PARTICLE_EMITTER* add_emitter(PARTICLE_SYSTEM* psys) {
    __n_assert(psys, return NULL);
    __n_assert(psys->emitters, return NULL);

    PARTICLE_EMITTER* em = NULL;
    Malloc(em, PARTICLE_EMITTER, 1);
    __n_assert(em, return NULL);

    memset(em, 0, sizeof(PARTICLE_EMITTER));
    em->attached_entity_id = -1;
    em->spr_id = -1;
    em->depth_sort = 1; /* default: depth-sorted (caller can override to 0 for overlay) */

    if (!list_push(psys->emitters, em, &free)) {
        Free(em);
        return NULL;
    }

    return em;
} /* add_emitter() */

/**
 *@brief remove an emitter from the particle system and free it
 *@param psys the targeted particle system
 *@param em the emitter to remove
 *@return TRUE or FALSE
 */
int remove_emitter(PARTICLE_SYSTEM* psys, PARTICLE_EMITTER* em) {
    __n_assert(psys, return FALSE);
    __n_assert(psys->emitters, return FALSE);
    __n_assert(em, return FALSE);

    /* Clear back-pointers on any particles owned by this emitter */
    LIST_NODE* pnode = psys->list ? psys->list->start : NULL;
    while (pnode) {
        PARTICLE* p = (PARTICLE*)pnode->ptr;
        if (p && p->emitter == em) {
            p->emitter = NULL;
        }
        pnode = pnode->next;
    }

    LIST_NODE* node = psys->emitters->start;
    while (node) {
        if (node->ptr == em) {
            PARTICLE_EMITTER* removed = remove_list_node(psys->emitters, node, PARTICLE_EMITTER);
            Free(removed);
            return TRUE;
        }
        node = node->next;
    }
    return FALSE;
} /* remove_emitter() */

/**
 *@brief free all emitters in the particle system
 *@param psys the targeted particle system
 *@return TRUE or FALSE
 */
int free_emitters(PARTICLE_SYSTEM* psys) {
    __n_assert(psys, return FALSE);
    if (!psys->emitters) return TRUE;

    while (psys->emitters->start) {
        PARTICLE_EMITTER* em = remove_list_node(psys->emitters, psys->emitters->start, PARTICLE_EMITTER);
        Free(em);
    }
    return TRUE;
} /* free_emitters() */

/**
 *@brief add a particle at an absolute world position (not relative to psys->source)
 *@param psys targeted particle system
 *@param spr sprite id (-1 for none)
 *@param mode particle draw mode
 *@param px absolute x position
 *@param py absolute y position
 *@param pz absolute z position
 *@param lifetime duration in msecs
 *@param size_start particle size at birth
 *@param size_end particle size at death
 *@param color_start particle start color
 *@param color_end particle end color (lerped over lifetime)
 *@param vx x velocity
 *@param vy y velocity
 *@param vz z velocity
 *@param ax x acceleration
 *@param ay y acceleration
 *@param az z acceleration
 *@return TRUE or FALSE
 */
int add_particle_at(PARTICLE_SYSTEM* psys, int spr, int mode, double px, double py, double pz, int lifetime, int size_start, int size_end, ALLEGRO_COLOR color_start, ALLEGRO_COLOR color_end, double vx, double vy, double vz, double ax, double ay, double az) {
    __n_assert(psys, return FALSE);

    /* nb_max_items == 0 means unlimited (matches list_push behavior) */
    if (psys->list->nb_max_items > 0 && psys->list->nb_items >= psys->list->nb_max_items)
        return FALSE;

    PARTICLE* new_p = NULL;
    Malloc(new_p, PARTICLE, 1);
    __n_assert(new_p, return FALSE);

    new_p->depth_sort = 1;
    new_p->spr_id = spr;
    new_p->mode = mode;
    new_p->lifetime = lifetime;
    new_p->lifetime_max = lifetime;
    new_p->age = 0;
    new_p->color = color_start;
    new_p->color_start = color_start;
    new_p->color_end = color_end;
    new_p->size = size_start;
    new_p->size_start = size_start;
    new_p->size_end = size_end;

    VECTOR3D_SET(new_p->object.position, px, py, pz);
    VECTOR3D_SET(new_p->object.speed, vx, vy, vz);
    VECTOR3D_SET(new_p->object.acceleration, ax, ay, az);
    VECTOR3D_SET(new_p->object.orientation, 0.0, 0.0, 0.0);
    VECTOR3D_SET(new_p->object.angular_speed, 0.0, 0.0, 0.0);
    VECTOR3D_SET(new_p->object.angular_acceleration, 0.0, 0.0, 0.0);

    return list_push(psys->list, new_p, &free);
} /* add_particle_at() */
