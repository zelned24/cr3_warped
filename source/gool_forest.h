#ifndef GOOL_FOREST_H
#define GOOL_FOREST_H
#include "gool_destroy.h"
/* Native hierarchy traversal composed with actual metadata teardown. The
 * engine still supplies visit (logic/physics/render), notification and audio
 * services. This composition never substitutes those bodies or activation.
 * visit's native return -25 starts one persistent deletion job. Parent/sibling
 * traversal resumes only after that job completes, even across work budgets.
 * Fixed cursors own no resources and perform no allocation. Keep the owner,
 * providers, graph and metadata stable on the same engine timeline. */
typedef struct {
    gool_tree tree;
    gool_destroy destroy;
    gool_destroy_services services;
    uint32_t deleting;
    int deletion_pending;
} gool_forest;
int gool_forest_begin(gool_forest *forest,const uint32_t roots[GOOL_TREE_ROOTS],
                      const gool_destroy_services *services);
int gool_forest_step(gool_forest *forest,uint32_t work_budget);
#endif
