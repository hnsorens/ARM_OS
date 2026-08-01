#ifndef TIMER_H
#define TIMER_H

#include "modules.h"
#include <stdint.h>
#include <api/timer.h>
#include <api/gic_v3.h>

EXTERN_IMPORT_INTERFACE(interrupt_manager, gic);

// Cached Red-Black tree
typedef struct timer_node {
    uint64_t expire_ns;
    int pid;
    void (*callback)(void* args);
    
    struct timer_node *left;
    struct timer_node *right;
    struct timer_node *parent;
    int color;
} timer_node_t;

typedef struct {
    struct timer_node *rb_node;
    struct timer_node *rb_leftmost;
} CachedRBTree;

static void rb_rotate_left(CachedRBTree *tree, struct timer_node *x) {
    struct timer_node *y = x->right;
    x->right = y->left;

    if (y->left != 0) {
        y->left->parent = x;
    }
    y->parent = x->parent;

    if (x->parent == 0) {
        tree->rb_node = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

static void rb_rotate_right(CachedRBTree *tree, struct timer_node *y) {
    struct timer_node *x = y->left;
    y->left = x->right;

    if (x->right != 0) {
        x->right->parent = y;
    }
    x->parent = y->parent;

    if (y->parent == 0) {
        tree->rb_node = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
}

void rb_insert_color_fixup(CachedRBTree *tree, struct timer_node *z) {
    while (z->parent != 0 && z->parent->color == 1) {
        struct timer_node *grandparent = z->parent->parent;

        if (z->parent == grandparent->left) {
            struct timer_node *uncle = grandparent->right;

            if (uncle != 0 && uncle->color == 1) {
                z->parent->color = 0;
                uncle->color = 0;
                grandparent->color = 1;
                z = grandparent;
            }
            else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rb_rotate_left(tree, z);
                }

                z->parent->color = 0;
                grandparent->color = 1;
                rb_rotate_right(tree, grandparent);
            }
        } else {
            struct timer_node *uncle = grandparent->left;

            if (uncle != 0 && uncle->color == 1) {
                z->parent->color = 0;
                uncle->color = 0;
                grandparent->color = 1;
                z = grandparent;
            }
            else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rb_rotate_right(tree, z);
                }

                z->parent->color = 0;
                grandparent->color = 1;
                rb_rotate_left(tree, grandparent);
            }
        }
    }

    tree->rb_node->color = 0;
}

void timer_tree_insert(CachedRBTree *tree, struct timer_node *new_node) {
    struct timer_node **link = &tree->rb_node;
    struct timer_node *parent = 0;

    int is_leftmost = 1;


    while (*link) {
        parent = *link;

        if (new_node->expire_ns < parent->expire_ns) {
            link = &parent->left;
        } else {
            link = &parent->right;
            is_leftmost = 0;
        }
    }

    new_node->parent = parent;
    new_node->left = 0;
    new_node->right = 0;
    new_node->color = 1;
    *link = new_node;

    if (is_leftmost) {
        tree->rb_leftmost = new_node;
    }

    rb_insert_color_fixup(tree, new_node);
}

void timer_handler(void* arg) {
    (void)arg;

    // get current time
    uint64_t val;
    __asm__ volatile("msr %0, cntpct_el0" : "=r" (val));

    // handle red black tree

    
}

void timer_init() {
	const irq_vector_t timer_irq = 30;

	int ret = gic.configure(timer_irq, IRQ_TRIGGER_LEVEL, 0x80);

	ret = gic.set_group(timer_irq, IRQ_GROUP_NON_SECURE);
	ret = gic.register_handler(timer_irq, timer_handler, NULL);

	gic.set_core_priority_mask(0xFF);

	/* Program the EL1 physical timer to fire after ~10 ns */
	uint64_t cntfrq;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));
	uint64_t cval;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(cval));
	cval += cntfrq / 1000000; /* ~10 ns */

	__asm__ volatile("msr cntp_cval_el0, %0" : : "r"(cval));

	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(1UL));
	__asm__ volatile("isb");

	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

int register_callback(uint32_t ticks_period, uint8_t periodic, timer_callback_t callback, timer_id_t* id, void *context) {

}

int unregister_callback(timer_id_t id) {

}

int pause(timer_id_t id) {

}

int resume(timer_id_t id) {

}

int modify(timer_id_t id, uint32_t new_period) {

}

int get_system_ticks(uint64_t *ticks) {

}

int delay_ticks(uint32_t ticks) {

}

#endif
