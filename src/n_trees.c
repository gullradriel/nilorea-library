/**
 *@file n_trees.c
 *@brief Tree functions
 *@author Castagnier Mickael
 *@version 1.0
 *@date 07/08/2024
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "nilorea/n_common.h"
#include "nilorea/n_log.h"
#include "nilorea/n_str.h"
#include "nilorea/n_trees.h"

/**
 * @brief create a new TREE
 * @return a pointer to the new tree or NULL on failure
 */
TREE* new_tree() {
    TREE* tree = NULL;
    Malloc(tree, TREE, 1);
    __n_assert(tree, n_log(LOG_ERR, "Failed to allocate memory for tree"); return NULL;);

    tree->root = NULL;
    tree->nb_nodes = 0;
    tree->height = 0;
    pthread_rwlock_init(&(tree->rwlock), NULL);

    return tree;
}

/**
 * @brief create a TREE node
 * @param value NODE_DATA containing the value to add
 * @param destroy_func a destroy function pointer for the eventual private data_ptr that will later on be attached to the node
 * @return a pointer to the new node or NULL on failure
 */
TREE_NODE* tree_create_node(NODE_DATA value, void (*destroy_func)(void* ptr)) {
    TREE_NODE* node = (TREE_NODE*)malloc(sizeof(TREE_NODE));
    if (node == NULL) {
        n_log(LOG_ERR, "Failed to allocate memory for tree node");
        return NULL;
    }

    node->data = value;
    node->destroy_func = destroy_func;
    node->parent_list_node = NULL;
    node->parent = NULL;
    node->children = new_generic_list(MAX_LIST_ITEMS);

    return node;
}

/**
 * @brief insert a child node into the parent node
 * @param parent the parent TREE_NODE
 * @param child the new TREE_NODE to add as a child
 * @return TRUE on success or FALSE on failure
 */
int tree_insert_child(TREE_NODE* parent, TREE_NODE* child) {
    if (parent == NULL || child == NULL) {
        return FALSE;
    }

    if (parent->children == NULL) {
        parent->children = new_generic_list(MAX_LIST_ITEMS);
        __n_assert(parent->children, n_log(LOG_ERR, "Failed to create parent->children list"); return FALSE;);
    }

    LIST_NODE* node = new_list_node(child, child->destroy_func);
    __n_assert(node, n_log(LOG_ERR, "Failed to create child node"); return FALSE;);

    if (!list_node_push(parent->children, node)) {
        if (node->destroy_func != NULL) {
            node->destroy_func(node->ptr);
        }
        Free(node);
        return FALSE;
    }

    child->parent_list_node = node;
    child->parent = parent;

    return TRUE;
}

/**
 * @brief delete a TREE node
 * @param tree the TREE in which we want to delete a node
 * @param node the TREE node we want to delete
 * @return TRUE on success or FALSE on failure
 */
int tree_delete_node(TREE* tree, TREE_NODE* node) {
    if (tree == NULL || node == NULL) {
        return FALSE;
    }

    // Recursively delete child nodes
    LIST_NODE* child_node = node->children->start;
    while (child_node) {
        TREE_NODE* child = (TREE_NODE*)child_node->ptr;
        tree_delete_node(tree, child);
        child_node = child_node->next;
    }

    // Remove the node from its parent's child list, if applicable
    if (node->parent && node->parent->children) {
        remove_list_node_f(node->parent->children, node->parent_list_node);
    }

    // Free node data if necessary
    if (node->destroy_func) {
        node->destroy_func(node->data.value.ptr);
    } else {
        free(node->data.value.ptr);
    }

    // Free the node itself
    free(node);
    tree->nb_nodes--;

    return TRUE;
}

/**
 * @brief destroy a TREE
 * @param tree a pointer to the pointer of the TREE to destroy
 */
void tree_destroy(TREE** tree) {
    if (tree == NULL || *tree == NULL) {
        return;
    }

    TREE_NODE* root = (*tree)->root;
    if (root != NULL) {
        tree_delete_node(*tree, root);
    }

    pthread_rwlock_destroy(&((*tree)->rwlock));
    free(*tree);
    *tree = NULL;
}

/**
 *@brief int comparison function
 *@param a COORD_VALUE int element to compare
 *@param b COORD_VALUE int element to compare
 *@return TRUE or FALSE
 */
int compare_int(COORD_VALUE a, COORD_VALUE b) {
    return a.i - b.i;
}

/**
 *@brief float comparison function
 *@param a COORD_VALUE float element to compare
 *@param b COORD_VALUE float element to compare
 *@return TRUE or FALSE
 */
int compare_float(COORD_VALUE a, COORD_VALUE b) {
    return (a.f > b.f) - (a.f < b.f);
}

