#include <TestSupport.h>
#include <MemoryKit/palloc.h>
#include <StaticString.h>
#include <boost/static_assert.hpp>
#include <boost/cstdint.hpp>

using namespace Passenger;
using namespace std;

namespace tut {
	struct MemoryKit_PallocTest {
		psg_pool_t *pool;

		MemoryKit_PallocTest()
			: pool(NULL)
			{ }

		~MemoryKit_PallocTest() {
			if (pool != NULL) {
				psg_destroy_pool(pool);
			}
		}
	};

	DEFINE_TEST_GROUP(MemoryKit_PallocTest);

	#define TEST_BASIC_ALLOCATIONS() \
		do { \
			volatile char *buf = (char *) psg_pnalloc(pool, 8); \
			buf[0] = '1'; \
			buf[1] = '2'; \
			buf[2] = '3'; \
			buf[3] = '4'; \
			buf[4] = '5'; \
			buf[5] = '6'; \
			buf[6] = '7'; \
			buf[7] = '\0'; \
			ensure_equals("psg_pnalloc works", \
				StaticString((const char *) buf), \
				P_STATIC_STRING("1234567")); \
			\
			BOOST_STATIC_ASSERT(sizeof(void *) <= sizeof(boost::uintmax_t)); \
			\
			volatile int *i = (int *) psg_palloc(pool, sizeof(int)); \
			ensure_equals<boost::uintmax_t>( \
				"psg_palloc's alignment is suitable for integers", \
				(boost::uintmax_t) i % sizeof(int), \
				0); \
			*i = 1024; \
			ensure_equals("psg_palloc on integers works", \
				*i, 1024); \
			\
			volatile double *d = (double *) psg_palloc(pool, sizeof(double)); \
			ensure_equals<boost::uintmax_t>( \
				"psg_palloc's alignment is suitable for doubles", \
				(boost::uintmax_t) i % sizeof(double), \
				0); \
			*d = 1234.5; \
			ensure_equals("psg_palloc on doubles works", \
				*d, 1234.5); \
		} while (false)

	#define TEST_LARGE_ALLOCATION() \
		do { \
			size_t size = PSG_MAX_ALLOC_FROM_POOL + 32; \
			largebuf = (char *) psg_pnalloc(pool, size); \
			for (unsigned i = 0; i < size; i++) { \
				largebuf[i] = (char) i; \
			} \
			for (unsigned i = 0; i < size; i++) { \
				ensure_equals("Testing buffer content", largebuf[i], (char) i); \
			} \
		} while (false)

