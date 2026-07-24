#include "memory.h"

#include "errno.h"
#include "user.h"

// Header/footer size (bytes)
#define WSIZE (4)
#define DSIZE (2 * WSIZE)
// Extend heap by this amount (bytes) (4096 bytes)
#define CHUNKSIZE (1 << 12)

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(uint32_t *)(p))
#define PUT(p, val) (*(uint32_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp (points to first payload byte), compute address of its
 * header and footer */
#define HDRP(bp) ((uint8_t *)(bp) - WSIZE)
#define FTRP(bp) ((uint8_t *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((uint8_t *)(bp) + GET_SIZE(((uint8_t *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((uint8_t *)(bp) - GET_SIZE(((uint8_t *)(bp) - DSIZE)))

static void *heap_listp = NULL;

static void *coalesce(void *bp) {
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  if (prev_alloc && next_alloc) {
    return bp;
  } else if (prev_alloc && !next_alloc) {
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  } else if (!prev_alloc && next_alloc) {
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  } else {
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    bp = PREV_BLKP(bp);
  }
  return bp;
}

static void *extend_heap(size_t words) {
  // Allocate an even number of words to maintain alignment
  size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
  uint8_t *bp = sbrk(size);
  if (bp == (void *)-1) {
    return NULL;
  }

  // Initialize free block header/footer and the epilogue header
  PUT(HDRP(bp), PACK(size, 0));         // Free block header
  PUT(FTRP(bp), PACK(size, 0));         // Free block footer
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header

  return coalesce(bp);
}

static int mm_init(void) {
  heap_listp = sbrk(4 * WSIZE);
  if (heap_listp == (void *)-1) {
    return ERR_NO_MEMORY;
  }
  PUT(heap_listp, 0);                            // Alignment padding
  PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // Prologue header
  PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // Prologue footer
  PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // Epilogue header
  heap_listp += (2 * WSIZE);

  // Extend the empty heap with a free block of CHUNKSIZE bytes
  if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
    return ERR_NO_MEMORY;
  }
  return 0;
}

// Perform a first-fit search to return a free block.
static void *find_fit(size_t asize) {
  void *bp;

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      return bp;
    }
  }
  return NULL;
}

static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));

  if ((csize - asize) >= (2 * DSIZE)) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
  } else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

void *malloc(size_t size) {
  if (heap_listp == NULL) {
    if (mm_init() < 0) {
      printf("failed to initialize heap\n");
      return NULL;
    }
  }

  if (size == 0) {
    return NULL;
  }

  // Ensure the size is a multiple of DSIZE
  size_t asize;
  if (size <= DSIZE) {
    asize = 2 * DSIZE;
  } else {
    asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
  }

  uint8_t *bp = NULL;
  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }

  // No fit found: Get more memory and place the block
  size_t extendsize = MAX(asize, CHUNKSIZE);
  if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
    return NULL;
  }
  place(bp, asize);
  return bp;
}

void free(void *bp) {
  if (bp == NULL) {
    return;
  }

  if (heap_listp == NULL) {
    printf("free called on uninitialized heap\n");
    return;
  }

  size_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  coalesce(bp);
}

void *calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *p = malloc(total);
  if (p) {
    memset(p, 0, total);
  }
  return p;
}

void *realloc(void *ptr, size_t size) {
  if (ptr == NULL) {
    return malloc(size);
  }
  if (size == 0) {
    free(ptr);
    return NULL;
  }

  void *new_ptr = malloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }

  size_t old_size = GET_SIZE(HDRP(ptr)) - DSIZE;
  size_t copy_size = old_size < size ? old_size : size;
  memcpy(new_ptr, ptr, copy_size);
  free(ptr);
  return new_ptr;
}