/**
 *@brief double comparison functions
 *@param a COORD_VALUE double element to compare
 *@param b COORD_VALUE double element to compare
 *@return TRUE or FALSE
 */
int compare_double(COORD_VALUE a, COORD_VALUE b) {
    return (a.d > b.d) - (a.d < b.d);
}

/**
 * @brief print int function
 * @param val the COORD_VALUE from which to read the int to print
 */
void print_int(COORD_VALUE val) {
    printf("%d", val.i);
}

/**
 * @brief print float function
 * @param val the COORD_VALUE from which to read the float to print
 */

void print_float(COORD_VALUE val) {
    printf("%f", val.f);
}

/**
 * @brief print double function
 * @param val the COORD_VALUE from which to read the double to print
 */
void print_double(COORD_VALUE val) {
    printf("%lf", val.d);
}

/**
 * @brief Function to create a new quad tree
 * @param coord_type the type of nodes for the new tree (COORD_INT,COORD_FLOAT,COORD_DOUBLE)
 * @return a new empty QUADTREE
 */
QUADTREE* create_quadtree(int coord_type) {
    QUADTREE* qt = (QUADTREE*)malloc(sizeof(QUADTREE));
    if (!qt) {
        n_log(LOG_ERR, "Failed to allocate QUADTREE");
        return NULL;
    }

    qt->coord_type = coord_type;
    qt->root = NULL;

    switch (coord_type) {
        case COORD_INT:
            qt->compare = compare_int;
            qt->print = print_int;
            break;
        case COORD_FLOAT:
            qt->compare = compare_float;
            qt->print = print_float;
            break;
        case COORD_DOUBLE:
            qt->compare = compare_double;
            qt->print = print_double;
            break;
    }

    return qt;
}

/**
 * @brief function to create a new quad tree node
 * @param x x coordinate
 * @param y y coordinate
 * @param data_ptr node private data pointer, can be NULL
 * @return a new QUADTREE_NODE or NULL
 * */
QUADTREE_NODE* create_node(COORD_VALUE x, COORD_VALUE y, void* data_ptr) {
    QUADTREE_NODE* node = (QUADTREE_NODE*)malloc(sizeof(QUADTREE_NODE));
    if (!node) {
        n_log(LOG_ERR, "Failed to allocate QUADTREE_NODE");
        return NULL;
    }
    node->x = x;
    node->y = y;
    node->data_ptr = data_ptr;
    node->nw = NULL;
    node->ne = NULL;
    node->sw = NULL;
    node->se = NULL;
    return node;
}

/**
 * @brief Function to insert a point into the quad tree
 * @param qt the target quad tree
 * @param root the QUADTREE_NODE to insert
 * @param x x coordinate
 * @param y y coordinate
 * @param data_ptr node private data pointer, can be NULL
 */
void insert(QUADTREE* qt, QUADTREE_NODE** root, COORD_VALUE x, COORD_VALUE y, void* data_ptr) {
    if (*root == NULL) {
        *root = create_node(x, y, data_ptr);
        return;
    }

    if (qt->compare(x, (*root)->x) < 0 && qt->compare(y, (*root)->y) < 0) {
        insert(qt, &((*root)->sw), x, y, data_ptr);
    } else if (qt->compare(x, (*root)->x) < 0 && qt->compare(y, (*root)->y) >= 0) {
        insert(qt, &((*root)->nw), x, y, data_ptr);
    } else if (qt->compare(x, (*root)->x) >= 0 && qt->compare(y, (*root)->y) < 0) {
        insert(qt, &((*root)->se), x, y, data_ptr);
    } else if (qt->compare(x, (*root)->x) >= 0 && qt->compare(y, (*root)->y) >= 0) {
        insert(qt, &((*root)->ne), x, y, data_ptr);
    }
}

/**
 * @brief Function to search for a point in the quad tree
 * @param qt the target QUADTREE in which to search
 * @param root the starting QUADTREE_NODE in the tree
 * @param x x coordinate
 * @param y y coordinate
 * @return the QUADTREE_NODE found at coordinate or NULL
 */
QUADTREE_NODE* search(QUADTREE* qt, QUADTREE_NODE* root, COORD_VALUE x, COORD_VALUE y) {
    if (root == NULL || (qt->compare(root->x, x) == 0 && qt->compare(root->y, y) == 0)) {
        return root;
    }

    if (qt->compare(x, root->x) < 0 && qt->compare(y, root->y) < 0) {
        return search(qt, root->sw, x, y);
    } else if (qt->compare(x, root->x) < 0 && qt->compare(y, root->y) >= 0) {
        return search(qt, root->nw, x, y);
    } else if (qt->compare(x, root->x) >= 0 && qt->compare(y, root->y) < 0) {
        return search(qt, root->se, x, y);
    } else if (qt->compare(x, root->x) >= 0 && qt->compare(y, root->y) >= 0) {
        return search(qt, root->ne, x, y);
    }

    return NULL;  // Should not reach here
}