	TEST_METHOD(1) {
		set_test_name("Initial state");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(2) {
		set_test_name("Basic allocations that fit within one pool data struct");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		TEST_BASIC_ALLOCATIONS();

		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(3) {
		set_test_name("Basic allocations that require multiple pool data structs");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		size_t allocated = 0;
		while (allocated < PSG_DEFAULT_POOL_SIZE) {
			psg_palloc(pool, sizeof(double));
			allocated += sizeof(double);
		}

		TEST_BASIC_ALLOCATIONS();

		ensure("At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("Exactly two pool data struct are allocated",
			pool->data.next->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(4) {
		set_test_name("It allocates objects larger than"
			" PSG_MAX_ALLOC_FROM_POOL using malloc()");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		volatile char *largebuf;
		TEST_LARGE_ALLOCATION();

		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure("The buffer is allocated through the large list (1)",
			pool->large != NULL);
		ensure_equals<void *>("The buffer is allocated through the large list (2)",
			pool->large->alloc, (void *) largebuf);
		ensure_equals<void *>("There is only one item in the large list",
			pool->large->next, NULL);
	}

	TEST_METHOD(5) {
		set_test_name("It allows freeing"
			" objects larger than PSG_MAX_ALLOC_FROM_POOL");

		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		volatile char *largebuf;

		volatile char *largebuf1;
		TEST_LARGE_ALLOCATION();
		largebuf1 = largebuf;

		volatile char *largebuf2;
		TEST_LARGE_ALLOCATION();
		largebuf2 = largebuf;

		volatile char *largebuf3;
		TEST_LARGE_ALLOCATION();
		largebuf3 = largebuf;

		ensure("Object 2 was freed", psg_pfree(pool, (void *) largebuf2));
		ensure("Object 1 was freed", psg_pfree(pool, (void *) largebuf1));
		ensure("Object 3 was freed", psg_pfree(pool, (void *) largebuf3));

		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(6) {
		set_test_name("It bumps the 'current' pointer to the next data struct"
			" after upon allocating the 8th data struct");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		while (pool->current == pool) {
			psg_pnalloc(pool, 32);
		}

		psg_pool_t *segment = pool;
		ensure_equals("(1) data.failed is 6", segment->data.failed, 6u);

		segment = segment->data.next;
		ensure("(2) data struct is not NULL", segment != NULL);
		ensure_equals("(2) data.failed is 5", segment->data.failed, 5u);
		ensure_equals("pool->current points to segment 2",
			pool->current, segment);

		segment = segment->data.next;
		ensure("(3) data struct is not NULL", segment != NULL);
		ensure_equals("(3) data.failed is 4", segment->data.failed, 4u);

		segment = segment->data.next;
		ensure("(4) data struct is not NULL", segment != NULL);
		ensure_equals("(4) data.failed is 3", segment->data.failed, 3u);

		segment = segment->data.next;
		ensure("(5) data struct is not NULL", segment != NULL);
		ensure_equals("(5) data.failed is 2", segment->data.failed, 2u);

		segment = segment->data.next;
		ensure("(6) data struct is not NULL", segment != NULL);
		ensure_equals("(6) data.failed is 1", segment->data.failed, 1u);

		segment = segment->data.next;
		ensure("(7) data struct is not NULL", segment != NULL);
		ensure_equals("(7) data.failed is 0", segment->data.failed, 0u);

		segment = segment->data.next;
		ensure("(8) data struct is not NULL", segment != NULL);
		ensure_equals("(8) data.failed is 0", segment->data.failed, 0u);
		ensure_equals<void *>("(8) This is the last data struct",
			segment->data.next, NULL);
	}

	TEST_METHOD(10) {
		set_test_name("psg_reset_pool() resets the pool for reuse if the pool"
			" only has one pool data struct");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		void *origLast1 = pool->data.last;

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("psg_reset_pool succeeds",
			psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(11) {
		set_test_name("psg_reset_pool() fails to reset the pool for reuse if the pool"
			" has multiple pool data structs");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		void *origLast1 = pool->data.last;
		while (pool->data.next == NULL) {
			psg_pnalloc(pool, 32);
		}
		void *origLast2 = pool->data.next->data.last - 32;

		TEST_BASIC_ALLOCATIONS();
		ensure("psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure("At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("Exactly two pool data struct are allocated",
			pool->data.next->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure_equals("pool->data.next->data.failed is 0",
			pool->data.next->data.failed, 0u);
		ensure_equals<void *>("pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals<void *>("pool->data.next->data.last is correctly reset",
			pool->data.next->data.last, origLast2);
	}

	TEST_METHOD(12) {
		set_test_name("psg_reset_pool() frees large allocations correctly"
			" if the pool only has one pool data struct");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("psg_reset_pool succeeds",
			psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
	}

	TEST_METHOD(13) {
		set_test_name("psg_reset_pool() frees large allocations correctly"
			" if the pool has multiple pool data structs");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		while (pool->data.next == NULL) {
			psg_palloc(pool, sizeof(double));
		}

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure("At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("Exactly two pool data struct are allocated",
			pool->data.next->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
	}

	TEST_METHOD(14) {
		set_test_name("A pool that had 1 data struct can be reused after a reset");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);
		void *origLast1 = pool->data.last;

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("psg_reset_pool succeeds (1)",
			psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("psg_reset_pool succeeds (1)",
			psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure_equals<void *>("Only one pool data struct is allocated",
			pool->data.next, NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(15) {
		set_test_name("A pool that had multiple data structs can be reused after a reset");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		void *origLast1 = pool->data.last;
		while (pool->data.next == NULL) {
			psg_pnalloc(pool, 32);
		}
		void *origLast2 = pool->data.next->data.last - 32;

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("(1) psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("(1) pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals("(1) pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure("(1) At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("(1) Exactly two pool data struct are allocated",
			pool->data.next->data.next, NULL);
		ensure_equals<void *>("(1) pool->data.next->data.last is correctly reset",
			pool->data.next->data.last, origLast2);
		ensure_equals<void *>("(1) pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("(1) Nothing is allocated through the large list",
			pool->large, NULL);

		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
		ensure("(2) psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("(2) pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals("(2) pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure("(2) At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("(2) Exactly two pool data struct are allocated",
			pool->data.next->data.next, NULL);
		ensure_equals<void *>("(2) pool->data.next->data.last is correctly reset",
			pool->data.next->data.last, origLast2);
		ensure_equals<void *>("(2) pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("(2) Nothing is allocated through the large list",
			pool->large, NULL);
	}

	TEST_METHOD(16) {
		set_test_name("A pool that had its 'current' pointer bumped"
			" can be reused after a reset");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		void *origLast1 = pool->data.last;
		while (pool->current == pool) {
			psg_palloc(pool, sizeof(double));
		}
		ensure("psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));

		ensure_equals<void *>("pool->data.last is correctly reset",
			pool->data.last, origLast1);
		ensure_equals("pool->data.failed is 0",
			pool->data.failed, 0u);
		ensure("At least one pool data struct is allocated",
			pool->data.next != NULL);
		ensure_equals<void *>("pool->current points to the first pool data struct",
			pool->current, pool);
		ensure_equals<void *>("Nothing is allocated through the large list",
			pool->large, NULL);

		volatile char *largebuf;
		TEST_BASIC_ALLOCATIONS();
		TEST_LARGE_ALLOCATION();
	}

	TEST_METHOD(20) {
		set_test_name("Miscellaneous stress test");
		pool = psg_create_pool(PSG_DEFAULT_POOL_SIZE);

		for (unsigned i = 0; i < 1024; i++) {
			volatile char *largebuf;
			TEST_BASIC_ALLOCATIONS();
			TEST_LARGE_ALLOCATION();
		}
		ensure("psg_reset_pool fails",
			!psg_reset_pool(pool, PSG_DEFAULT_POOL_SIZE));
	}
}
