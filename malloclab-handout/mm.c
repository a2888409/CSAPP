/*
 *本程序使用了分离适配算法，维护18个空闲链表，其头指针存放在heap开始处，后面 *同样紧跟着对齐块，序言头，足，结尾块。
 *以下是每个空闲块的结构：注意next指针在低地址，而prev在高地址。
 *      ——————————————  低位
 *      |    head     |
 *      —————————————— 
 *      |    next     |
 *      —————————————— 
 *      |    prev     |
 *      ——————————————
 *      |   有效载荷  |        
 *      ——————————————
 *      |     填充    |
 *      ——————————————
 *      |    foot     |
 *      ——————————————  高位
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* Basic constants and macros */
#define WSIZE 4 	/* Word and header/footer size (bytes) */
#define DSIZE 8 	/* Double word size (bytes) */
#define CHUNKSIZE (1<<12) 	/* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into  word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(unsigned int *)(p))
#define PUT(p, val)      (*(unsigned int *)(p) = (val))

/* Read and write a pointer at address p */
#define GET_PTR(p) ((unsigned int *)(long)(GET(p)))
#define PUT_PTR(p, ptr) (*(unsigned int *)(p) = ((long)ptr))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))


/* 18个空闲链表的大小 */
#define SIZE1 (1<<4)
#define SIZE2 (1<<5)
#define SIZE3 (1<<6)
#define SIZE4 (1<<7)
#define SIZE5 (1<<8)
#define SIZE6 (1<<9)
#define SIZE7 (1<<10)  		
#define SIZE8 (1<<11)
#define SIZE9 (1<<12)
#define SIZE10 (1<<13)
#define SIZE11 (1<<14)
#define SIZE12 (1<<15)
#define SIZE13 (1<<16)
#define SIZE14 (1<<17)
#define SIZE15 (1<<18)
#define SIZE16 (1<<19)
#define SIZE17 (1<<20) 		

#define LISTS_NUM 18 		

/* Globe var */
static char *heap_listp;

/* 函数声明 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_list(void *bp);
int getListOffset(size_t size);
void delete_list(void *bp);

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
	/* Team name */
	"qzy",
	/* First member's full name */
	"qzy",
	/* First member's email address */
	"qzy@XXX.com",
	/* Second member's full name (leave blank if none) */
	"",
	/* Second member's email address (leave blank if none) */
	""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*新建一个heap，包括18个空闲链表，和4个初始化块*/
    char *bp;
	int i;

	if ((heap_listp = mem_sbrk((LISTS_NUM + 4) * WSIZE)) == (void *)-1) {
		return -1;
	}
	PUT(heap_listp + LISTS_NUM * WSIZE, 0);//18个头指针后的填充块
	PUT(heap_listp + (1 + LISTS_NUM) * WSIZE, PACK(DSIZE, 1));
	PUT(heap_listp + (2 + LISTS_NUM) * WSIZE, PACK(DSIZE, 1));
	PUT(heap_listp + (3 + LISTS_NUM) * WSIZE, PACK(0, 1));
    /*将18个链表头初始化都指向NULL*/
	for (i = 0; i < LISTS_NUM; i++) {
		PUT_PTR(heap_listp + WSIZE * i, NULL);
	}

	/* Extend the empty heap with a free block of CHUNKSIZE bytes */
	if ((bp = extend_heap(CHUNKSIZE / WSIZE)) == NULL) {
		return -1;
	}

	return 0;
}

/*
 * 扩展heap大小
 */
void *extend_heap(size_t words)
{
	char *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment */
	size = (words % 2) ? ((words + 1) * WSIZE) : (words * WSIZE);
	if ((long)(bp = mem_sbrk(size)) == -1) {
		return NULL;
	}

	/* Initialize free block header/footer and the epilogue header */
	PUT(HDRP(bp), PACK(size, 0)); 		/* Free block header */
	PUT(FTRP(bp), PACK(size, 0)); 		/* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 	/* New epilogue header */

	/* Coalesce if the previous block was free */
	return coalesce(bp);
}