/**
 * @brief Function to free the quad tree
 * @param root target quad tree to free
 */
void free_quadtree(QUADTREE_NODE* root) {
    __n_assert(root, return);

    free_quadtree(root->nw);
    free_quadtree(root->ne);
    free_quadtree(root->sw);
    free_quadtree(root->se);

    free(root);
}

/**
 * @brief create and OCTREE node
 * @param point the POINT3D representing the coordinate
 * @param data_ptr the data to insert. Can be NULL
 * @return a new OCTREE_NODE or NULL
 */
OCTREE_NODE* create_octree_node(POINT3D point, void* data_ptr) {
    OCTREE_NODE* node = (OCTREE_NODE*)malloc(sizeof(OCTREE_NODE));
    if (!node) {
        n_log(LOG_ERR, "Failed to allocate OCTREE_NODE");
        return NULL;
    }

    node->point = point;
    node->data_ptr = data_ptr;
    for (int i = 0; i < 8; i++) {
        node->children[i] = NULL;
    }

    return node;
}

/**
 * @brief Create a new OCTREE with a specified coordinate type
 * @param type the type of coordinate used by the OCTREE (COORD_INT,COORD_FLOAT,COORD_DOUBLE)
 * @return a new empty OCTREE or NULL
 */
OCTREE* create_octree(int type) {
    OCTREE* octree = (OCTREE*)malloc(sizeof(OCTREE));
    if (!octree) {
        n_log(LOG_ERR, "Failed to allocate OCTREE");
        return NULL;
    }

    octree->root = NULL;
    octree->coord_type = type;

    return octree;
}

/**
 * @brief function to determine the octant for the given point relative to the node
 * @param point POINT3D coordinates of the target point
 * @param center POINT3D coordinates of the center point
 * @param type the type of coordinate used by the OCTREE (COORD_INT,COORD_FLOAT,COORD_DOUBLE)
 * @return TRUE or FALSE
 */
int determine_octant(POINT3D point, POINT3D center, int type) {
    int octant = 0;
    if (type == COORD_INT) {
        if (point.x.i >= center.x.i) octant |= 4;
        if (point.y.i >= center.y.i) octant |= 2;
        if (point.z.i >= center.z.i) octant |= 1;
    } else if (type == COORD_FLOAT) {
        if (point.x.f >= center.x.f) octant |= 4;
        if (point.y.f >= center.y.f) octant |= 2;
        if (point.z.f >= center.z.f) octant |= 1;
    } else if (type == COORD_DOUBLE) {
        if (point.x.d >= center.x.d) octant |= 4;
        if (point.y.d >= center.y.d) octant |= 2;
        if (point.z.d >= center.z.d) octant |= 1;
    }
    return octant;
}

/**
 * @brief recursive function to insert a point into the OCTREE
 * @param node the target OCTREE_NODE *node
 * @param point the POINT3D point to insert
 * @param data_ptr eventual private data forthe point, can be NULL
 * @param type the type of coordinate used by the OCTREE (COORD_INT,COORD_FLOAT,COORD_DOUBLE)
 */
void insert_octree_node(OCTREE_NODE* node, POINT3D point, void* data_ptr, int type) {
    int octant = determine_octant(point, node->point, type);
    if (!node->children[octant]) {
        node->children[octant] = create_octree_node(point, data_ptr);
    } else {
        insert_octree_node(node->children[octant], point, data_ptr, type);
    }
}

/**
 * @brief Insert a point into the OCTREE
 * @param octree target OCTREE tree
 * @param point the POINT3D point to insert in the OCTREE
 * @param data_ptr eventual private data for the point, can be NULL
 */
void insert_octree(OCTREE* octree, POINT3D point, void* data_ptr) {
    if (!octree->root) {
        octree->root = create_octree_node(point, data_ptr);
    } else {
        insert_octree_node(octree->root, point, data_ptr, octree->coord_type);
    }
}

/**
 * @brief recursive function to free an OCTREE node and its children
 * @param node the starting node for the deletion
 */
void free_octree_node(OCTREE_NODE* node) {
    if (node) {
        for (int i = 0; i < 8; i++) {
            free_octree_node(node->children[i]);
        }
        free(node);
    }
}

/**
 * @brief free the OCTREE
 * @param octree the OCTREE to free
 */
void free_octree(OCTREE* octree) {
    if (octree) {
        free_octree_node(octree->root);
        free(octree);
    }
}
