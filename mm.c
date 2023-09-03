/**
 * @file mm.c
 * @brief A 64-bit struct-based implicit free list memory allocator
 *
 * 15-213: Introduction to Computer Systems
 *
 * TODO: insert your documentation here. :)
 *
 *************************************************************************
 *
 * ADVICE FOR STUDENTS.
 * - Step 0: Please read the writeup!
 * - Step 1: Write your heap checker.
 * - Step 2: Write contracts / debugging assert statements.
 * - Good luck, and have fun!
 *
 *************************************************************************
 *
 * @author Raman Saparkhan <rsaparkh@andrew.cmu.edu>
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* Do not change the following! */

#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mem_memset
#define memcpy mem_memcpy
#endif /* def DRIVER */

/* You can change anything from here onward */

/*
 *****************************************************************************
 * If DEBUG is defined (such as when running mdriver-dbg), these macros      *
 * are enabled. You can use them to print debugging output and to check      *
 * contracts only in debug mode.                                             *
 *                                                                           *
 * Only debugging macros with names beginning "dbg_" are allowed.            *
 * You may not define any other macros having arguments.                     *
 *****************************************************************************
 */
#ifdef DEBUG
/* When DEBUG is defined, these form aliases to useful functions */
#define dbg_requires(expr) assert(expr)
#define dbg_assert(expr) assert(expr)
#define dbg_ensures(expr) assert(expr)
#define dbg_printf(...) ((void)printf(__VA_ARGS__))
#define dbg_printheap(...) print_heap(__VA_ARGS__)
#else
/* When DEBUG is not defined, these should emit no code whatsoever,
 * not even from evaluation of argument expressions.  However,
 * argument expressions should still be syntax-checked and should
 * count as uses of any variables involved.  This used to use a
 * straightforward hack involving sizeof(), but that can sometimes
 * provoke warnings about misuse of sizeof().  I _hope_ that this
 * newer, less straightforward hack will be more robust.
 * Hat tip to Stack Overflow poster chqrlie (see
 * https://stackoverflow.com/questions/72647780).
 */
#define dbg_discard_expr_(...) ((void)((0) && printf(__VA_ARGS__)))
#define dbg_requires(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_assert(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_ensures(expr) dbg_discard_expr_("%d", !(expr))
#define dbg_printf(...) dbg_discard_expr_(__VA_ARGS__)
#define dbg_printheap(...) ((void)((0) && print_heap(__VA_ARGS__)))
#endif

/* Basic constants */

typedef uint64_t word_t;

/** @brief Word and header size (bytes) */
static const size_t wsize = sizeof(word_t);

/** @brief Double word size (bytes) */
static const size_t dsize = 2 * wsize;

/** @brief Minimum block size (bytes) */
static const size_t min_block_size = dsize;

/**
 * Chuncksize is a chunk of the memory that allocator receives for serving as a
 * space for malloc'ed memory. The further chunks can be requested in case of
 * overflow. It should be divisible by dsize because of the need for alignment
 */
static const size_t chunksize = (1 << 5);

/**
 * alloc_mask obtains the flag of the header
 * memory space is either 1 or 0 (allocated/free)
 */
static const word_t alloc_mask = 0x1;

/**
 * prev_alloc_mask obtains the flag of the header
 * memory space is either 1 or 0 if previous block is (allocated/free)
 */
static const word_t prev_alloc_mask = 0x2;

/**
 * mini_block_mask obtains the flag of the header
 * memory space is either 1 or 0 if the previous block is a(mini block/not)
 */
static const word_t mini_block_mask = 0x4;

/**
 * size_mask obtains the size of the
 * memory block from the header
 */
static const word_t size_mask = ~(word_t)0xF;

/** @brief Represents the header and payload of one block in the heap */
typedef struct block {

    /** @brief Header contains size + allocation flag */
    word_t header;

    /*** @brief A pointer to the block payload and the struct of free block
     * ptr.*/
    union {
        struct {
            struct block *next_free;
            struct block *prev_free;
        };
        char payload[0];
    };
} block_t;

/* Global variables */

/** @brief Pointer to free block classes */
static block_t *root16 = NULL;
static block_t *root32 = NULL;
static block_t *root48 = NULL;
static block_t *root64 = NULL;
static block_t *root96 = NULL;
static block_t *root128 = NULL;

static block_t *root256 = NULL;
static block_t *root512 = NULL;
static block_t *root2_10 = NULL;
static block_t *root2_11 = NULL;
static block_t *root2_12 = NULL;

static block_t *root2_13 = NULL;
static block_t *root2_16 = NULL;
static block_t *root2_44 = NULL;
static block_t *root2_48 = NULL;
static block_t *root2_64 = NULL;

/*
 *****************************************************************************
 * The functions below are short wrapper functions to perform                *
 * bit manipulation, pointer arithmetic, and other helper operations.        *
 *                                                                           *
 * We've given you the function header comments for the functions below      *
 * to help you understand how this baseline code works.                      *
 *                                                                           *
 * Note that these function header comments are short since the functions    *
 * they are describing are short as well; you will need to provide           *
 * adequate details for the functions that you write yourself!               *
 *****************************************************************************
 */

/*
 * ---------------------------------------------------------------------------
 *                        BEGIN SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/**
 * @brief Returns the maximum of two integers.
 * @param[in] x
 * @param[in] y
 * @return `x` if `x > y`, and `y` otherwise.
 */
static size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
}

/**
 * @brief Rounds `size` up to next multiple of n
 * @param[in] size
 * @param[in] n
 * @return The size after rounding up
 */
static size_t round_up(size_t size, size_t n) {
    return n * ((size + (n - 1)) / n);
}

/**
 * @brief Packs the `size` and `alloc` of a block into a word suitable for
 *        use as a packed value.
 *
 * Packed values are used for both headers and footers.
 *
 * The allocation status is packed into the lowest bit of the word.
 *
 * @param[in] size The size of the block being represented
 * @param[in] alloc True if the block is allocated
 * @param[in] prev_alloc True if the previous block is allocated
 * @param[in] mini_block True if the previous block is mini
 * @return The packed value
 */