/*
 * 合并空闲块。合并的情况要先删除空闲块再重新插入。
 */
void *coalesce(void *bp)
{
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));
	
	if (prev_alloc && next_alloc) { 	/* 前后无空闲  */
		bp = bp;
	} else if (prev_alloc && !next_alloc) { 	/* 后空闲 */
		delete_list(NEXT_BLKP(bp));
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	} else if (!prev_alloc && next_alloc) { 	/* 前空闲 */
		delete_list(PREV_BLKP(bp));
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	} else { 				/* 前后均空闲 */
		delete_list(NEXT_BLKP(bp));
		delete_list(PREV_BLKP(bp));
		size = size + GET_SIZE(HDRP(PREV_BLKP(bp))) + 
			GET_SIZE(HDRP(NEXT_BLKP(bp)));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	insert_list(bp);
	return bp;
}


/*
 * 将一个空闲块插入到合适位置，插入位置为每个链表表头
 */
void insert_list(void *bp)
{
	int index;
	size_t size;
	size = GET_SIZE(HDRP(bp));
	index = getListOffset(size);
    /*当前链表表头指向NULL*/
	if (GET_PTR(heap_listp + WSIZE * index) == NULL) {
		PUT_PTR(heap_listp + WSIZE * index, bp);
		PUT_PTR(bp, NULL);
		PUT_PTR((unsigned int *)bp + 1, NULL);
	} else {    /*当前链表已经有元素，插入到该链表表头，新的第一个元素与原来第一个元素连接*/
		PUT_PTR(bp, GET_PTR(heap_listp + WSIZE * index));
		PUT_PTR(GET_PTR(heap_listp + WSIZE * index) + 1, bp);  	
        PUT_PTR((unsigned int *)bp + 1, NULL);
		PUT_PTR(heap_listp + WSIZE * index, bp);
	}
}

/* 
 * 删除链表结点，就是双向链表的删除操作。一共四种情况。
 */
void delete_list(void *bp)
{
	int index;
	size_t size;
	size = GET_SIZE(HDRP(bp));
	index = getListOffset(size);
	if (GET_PTR(bp) == NULL && GET_PTR((unsigned int *)bp + 1) == NULL) { 
		/* 后继next为NULL，前驱prev也为NULL,表明这个链表仅含一个结点。
        然后我们将合适大小对应的头指针设置为NULL*/
		PUT_PTR(heap_listp + WSIZE * index, NULL);
	} else if (GET_PTR(bp) == NULL && GET_PTR((unsigned int *)bp + 1) != NULL) {
		/*当前链表有多个结点，是最后一个。
        通过prev指针得到前一个块，再减去(unsigned int)1，就得到了指向next的指针，
        再将next指向NULL*/
        PUT_PTR( (GET_PTR( (unsigned int*)GET_PTR((unsigned int *)bp + 1) - 1 )), NULL );
		PUT_PTR(GET_PTR((unsigned int *)bp + 1), NULL);  //bp前驱指针prev=NULL
	} else if (GET_PTR(bp) != NULL && GET_PTR((unsigned int *)bp + 1) == NULL){
		/*当前链表有多个结点，是第一个
        第一条语句将相应大小的头指针指向了bp的next*/
		PUT_PTR(heap_listp + WSIZE * index, GET_PTR(bp));
		PUT_PTR(GET_PTR(bp) + 1, NULL); //prev=NULL
	} else if (GET_PTR(bp) != NULL && GET_PTR((unsigned int *)bp + 1) != NULL) {
		/*当前链表有多个节点，为中间结点
        第一条前一个块的next指向了当前块的next*/
		PUT_PTR(GET_PTR((unsigned int *)bp + 1), GET_PTR(bp));
		PUT_PTR(GET_PTR(bp) + 1, GET_PTR((unsigned int*)bp + 1));//bp->prev = bp->next
	}
}


