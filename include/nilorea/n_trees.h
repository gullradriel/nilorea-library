/**\file n_trees.h
 * trees module headers
 *\author Castagnier Mickael
 *\version 1.0
 *\date 07/08/2024
 */

#ifndef __NILOREA_TREES__
#define __NILOREA_TREES__

#ifdef __cplusplus
extern "C" {
#endif

#include "nilorea/n_common.h"
#include "nilorea/n_str.h"
#include "nilorea/n_log.h"
#include "nilorea/n_list.h"
#include "nilorea/n_hash.h"
#include "nilorea/n_3d.h"

/**\defgroup TREE TREE: various tree with generic support
  \addtogroup TREE
  @{
  */

/*! union of the possibles data values of a TREE node */
union NODE_DATA_TYPES {
    /*! integral type */
    int ival;
    /*! double type */
    double fval;
    /*! pointer type */
    void* ptr;
    /*! char *type */
    char* string;
    /*! N_STR *type */
    N_STR* nstr;
};

/*! structure of a TREE node data */
typedef struct NODE_DATA {
    /*! node value */
    union NODE_DATA_TYPES value;
    /*! node type */
    int32_t type;
} NODE_DATA;

/*! structure of a n-ary TREE node */
typedef struct TREE_NODE {
    /*! structure holding values for node */
    NODE_DATA data;
    /*! value destructor if of type ptr and specified, else a simple free will be used */
    void (*destroy_func)(void* ptr);
    /*! pointer to parent */
    struct TREE_NODE* parent;
    /*! pointer to parent container of the TREE_NODE, LIST_NODE */
    LIST_NODE* parent_list_node;
    /*! ordered list of children */
    LIST* children;
} TREE_NODE;

/*! structure of a TREE */
typedef struct TREE {
    /*! pointer to first node */
    TREE_NODE* root;
    /*! mutex for thread safety (optional) */
    pthread_rwlock_t rwlock;
    /*! number of nodes in the tree */
    size_t nb_nodes;
    /*! height of the tree */
    size_t height;
} TREE;

// create a TREE
TREE* new_tree();
// create a TREE node
TREE_NODE* tree_create_node(NODE_DATA value, void (*destroy_func)(void* ptr));
// insert a node in the parent node
int tree_insert_child(TREE_NODE* parent, TREE_NODE* child);
// delete a TREE node
int tree_delete_node(TREE* tree, TREE_NODE* node);
// delete a TREE
void tree_destroy(TREE** tree);

/*! Enum for coordinate types */
enum COORD_TYPE {
    COORD_INT,
    COORD_FLOAT,
    COORD_DOUBLE
};

/*! Union to store the coordinate values */
typedef union {
    int i;
    float f;
    double d;
} COORD_VALUE;

/*! Structure for a POINT2D in the 2D space */
typedef struct POINT2D {
    COORD_VALUE
    /*! x coordinate */
    x,
        /*! y coordinate */
        y;
} POINT2D;

/*! Structure for a POINT3D in the 3D space */
typedef struct POINT3D {
    COORD_VALUE
    /*! x coordinate */
    x,
        /*! y coordinate */
        y,
        /*! z coordinate */
        z;
} POINT3D;

/*! function pointer types for comparison */
typedef int (*compare_func)(COORD_VALUE a, COORD_VALUE b);
/*! function pointer types for debug print */
typedef void (*print_func)(COORD_VALUE val);

/*! structure of a quad tree node */
typedef struct QUADTREE_NODE {
    /*! X coordinate */
    COORD_VALUE x;
    /*! Y coordinate */
    COORD_VALUE y;
    /*! X,Y point */
    POINT2D point;
    /*! Pointer to data, can be NULL */
    void* data_ptr;
    /*! North-West child */
    struct QUADTREE_NODE* nw;
    /*! North-East child */
    struct QUADTREE_NODE* ne;
    /*! South-West child */
    struct QUADTREE_NODE* sw;
    /*! South-East child */
    struct QUADTREE_NODE* se;
} QUADTREE_NODE;

/*! structure of a quad tree */
typedef struct QUADTREE {
    /*! type of coordinate used in the quad tree */
    int coord_type;
    /*! pointer to comparison function */
    compare_func compare;
    /*! pointer to print function */
    print_func print;
    /*! tree list first node */
    QUADTREE_NODE* root;
} QUADTREE;

// Function to create a new quad tree
QUADTREE* create_quadtree(int coord_type);
// Function to create a new quad tree node
QUADTREE_NODE* create_node(COORD_VALUE x, COORD_VALUE y, void* data_ptr);
// Function to search for a point in the quad tree
QUADTREE_NODE* search(QUADTREE* qt, QUADTREE_NODE* root, COORD_VALUE x, COORD_VALUE y);
// Function to insert a point into the quad tree
void insert(QUADTREE* qt, QUADTREE_NODE** root, COORD_VALUE x, COORD_VALUE y, void* data_ptr);
// Function to free the quad tree
void free_quadtree(QUADTREE_NODE* root);

/*! structure of an OCTREE node */
typedef struct OCTREE_NODE {
    /*! Point represented by this node */
    POINT3D point;
    /*! Pointer to additional data, can be NULL */
    void* data_ptr;
    /*! Child nodes */
    struct OCTREE_NODE* children[8];
} OCTREE_NODE;

/*! structure of an OCTREE */
typedef struct {
    /*! tree list first node */
    OCTREE_NODE* root;
    /*! Coordinate type for the entire tree */
    int coord_type;
} OCTREE;

// Function prototypes
OCTREE* create_octree(int type);
OCTREE_NODE* create_octree_node(POINT3D point, void* data_ptr);
void insert_octree(OCTREE* OCTREE, POINT3D point, void* data_ptr);
void free_octree_node(OCTREE_NODE* node);
void free_octree(OCTREE* OCTREE);

#ifdef __cplusplus
}
#endif

/**
  @}
  */

#endif  // header guard