static word_t pack(size_t size, bool alloc, bool prev_alloc, bool mini_block) {
    word_t word = size;
    if (alloc) {
        word |= alloc_mask;
    }
    if (prev_alloc) {
        word |= prev_alloc_mask;
    }
    if (mini_block) {
        word |= mini_block_mask;
    }
    return word;
}

/**
 * @brief Extracts the size represented in a packed word.
 *
 * This function simply clears the lowest 4 bits of the word, as the heap
 * is 16-byte aligned.
 *
 * @param[in] word
 * @return The size of the block represented by the word
 */
static size_t extract_size(word_t word) {
    return (word & size_mask);
}

/**
 * @brief Extracts the size of a block from its header.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size(block_t *block) {
    return extract_size(block->header);
}

/**
 * @brief Given a payload pointer, returns a pointer to the corresponding
 *        block.
 * @param[in] bp A pointer to a block's payload
 * @return The corresponding block
 */
static block_t *payload_to_header(void *bp) {
    return (block_t *)((char *)bp - offsetof(block_t, payload));
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        payload.
 * @param[in] block
 * @return A pointer to the block's payload
 * @pre The block must be a valid block, not a boundary tag.
 */
static void *header_to_payload(block_t *block) {
    dbg_requires(get_size(block) != 0);
    return (void *)(block->payload);
}

/**
 * @brief Given a block pointer, returns a pointer to the corresponding
 *        footer.
 * @param[in] block
 * @return A pointer to the block's footer
 * @pre The block must be a valid block, not a boundary tag.
 */
static word_t *header_to_footer(block_t *block) {
    dbg_requires(get_size(block) != 0 &&
                 "Called header_to_footer on the epilogue block");
    return (word_t *)(block->payload + get_size(block) - dsize);
}

/**
 * @brief Given a block footer, returns a pointer to the corresponding
 *        header.
 * @param[in] footer A pointer to the block's footer
 * @return A pointer to the start of the block
 * @pre The footer must be the footer of a valid block, not a boundary tag.
 */
static block_t *footer_to_header(word_t *footer) {
    size_t size = extract_size(*footer);
    if (size == 0) {
        return (block_t *)footer;
    }
    return (block_t *)((char *)footer + wsize - size);
}

/**
 * @brief Returns the allocation status of a given header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_alloc(word_t word) {
    return (bool)(word & alloc_mask);
}

/**
 * @brief Returns the allocation status of a previous header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_prev_alloc(word_t word) {
    return (bool)(word & prev_alloc_mask);
}


/**
 * @brief Returns the mini block status of a previous header value.
 *
 * This is based on the lowest bit of the header value.
 *
 * @param[in] word
 * @return The allocation status correpsonding to the word
 */
static bool extract_mini_alloc(word_t word) {
    return (bool)(word & mini_block_mask);
}


/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc(block_t *block) {
    return extract_alloc(block->header);
}

/**
 * @brief Returns the allocation status of a previous block, based on its
 * header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_prev_alloc(block_t *block) {
    return extract_prev_alloc(block->header);
}

/**
 * @brief Returns the mini block status of a previous block, based on its
 * header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_mini_alloc(block_t *block) {
    return extract_mini_alloc(block->header);
}

/**
 * @brief Writes an epilogue header at the given address.
 *
 * The epilogue header has size 0, and is marked as allocated.
 *
 * @param[out] block The location to write the epilogue header
 */
static void write_epilogue(block_t *block, bool prev_alloc, bool mini_alloc) {
    dbg_requires(block != NULL);
    dbg_requires((char *)block == (char *)mem_heap_hi() - 7);
    block->header = pack(0, true, prev_alloc, mini_alloc);
}

/**
 * @brief Writes a block starting at the given address.
 *
 * This function writes both a header and footer, where the location of the
 * footer is computed in relation to the header.
 *
 * @param[out] block The location to begin writing the block header
 * @param[in] size The size of the new block
 * @param[in] alloc The allocation status of the new block
 */
static void write_block(block_t *block, size_t size, bool alloc,
                        bool prev_alloc, bool mini_alloc) {
    dbg_requires(block != NULL);
    dbg_requires(size > 0);
    block->header = pack(size, alloc, prev_alloc, mini_alloc);
    if (!alloc && size > 16) {
        word_t *footerp = header_to_footer(block);
        *footerp = pack(size, alloc, prev_alloc, mini_alloc);
    }
}

/**
 * @brief Finds the next consecutive block on the heap.
 *
 * This function accesses the next block in the "implicit list" of the heap
 * by adding the size of the block.
 *
 * @param[in] block A block in the heap
 * @return The next consecutive block on the heap
 * @pre The block is not the epilogue
 */
static block_t *find_next(block_t *block) {
    dbg_requires(block != NULL);
    dbg_requires(get_size(block) != 0 &&
                 "Called find_next on the last block in the heap");
    return (block_t *)((char *)block + get_size(block));
}

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_next_footer(block_t *block) {
    block_t *block_next = find_next(block);
    if (get_size(block_next) == 0)
        return (word_t *)block_next;
    word_t *footer = header_to_footer(block_next);
    return footer;
}

/**
 * @brief Finds the footer of the previous block on the heap.
 * @param[in] block A block in the heap
 * @return The location of the previous block's footer
 */
static word_t *find_prev_footer(block_t *block) {
    // Compute previous footer position as one word before the header
    return &(block->header) - 1;
}

/**
 * @brief Finds the previous consecutive block on the heap.
 *
 * This is the previous block in the "implicit list" of the heap.
 *
 * The position of the previous block is found by reading the previous
 * block's footer to determine its size, then calculating the start of the
 * previous block based on its size.
 *
 * @param[in] block A block in the heap
 * @return The previous consecutive block in the heap.
 */
static block_t *find_prev(block_t *block) {
    dbg_requires(block != NULL);
    word_t *footerp = find_prev_footer(block);

    return footer_to_header(footerp);
}

/**
 * @brief Extracts the size of a block from its footer.
 * @param[in] block
 * @return The size of the block
 */
static size_t get_size_footer(block_t *block) {
    return extract_size(*(header_to_footer(block)));
}

/**
 * @brief Returns the allocation status of a block, based on its header.
 * @param[in] block
 * @return The allocation status of the block
 */
static bool get_alloc_footer(block_t *block) {
    return extract_alloc(*(header_to_footer(block)));
}

/**
 * @brief Returns the payload size of a given block.
 *
 * The payload size is equal to the entire block size minus the sizes of the
 * block's header and footer.
 *
 * @param[in] block
 * @return The size of the block's payload
 */
static size_t get_payload_size(block_t *block) {
    size_t asize = get_size(block);
    if (!get_alloc(block))
        return asize - dsize;
    return asize - wsize;
}

/**
 * @brief Checks if a given block lies within the boundaries of the heap.
 *
 * The function compares the block pointer with the start and end of the heap
 * to determine if it falls within the allocated heap memory.
 *
 * @param[in] block A block in the heap
 * @return True if the block is within the heap boundaries, False otherwise.
 */
static bool within_heap_bounds(block_t *block) {
    block_t *heap_end = (block_t *)((char *)mem_heap_hi() - 7);
    block_t* heap_start = (block_t *)((char *)mem_heap_lo() + 8);

    return block >= heap_start && block <= heap_end;
}


/**
* @brief Finds the appropriate root of the free list based on the requested size.
* This function determines the appropriate root of the segregated free list to search
* for a free block based on the requested size. The segregated free list uses different
* size classes to group free blocks of similar sizes together. This allows for more
* efficient searching and allocation of blocks that closely match the requested size.
* 
* @param[in] size The requested size of the block to be allocated.
* @return A pointer to the root of the appropriate size class in the segregated free list,
*/
static block_t *find_class(size_t size) {
    if (size <= 16) {
        return root16;
    }
    if (size <= 32) {
        return root32;
    }
    if (size <= 48) {
        return root48;
    }
    if (size <= 64) {
        return root64;
    }
    if (size <= 96) {
        return root96;
    }
    if (size <= 128) {
        return root128;
    }
    if (size <= 256) {
        return root256;
    }
    if (size <= 512) {
        return root512;
    }
    if (size <= 1024) {
        return root2_10;
    }
    if (size <= 2048) {
        return root2_11;
    }
    if (size <= 4096) {
        return root2_12;
    }
    if (size <= 8192) {
        return root2_13;
    }
    if (size <= 65536) {
        return root2_16;
    }
    if (size <= 17592186044416) {
        return root2_44;
    }
    if (size <= 281474976710656) {
        return root2_48;
    }
    if (size <= 1844674407370955161) {
        return root2_64;
    } else
        return NULL; // No suitable root found
}
/*
* @brief Removes a block from the free list rooted at the given root.
* This function removes the specified block from the free list. If the block
* is the root block, the root is updated accordingly. Otherwise, the pointers
* of the adjacent blocks are adjusted to remove the block from the list.
*
* @param[in] block The block to be removed from the free list.
* @param[in] root The root block of the free list.
* @return The updated root block of the free list.
*/
static block_t *remove_free(block_t *block, block_t *root) {

    if (block == root) {
        if (root != NULL && root->next_free == NULL) {
            root = root->next_free;
            return root;
        }
        root = root->next_free;
        if (root->prev_free != NULL)
            root->prev_free = NULL;
        return root;
    }
    // Get the previous and next free blocks
    block_t *prev_ = block->prev_free;
    block_t *next_ = block->next_free;

    // Update the prev_free pointer of the next free block
    prev_->next_free = next_;

    // Update the next_free pointer of the previous free block
    if (next_ != NULL && within_heap_bounds(next_))
        next_->prev_free = prev_;
    return root;
}

/**

* @brief Removes a block from the free list rooted at the given root.
* This function removes the specified block from the free list. If the block
* is the root block, the root is updated accordingly. Otherwise, the pointers
* of the adjacent blocks are adjusted to remove the block from the list.
*
* @param[in] block The block to be removed from the free list.
* @param[in] root The root block of the free list.
* @return The updated root block of the free list.
*/
static block_t *remove_free_mini(block_t *block, block_t *root) {
    if (block == root) {
        if (root != NULL) {
            root = root->next_free;
            return root;
        }
        return root;
    }
    block_t *prev = NULL;
    block_t *current = root;
    while (current != NULL) {

        if (current == block) {
            prev->next_free = current->next_free;
            root16 = root;
            return root16;
        }
        prev = current;
        current = current->next_free;
    }
    return root;
}

/**
 * @brief Removes a block from the free list.
 *
 * This function updates the prev_free and next_free pointers of the adjacent
 * free blocks to remove the given block from the free list.
 *
 * @param[in] block The block to be removed from the free list.
 */
static void remove_from_free_list(block_t *block) {
    if (get_size(block) == 0)
        return;
    size_t size = get_size(block);

    if (size <= 16) {
        root16 = remove_free_mini(block, root16);
        return;
    }
    if (size <= 32) {
        root32 = remove_free(block, root32);
        return;
    }
    if (size <= 48) {
        root48 = remove_free(block, root48);
        return;
    }
    if (size <= 64) {
        root64 = remove_free(block, root64);
        return;
    }
    if (size <= 96) {
        root96 = remove_free(block, root96);
        return;
    }
    if (size <= 128) {
        root128 = remove_free(block, root128);
        return;
    }
    if (size <= 256) {
        root256 = remove_free(block, root256);
        return;
    }
    if (size <= 512) {
        root512 = remove_free(block, root512);
        return;
    }
    if (size <= 1024) {
        root2_10 = remove_free(block, root2_10);
        return;
    }
    if (size <= 2048) {
        root2_11 = remove_free(block, root2_11);
        return;
    }
    if (size <= 4096) {
        root2_12 = remove_free(block, root2_12);
        return;
    }
    if (size <= 8192) {
        root2_13 = remove_free(block, root2_13);
        return;
    }
    if (size <= 65536) {
        root2_16 = remove_free(block, root2_16);
        return;
    }
    if (size <= 17592186044416) {
        root2_44 = remove_free(block, root2_44);
        return;
    }
    if (size <= 281474976710656) {
        root2_48 = remove_free(block, root2_48);
        return;
    }
    if (size <= 1844674407370955161) {
        root2_64 = remove_free(block, root2_64);
        return;

    } else
        return;
}

/**
* @brief Adds a block to the free list rooted at the given root block.
* This function adds the provided block to the front of the free list.
* It updates the next and previous pointers accordingly.

* @param[in] block The block to be added to the free list.
* @param[in] root The root block of the free list.
* @return The updated root block of the free list.
*/

static block_t *add_free(block_t *block, block_t *root) {
    if (block == root) {
        return root;
    }
    // Get the current head of the free list
    block_t *current_head = root;

    // Link the block with the heap_start
    block->next_free = current_head;

    if (current_head != NULL && root != root16) {
        current_head->prev_free = block;
    }
    root = block;
    if (current_head != root16 && root->prev_free != NULL && root != root16)
        root->prev_free = NULL;

    return root;
}

/**
 * @brief Adds a block to the free list.
 *
 * This function links the given block with the heap_start and the first
 * next_free block after heap_start, making the given block the new head of the
 * free list.
 *
 * @param[in] block The block to be added to the free list.
 */
static void add_to_free_list(block_t *block) {

    size_t size = get_size(block);

    if (size <= 16) {
        root16 = add_free(block, root16);
        return;
    }
    if (size <= 32) {
        root32 = add_free(block, root32);
        return;
    }
    if (size <= 48) {
        root48 = add_free(block, root48);
        return;
    }
    if (size <= 64) {
        root64 = add_free(block, root64);
        return;
    }
    if (size <= 96) {
        root96 = add_free(block, root96);
        return;
    }
    if (size <= 128) {
        root128 = add_free(block, root128);
        return;
    }
    if (size <= 256) {
        root256 = add_free(block, root256);
        return;
    }
    if (size <= 512) {
        root512 = add_free(block, root512);
        return;
    }
    if (size <= 1024) {
        root2_10 = add_free(block, root2_10);
        return;
    }
    if (size <= 2048) {
        root2_11 = add_free(block, root2_11);
        return;
    }
    if (size <= 4096) {
        root2_12 = add_free(block, root2_12);
        return;
    }
    if (size <= 8192) {
        root2_13 = add_free(block, root2_13);
        return;
    }
    if (size <= 65536) {
        root2_16 = add_free(block, root2_16);
        return;
    }
    if (size <= 17592186044416) {
        root2_44 = add_free(block, root2_44);
        return;
    }
    if (size <= 281474976710656) {
        root2_48 = add_free(block, root2_48);
        return;
    }
    if (size <= 1844674407370955161) {
        root2_64 = add_free(block, root2_64);
        return;

    } else
        return;
}

/**

* @brief Checks the consistency of the free list rooted at the given block.
*
* This function verifies that the free list is consistent by checking the
following:
*
* Each block in the list is within the bounds of the heap.
* Each block is marked as unallocated (not in use).
* The next and previous pointers of each block in the list are correctly set.
* @param[in] root The root block of the free list to be checked.
*/
// Helper function to check free list consistency
static void check_free_list_consistency(block_t *root) {

    block_t *block = root;
    block_t *prev_block = NULL;

    while (block != NULL) {
        dbg_requires(within_heap_bounds(block));
        dbg_requires(!get_alloc(block));

        if (prev_block != NULL) {
            dbg_requires(prev_block->next_free == block);
            dbg_requires(block->prev_free == prev_block);
        }

        prev_block = block;
        block = block->next_free;
    }
}

/**
 * @brief First fit algorithm function.
 *
 * This function searches through the heap, starting from the beginning, for a
 * free block that is large enough to hold the requested size.
 *
 * @param[in] asize
 * @return
 * @pre The heap must be initialized.
 * @post The returned block, if not NULL, is unallocated and has
 * a size greater than or equal to the requested size.
 */

static block_t *first_fit(size_t asize, block_t *root) {

    // Start searching from the heap_start
    block_t *current_block = root;

    // Iterate through the free list until a suitable block is found
    while (current_block != NULL && within_heap_bounds(current_block)) {
        if (!(get_alloc(current_block)) && (asize <= get_size(current_block))) {
            return current_block;
        }
        current_block = current_block->next_free;
    }
    return NULL; // no fit found
}

/**
 * @brief Checks if a block of the given size can be found in the free list
 * rooted at the given root. This function checks if a block of the specified
 * size can be found in the free list rooted at the given root. It performs a
 * first-fit search in the free list to find a block that is large enough to
 * accommodate the requested size.
 *
 * @param[in] block_size The size of blocks in the free list
 * @param[in] size The size of the block to search for
 * @param[in] root The root of the free list
 * @return A pointer to a block of the requested size if found, otherwise NULL.
 */
static block_t *check_free(size_t block_size, size_t size, block_t *root) {
    if (size <= block_size && root != NULL && first_fit(size, root)) {
        return first_fit(size, root);
    }
    return NULL;
}

/**

* @brief Checks if a given block is present in the free list rooted at
* the specified root. This function traverses the free list rooted at
* the given root block and checks if the specified block is present in the list.
*
* @param[in] block The block to search for in the free list.
* @param[in] root The root block of the free list to search in.
* @return true if the block is found in the free list, false otherwise.
*/

static bool find_in_free_list(block_t *block, block_t *root) {
    block_t *curr = root;
    while (curr != NULL && within_heap_bounds(curr)) {
        if (curr == block) {
            return true;
        }
        curr = curr->next_free;
    }
    return false;
}

/**
 * @brief Checks if a given block belongs to the appropriate size class in the
 * segregated list. This function determines the size class of the block based
 * on its size and checks if the block exists in the corresponding root of the
 * size class in the segregated list.
 *
 * @param[in] block A block in the heap
 * @return true if the block is found in the appropriate size class, false
 * otherwise.
 */
static bool check_size_class(block_t *block) {
    size_t size = get_size(block);

    if (size <= 16) {
        return find_in_free_list(block, root16);
    } else if (size <= 32) {
        return find_in_free_list(block, root32);
    } else if (size <= 48) {
        return find_in_free_list(block, root48);
    } else if (size <= 64) {
        return find_in_free_list(block, root64);
    } else if (size <= 96) {
        return find_in_free_list(block, root96);
    } else if (size <= 128) {
        return find_in_free_list(block, root128);
    } else if (size <= 256) {
        return find_in_free_list(block, root256);
    } else if (size <= 512) {
        return find_in_free_list(block, root512);
    } else if (size <= 1024) {
        return find_in_free_list(block, root2_10);
    } else if (size <= 2048) {
        return find_in_free_list(block, root2_11);
    } else if (size <= 4096) {
        return find_in_free_list(block, root2_12);
    } else if (size <= 8192) {
        return find_in_free_list(block, root2_13);
    } else if (size <= 65536) {
        return find_in_free_list(block, root2_16);
    } else if (size <= 17592186044416) {
        return find_in_free_list(block, root2_44);
    } else if (size <= 281474976710656) {
        return find_in_free_list(block, root2_48);
    } else if (size <= 1844674407370955161) {
        return find_in_free_list(block, root2_64);
    } else {
        return false;
    }
}
// Check if if the previous block is a mini block
static bool check_if_prev_is_mini(block_t *block) {
    return get_mini_alloc(block);
}

/*
 * ---------------------------------------------------------------------------
 *                        END SHORT HELPER FUNCTIONS
 * ---------------------------------------------------------------------------
 */

/******** The remaining content below are helper and debug routines ********/

/**
 *@brief Coalesces a block with its neighboring blocks if they are free.

 * This function coalesces the given block with its neighboring blocks if they
 are free,
 * merging them into a single larger block. It considers four cases for
 coalescing:

 * When both the previous and next blocks are allocated.
 * When the next block is free but the previous block is allocated.
 * When the previous block is free but the next block is allocated.
 * When both the previous and next blocks are free.
 *
 * @param[in] block
 * @return
 */
static block_t *coalesce_block(block_t *block) {

    bool prev;
    bool next;
    bool mini;
    bool mini_next;
    size_t asize = get_size(block);

    block_t *block_next = find_next(block);
    size_t asize_next = get_size(block_next);

    prev = get_prev_alloc(block);
    mini = get_mini_alloc(block);
    mini_next = get_mini_alloc(block_next);
    next = get_alloc(block_next);
    //  Case 1 : when both sides are allocated

    if (next & prev) {

        write_block(block, asize, false, prev, mini);
        if (asize_next == 0)
            write_epilogue(block_next, false, mini_next);
        if (asize_next != 0)
            write_block(block_next, asize_next, next, false, mini_next);
        add_to_free_list(block);
        return block;
    }

    // Case 2: when the next block is free
    if (!next & prev) {

        // Remove the previous block from the free list
        remove_from_free_list(block_next);

        // update the next block if it is an epilogue
        if (asize_next == 0) write_epilogue(block_next, false, mini_next);

        // merge the current and next blocks
        write_block(block, asize + asize_next, false, prev, mini);

        // update the new next_block after the merge
        block_t* block_next_next = find_next(block_next);
        size_t size_next_next = get_size(block_next_next);
        if (size_next_next == 0) {
            if(asize + asize_next == 16) write_epilogue(block_next_next, get_prev_alloc(block_next_next), true);
            else write_epilogue(block_next_next, get_prev_alloc(block_next_next), false);
        }else{
            bool mini_next_next;
            if(asize + asize_next == 16) mini_next_next = true;
            else mini_next_next = false;
            write_block(block_next_next, size_next_next, get_alloc(block_next_next), get_prev_alloc(block_next_next), mini_next_next);
        }

        // update the free list
        add_to_free_list(block);
        return block;
    }

    // Case 3: when the previous block is free
    if (next & !prev) {

        block_t *block_prev;
        // get the previous block and its details
        if (check_if_prev_is_mini(block)) block_prev = (block_t *)(block->payload - wsize - dsize);
        else block_prev = find_prev(block);

        bool prev_prev = get_prev_alloc(block_prev);
        bool mini_prev = get_mini_alloc(block_prev);
        size_t asize_prev = get_size(block_prev);

        // Remove the previous block from the free list
        remove_from_free_list(block_prev);

        // merge the blocks 
        write_block(block_prev, asize + asize_prev, false, prev_prev, mini_prev);

        // update the next block
        if(asize+asize_prev != 16) mini_next = false;
        else mini_next = true;
        
        if (asize_next == 0) write_epilogue(block_next, false, mini_next);
        if (asize_next != 0) write_block(block_next, asize_next, next, false, mini_next);

        // update the free list
        add_to_free_list(block_prev);
        return block_prev;
    }
    // Case 4: when the both blocks are free
    if (!next & !prev) {

        block_t *block_prev;
        // get the previous block and its details
        if (check_if_prev_is_mini(block)) block_prev = (block_t *)(block->payload - wsize - dsize);
        else block_prev = find_prev(block);

        bool prev_prev = get_prev_alloc(block_prev); 
        size_t asize_prev = get_size(block_prev);

        bool mini_prev_prev = get_mini_alloc(block_prev);  
        // Remove the previous & next block from the free list
        remove_from_free_list(block_prev);
        remove_from_free_list(block_next);

        // update the next block
        if (asize_next == 0) write_epilogue(block_next, false, mini_next);

        // merge the blocks
        write_block(block_prev, asize + asize_prev + asize_next, false, prev_prev, mini_prev_prev);

        // update the new next_block after merge
        block_t* block_next_next = find_next(block_next);
        size_t size_next_next = get_size(block_next_next);

        bool mini_next_next;
        if(asize+asize_prev+asize_next != 16) mini_next_next = false;
        else mini_next_next = true;

        if (size_next_next == 0)  write_epilogue(block_next_next, get_prev_alloc(block_next_next), mini_next_next);
        else write_block(block_next_next, size_next_next, get_alloc(block_next_next), get_prev_alloc(block_next_next), mini_next_next);
        
        // update the free list
        add_to_free_list(block_prev);
        return block_prev;
    }
    return block;
}

/**
 * @brief Extends the heap by a specified size and initializes a new free block.

 * This function is called when there is not enough free space in the heap to
 * satisfy a memory allocation request.
 *
 * It requests additional memory from the system using the mem_sbrk function,
 * which increases the size of the heap by the specified amount.
 * The size is rounded up to maintain alignment requirements.
 * If the system call to mem_sbrk fails, indicating that there is no more memory
 available,
 * the function returns NULL.
 *
 * @param[in] size
 * @return
 */
static block_t *extend_heap(size_t size) {
    void *bp;

    block_t *epilogue = (block_t *)((char *)mem_heap_hi() - 7);
    bool last = extract_prev_alloc(epilogue->header);
    bool last_mini = extract_mini_alloc(epilogue->header);

    // Allocate an even number of words to maintain alignment
    size = round_up(size, dsize);
    if ((bp = mem_sbrk((intptr_t)size)) == (void *)-1) {
        return NULL;
    }

    // Initialize free block header/footer
    block_t *block = payload_to_header(bp);
    if (last) write_block(block, size, false, true, last_mini);
    if (!last) write_block(block, size, false, false, last_mini);

    // Create new epilogue header
    block_t *block_next = find_next(block);
    if (size == 16) write_epilogue(block_next, false, true);
    else write_epilogue(block_next, false, false);

    // Coalesce in case the previous block was free
    block = coalesce_block(block);
    return block;
}

/**
 * @brief Splits a block into two separate blocks if the remaining space after
 * allocating the requested size is large enough.

 * This function is called after allocating a block of memory to check if there
 is enough space
 * remaining in the block to split it into two separate blocks.
 * The split is performed by updating the size and allocation status of the
 current block
 * and creating a new block with the remaining space.

 * @param[in] block A pointer to the block to be split
 * @param[in] asize The adjusted size of the requested memory block (including
 overhead)
 * @pre The block must be allocated (get_alloc(block) == true)
 * @pre The size of the remaining space after allocation must be (asize >= 2 *
 min_block_size)
 * @post The original block is marked as allocated and its size is updated to
 asize
 * @post A new block is created with the remaining space, marked as free, and
 its size is updated
*/
static void split_block(block_t *block, size_t asize) {
    dbg_requires(get_alloc(block));
    dbg_requires(asize >= min_block_size);

    size_t block_size = get_size(block);
    bool mini = get_mini_alloc(block);

    // if the new next block is large enough, split them
    if ((block_size - asize) >= min_block_size) {
        block_t *block_next;
        write_block(block, asize, true, true, mini);

        block_next = find_next(block);
        if (asize == 16) write_block(block_next, block_size - asize, false, true, true);
        else write_block(block_next, block_size - asize, false, true, false);
        add_to_free_list(block_next);
    }

    dbg_ensures(get_alloc(block));
}

/**
 * @brief First fit algorithm function.
 *
 * This function searches through the heap, starting from the beginning, for a
 * free block that is large enough to hold the requested size.
 *
 * @param[in] asize
 * @return
 * @pre The heap must be initialized.
 * @post The returned block, if not NULL, is unallocated and has
 * a size greater than or equal to the requested size.
 */

static block_t *find_fit(size_t asize) {

    if (check_free(16, asize, root16) != NULL)
        return check_free(16, asize, root16);
    if (check_free(32, asize, root32) != NULL)
        return check_free(32, asize, root32);
    if (check_free(48, asize, root48) != NULL)
        return check_free(48, asize, root48);
    if (check_free(64, asize, root64) != NULL)
        return check_free(64, asize, root64);
    if (check_free(96, asize, root96) != NULL)
        return check_free(96, asize, root96);
    if (check_free(128, asize, root128) != NULL)
        return check_free(128, asize, root128);

    if (check_free(256, asize, root256) != NULL)
        return check_free(256, asize, root256);
    if (check_free(512, asize, root512) != NULL)
        return check_free(512, asize, root512);
    if (check_free(1024, asize, root2_10) != NULL)
        return check_free(1024, asize, root2_10);
    if (check_free(2048, asize, root2_11) != NULL)
        return check_free(2048, asize, root2_11);
    if (check_free(4096, asize, root2_12) != NULL)
        return check_free(4096, asize, root2_12);

    if (check_free(8192, asize, root2_13) != NULL)
        return check_free(8192, asize, root2_13);
    if (check_free(65536, asize, root2_16) != NULL)
        return check_free(65536, asize, root2_16);

    if (check_free(17592186044416, asize, root2_44) != NULL)
        return check_free(17592186044416, asize, root2_44);
    if (check_free(281474976710656, asize, root2_48) != NULL)
        return check_free(281474976710656, asize, root2_48);
    if (check_free(1844674407370955161, asize, root2_64) != NULL)
        return check_free(1844674407370955161, asize, root2_64);
    return NULL; // no fit found
}

/**
 * @brief Checks the consistency and validity of the heap.
 *
 * This function performs various checks on the heap to ensure its consistency
 * and validity. It checks the prologue and epilogue blocks, verifies that all
 * blocks lie within the heap boundaries, checks block alignment, size, header
 * and footer consistency, and checks for consecutive free blocks.
 *
 * @param[in] line
 * @return
 */
bool mm_checkheap(int line) {
    //  Check prologue block
    block_t *prologue = (block_t *)mem_heap_lo();
    if (get_size(prologue) != 0 || !get_alloc(prologue)) {
        return false;
    }

    // Check epilogue block
    block_t *epilogue = (block_t *)((char *)mem_heap_hi() - 7);
    if (get_size(epilogue) != 0 || !get_alloc(epilogue)) {
        return false;
    }

    // Checking roots
    check_free_list_consistency(root32);
    check_free_list_consistency(root48);
    check_free_list_consistency(root64);
    check_free_list_consistency(root96);
    check_free_list_consistency(root128);
    check_free_list_consistency(root256);
    check_free_list_consistency(root512);
    check_free_list_consistency(root2_10);
    check_free_list_consistency(root2_11);
    check_free_list_consistency(root2_12);
    check_free_list_consistency(root2_13);
    check_free_list_consistency(root2_16);
    check_free_list_consistency(root2_44);
    check_free_list_consistency(root2_48);
    check_free_list_consistency(root2_64);

    block_t *block;
    block_t* heap_start = (block_t *)((char *)mem_heap_lo() + 8);
    // Iterate through the heap to check blocks
    for (block = heap_start; get_size(block) > 0; block = find_next(block)) {

        // Check if blocks lie within heap boundaries
        if (!within_heap_bounds(block)) {
            dbg_printf("Error [line %d]: Block %p does not lie within bounds\n", line, (void *)block);
            return false;
        }
        // Check block alignment
        if (((size_t)block - wsize) % dsize != 0) {
            dbg_printf("Error [line %d]: Block %p is not double-word aligned\n",line, (void *)block);
            return false;
        }

        // Check epilogue block
        if (get_size((block_t *)((char *)mem_heap_hi() - 7)) != 0 ||
            !get_alloc((block_t *)((char *)mem_heap_hi() - 7))) {
            dbg_printf("Error [line %d]: Block %p has extra 0 size block besides epilogue\n",line, (void *)block);
            return false;
        }

        // Check block size
        if (get_size(block) % dsize != 0) {
            dbg_printf("Error [line %d]: Block %p has an unaligned size\n",line, (void *)block);
            return false;
        }

        // Check block header and footer consistency in normal free blocks
        if ((!get_alloc(block) && get_size(block) != 16 &&
             block->header != *header_to_footer(block))) {
            dbg_printf("Error [line %d]: Block %p has inconsistent header and ""footer\n",line, (void *)block);
            return false;
        }

        // Check if the prev_mini bit is 1 but the previous block is not 16 bytes
        if ((get_mini_alloc(block) && get_size((block_t *)(block->payload - wsize - dsize)) != 16)) {
            dbg_printf("Error [line %d]: Block %p has inconsistent MINI block\n",line, (void *)block);
            return false;
        }

        // Check if the block is 16 bytes but the prev_mini bit of the next block is 0
        if ((get_size(block) == 16) && !get_mini_alloc(find_next(block))) {
            dbg_printf("Error [line %d]: Block %p has SIZE 16 but the next MINI is 0 block\n",line, (void *)block);
            return false;
        }

        // Check block header and footer size/alloc, min size in normal free
        // blocks
        if (!get_alloc(block) && get_size(block) != 16 &&
            (get_size(block) < min_block_size ||
             (get_size(block) != get_size_footer(block)) ||
             (get_alloc(block) != get_alloc_footer(block)))) {
            dbg_printf("Error [line %d]: Block %p has inconsistent header and " "footer sze/alloc\n",line, (void *)block);
            return false;
        }

        // Check coalescing: no consecutive free blocks in the heap.
        if (!get_alloc(block) && !get_alloc(find_next(block))) {
            dbg_printf("Error [line %d]: Contiguous free blocks found: %p and %p\n",line, (void *)block, (void *)find_next(block));
            return false;
        }
        // Check if the free block is in the correct size class root
        if (!get_alloc(block) && !check_size_class(block)) {
            dbg_printf("Error [line %d]: Block %p is not in the correct size ""class root\n",line, (void *)block);
            return false;
        }
    }

    dbg_printf("I did not write a heap checker (called at line %d)\n", line);
    return true;
}

/**
 * @brief Initializes the memory allocator by creating the initial empty heap.
 * It requests memory from the system using the mem_sbrk function and sets up
 * the heap prologue and epilogue.
 * @return
 */
bool mm_init(void) {
    //  Create the initial empty heap
    word_t *start = (word_t *)(mem_sbrk(2 * wsize));

    if (start == (void *)-1) {
        return false;
    }

    start[0] = pack(0, true, true, false); // Heap prologue (block footer)
    start[1] = pack(0, true, true, false); // Heap epilogue (block header)

    // Initialize the class roots
    if (root16 != NULL)
        root16 = NULL;
    if (root32 != NULL)
        root32 = NULL;
    if (root48 != NULL)
        root48 = NULL;
    if (root64 != NULL)
        root64 = NULL;
    if (root96 != NULL)
        root96 = NULL;
    if (root128 != NULL)
        root128 = NULL;

    if (root256 != NULL)
        root256 = NULL;
    if (root512 != NULL)
        root512 = NULL;
    if (root2_10 != NULL)
        root2_10 = NULL;
    if (root2_11 != NULL)
        root2_11 = NULL;
    if (root2_12 != NULL)
        root2_12 = NULL;

    if (root2_13 != NULL)
        root2_13 = NULL;
    if (root2_16 != NULL)
        root2_16 = NULL;
    if (root2_44 != NULL)
        root2_44 = NULL;
    if (root2_48 != NULL)
        root2_48 = NULL;
    if (root2_64 != NULL)
        root2_64 = NULL;

    // Extend the empty heap with a free block of chunksize bytes
    if (extend_heap(chunksize) == NULL) {
        return false;
    }

    return true;
}

/**
 * @brief Allocates a block of memory of the specified size.
 *
 * It adjusts the block size to include overhead and
 * meet alignment requirements. If a suitable free block is found in the free
 * list, it is used to satisfy the allocation request. Otherwise, it requests
 * more memory from the system using extend_heap and allocates a new block.
 * The allocated block is marked as allocated and returned as a void pointer.
 *
 * @param[in] size
 * @return
 */
void *malloc(size_t size) {
    dbg_requires(mm_checkheap(__LINE__));

    size_t asize;      // Adjusted block size
    size_t extendsize; // Amount to extend heap if no fit is found
    block_t *block;
    void *bp = NULL;

    block_t* heap_start = (block_t *)((char *)mem_heap_lo() + 8);
    //  Initialize heap if it isn't initialized
    if (heap_start == NULL) {
        if (!(mm_init())) {
            dbg_printf("Problem initializing heap. Likely due to sbrk");
            return NULL;
        }
    }
    // Ignore spurious request
    if (size == 0) {
        dbg_ensures(mm_checkheap(__LINE__));
        return bp;
    }

    // Adjust block size to include overhead and to meet alignment requirements
    asize = round_up(size + wsize, dsize);

    // Search the free list for a fit
    block = find_fit(asize);

    //  If no fit is found, request more memory, and then and place the block
    if (block == NULL) {
        // Always request at least chunksize
        extendsize = max(asize, chunksize);

        block = extend_heap(extendsize);
        // extend_heap returns an error
        if (block == NULL) {
            return bp;
        }
    }

    // The block should be marked as free
    dbg_assert(!get_alloc(block));

    // Mark block as allocated
    size_t block_size = get_size(block);
    bool mini = get_mini_alloc(block);
    write_block(block, block_size, true, true, mini);
    // remove the allocated block from the free list
    remove_from_free_list(block);

    // Try to split the block if too large
    split_block(block, asize);

    // update the details of the new next block after the split
    bool mini_next;
    if (asize == 16) mini_next = true; 
    else mini_next = false;

    block_t *block_next = find_next(block);
    if (get_size(block_next) == 0)
        write_epilogue(block_next, true, mini_next);
    else {
        block_t *next_next = find_next(block_next);
        bool mini_next_next;
        size_t asize_next = get_size(block_next);
        if (asize_next == 16) mini_next_next = true;
        else mini_next_next = false;
        write_block(block_next, get_size(block_next), get_alloc(block_next), true, mini_next);
        if (get_size(next_next) == 0) write_epilogue(next_next, get_alloc(block_next), mini_next_next);
        else write_block(next_next, get_size(next_next), get_alloc(next_next), get_alloc(block_next), mini_next_next);
    }

    bp = header_to_payload(block);

    dbg_ensures(mm_checkheap(__LINE__));
    return bp;
}

/**
 * @brief Frees a previously allocated block of memory.
 *
 * This function marks a block of memory as free by updating its header
 * and footer to reflect its new status. It then attempts to coalesce the
 * block with its neighboring free blocks to form larger free blocks.
 *
 * @param[in] bp
 */
void free(void *bp) {
    dbg_requires(mm_checkheap(__LINE__));

    if (bp == NULL) {
        return;
    }

    block_t *block = payload_to_header(bp);
    size_t size = get_size(block);

    // The block should be marked as allocated
    dbg_assert(get_alloc(block));

    // Mark the block as free
    bool mini = get_mini_alloc(block);
    write_block(block, size, false, get_prev_alloc(block), mini);

    // Try to coalesce the block with its neighbors
    coalesce_block(block);

    dbg_ensures(mm_checkheap(__LINE__));
}

/**
 * @brief Reallocates a block of memory with a new size.
 *
 * This function reallocates a previously allocated block of memory pointed to
 * by ptr to a new size specified by size. The contents of the old block are
 * copied to the new block up to the minimum of the old size and the new size.
 * The old block is then freed.
 *
 * @param[in] ptr
 * @param[in] size
 * @return
 */
void *realloc(void *ptr, size_t size) {
    block_t *block = payload_to_header(ptr);
    size_t copysize;
    void *newptr;

    // If size == 0, then free block and return NULL
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // If ptr is NULL, then equivalent to malloc
    if (ptr == NULL) {
        return malloc(size);
    }

    // Otherwise, proceed with reallocation
    newptr = malloc(size);

    // If malloc fails, the original block is left untouched
    if (newptr == NULL) {
        return NULL;
    }

    // Copy the old data
    copysize = get_payload_size(block); // gets size of old payload
    if (size < copysize) {
        copysize = size;
    }
    memcpy(newptr, ptr, copysize);

    // Free the old block
    free(ptr);

    return newptr;
}

/**
 * @brief Allocates memory for an array of elements and initializes them to 0.
 *
 * This function allocates memory for an array of elements, where each element
 * has a size of 'size' bytes. It initializes all the bits of the allocated
 * memory to 0. The total size of the allocated memory is determined by the
 * product of 'elements' and 'size'.
 *
 * @param[in] elements
 * @param[in] size
 * @return
 */
void *calloc(size_t elements, size_t size) {
    void *bp;
    size_t asize = elements * size;

    if (elements == 0) {
        return NULL;
    }
    if (asize / elements != size) {
        // Multiplication overflowed
        return NULL;
    }

    bp = malloc(asize);
    if (bp == NULL) {
        return NULL;
    }

    // Initialize all bits to 0
    memset(bp, 0, asize);

    return bp;
}

/*
 *****************************************************************************
 * Do not delete the following super-secret(tm) lines!                       *
 *                                                                           *
 * 53 6f 20 79 6f 75 27 72 65 20 74 72 79 69 6e 67 20 74 6f 20               *
 *                                                                           *
 * 66 69 67 75 72 65 20 6f 75 74 20 77 68 61 74 20 74 68 65 20               *
 * 68 65 78 61 64 65 63 69 6d 61 6c 20 64 69 67 69 74 73 20 64               *
 * 6f 2e 2e 2e 20 68 61 68 61 68 61 21 20 41 53 43 49 49 20 69               *
 *                                                                           *
 * 73 6e 27 74 20 74 68 65 20 72 69 67 68 74 20 65 6e 63 6f 64               *
 * 69 6e 67 21 20 4e 69 63 65 20 74 72 79 2c 20 74 68 6f 75 67               *
 * 68 21 20 2d 44 72 2e 20 45 76 69 6c 0a c5 7c fc 80 6e 57 0a               *
 *                                                                           *
 *****************************************************************************
 */    