/*
 * 大小为size的块取哪个空闲链表中的块最合适 */
int getListOffset(size_t size)
{
	if (size <= SIZE1) {
		return 0;
	} else if (size <= SIZE2) {
		return 1;
	} else if (size <= SIZE3) {
		return 2;
	} else if (size <= SIZE4) {
		return 3;
	} else if (size <= SIZE5) {
		return 4;
	} else if (size <= SIZE6) {
		return 5;
	} else if (size <= SIZE7) {
		return 6;
	} else if (size <= SIZE8) {
		return 7;
	} else if (size <= SIZE9) {
		return 8;
	} else if (size <= SIZE10) {
		return 9;
	} else if (size <= SIZE11) {
		return 10;
	} else if (size <= SIZE12) {
		return 11;
	} else if (size <= SIZE13) {
		return 12;
	} else if (size <= SIZE14) {
		return 13;
	} else if (size <= SIZE15) {
		return 14;
	} else if (size <= SIZE16) {
		return 15;
	} else if (size <= SIZE17) {
		return 16;
	} else {
		return 17;
	}
}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	size_t asize; 		/* Adjusted block size */
	size_t extendsize; 	/* Amount to extend heap if no fit */
	char *bp;

	/* Igore spurious requests */
	if (0 == size) {
		return NULL;
	}

	/* Adjusted block size to include overhead and alignment reqs */
	if (size <= DSIZE) {
		asize = 2 * DSIZE;
	} else {
		asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
	}

	/* Search the free list for a fit */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	/* No fit found. Get more memory and place the block */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
		return NULL;
	}
	place(bp, asize);
	return bp;
 }

/*
 * 根据大小先确定哪一个空闲链表所含空闲块最合适。
 */
void *find_fit(size_t asize)
{
	int index;
	index = getListOffset(asize);
	unsigned int *ptr;

	/* 最合适的空闲链表没有满足要求到空闲块就到下一个空闲链表寻找*/
	while (index < 18) {
		ptr = GET_PTR(heap_listp + 4 * index);
		while (ptr != NULL) {
			if (GET_SIZE(HDRP(ptr)) >= asize) {
				return (void *)ptr;
			}
			ptr = GET_PTR(ptr);
		}
		index++;
	}

	return NULL;
}

/*
 * 确定新分配的空闲块是否需要分割。
 */
void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));
	delete_list(bp);
	if ((csize - asize) >= (2 * DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 1));	
		PUT(FTRP(bp), PACK(asize, 1));
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));
		insert_list(bp);
	} else {
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));
	}
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{	
    size_t size = GET_SIZE(HDRP(ptr));

	PUT(HDRP(ptr), PACK(size, 0));
	PUT(FTRP(ptr), PACK(size, 0));
	coalesce(ptr);
}


/*
 * mm_realloc - 直接用malloc和free组合实现
 */
void *mm_realloc(void *ptr, size_t size)
{
	size_t asize;
	void *oldptr = ptr;
	void *newptr;

	/* free */
	if (0 == size) {
		free(oldptr);
		return NULL;
	}


	if (size <= DSIZE) {
		asize = 2 * DSIZE;
	} else {
		asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
	}

	if (asize == GET_SIZE(HDRP(oldptr))) {
		return oldptr;
	}
	
	/* 缩小空间 */
	if (asize < GET_SIZE(HDRP(oldptr))) {
		newptr = mm_malloc(size);
		memmove(newptr, oldptr, size);
		mm_free(oldptr);

		return newptr;
	}

	/* 从heap的其他地方寻找 */
	newptr = mm_malloc(size);
	if (NULL == newptr)
		return NULL;
	memmove(newptr, oldptr, size);
	mm_free(oldptr);

	return newptr;
}


