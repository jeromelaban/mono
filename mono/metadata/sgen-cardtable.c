/*
 * sgen-cardtable.c: Card table implementation for sgen
 *
 * Author:
 * 	Rodrigo Kumpera (rkumpera@novell.com)
 *
 * Copyright 2010 Novell, Inc (http://www.novell.com)
 *
 */

#define CARD_COUNT_BITS (32 - CARD_BITS)
#define CARD_COUNT_IN_BYTES (1 << CARD_COUNT_BITS)
#define CARD_MASK ((1 << CARD_COUNT_BITS) - 1)

static guint8 *cardtable;

#ifdef OVERLAPPING_CARDS

static guint8 *shadow_cardtable;

static guint8*
sgen_card_table_get_shadow_card_address (mword address)
{
	return shadow_cardtable + ((address >> CARD_BITS) & CARD_MASK);
}

gboolean
sgen_card_table_card_begin_scanning (mword address)
{
	return *sgen_card_table_get_shadow_card_address (address) != 0;
}

static guint8*
sgen_card_table_get_card_address (mword address)
{
	return cardtable + ((address >> CARD_BITS) & CARD_MASK);
}

gboolean
sgen_card_table_region_begin_scanning (mword start, mword end)
{
	while (start <= end) {
		if (sgen_card_table_card_begin_scanning (start))
			return TRUE;
		start += CARD_SIZE_IN_BYTES;
	}
	return FALSE;
}

#else

static guint8*
sgen_card_table_get_card_address (mword address)
{
	return cardtable + (address >> CARD_BITS);
}

static gboolean
sgen_card_table_address_is_marked (mword address)
{
	return *sgen_card_table_get_card_address (address) != 0;
}

gboolean
sgen_card_table_card_begin_scanning (mword address)
{
	guint8 *card = sgen_card_table_get_card_address (address);
	gboolean res = *card;
	*card = 0;
	return res;
}

gboolean
sgen_card_table_region_begin_scanning (mword start, mword end)
{
	gboolean res = FALSE;
	mword old_start = start;
	while (start <= end) {
		if (sgen_card_table_address_is_marked (start)) {
			res = TRUE;
			break;
		}
		start += CARD_SIZE_IN_BYTES;
	}

	sgen_card_table_reset_region (old_start, end);
	return res;
}
#endif


void
sgen_card_table_mark_address (mword address)
{
	*sgen_card_table_get_card_address (address) = 1;
}


void*
sgen_card_table_align_pointer (void *ptr)
{
	return (void*)((mword)ptr & ~(CARD_SIZE_IN_BYTES - 1));
}

void
sgen_card_table_reset_region (mword start, mword end)
{
	memset (sgen_card_table_get_card_address (start), 0, (end - start) >> CARD_BITS);
}

void
sgen_card_table_mark_range (mword address, mword size)
{
	mword end = address + size;
	do {
		sgen_card_table_mark_address (address);
		address += CARD_SIZE_IN_BYTES;
	} while (address < end);
}

static void
card_table_init (void)
{
	cardtable = mono_sgen_alloc_os_memory (CARD_COUNT_IN_BYTES, TRUE);
#ifdef OVERLAPPING_CARDS
	shadow_cardtable = mono_sgen_alloc_os_memory (CARD_COUNT_IN_BYTES, TRUE);
#endif
}


void los_scan_card_table (GrayQueue *queue);
void los_iterate_live_block_ranges (sgen_cardtable_block_callback callback);

#ifdef OVERLAPPING_CARDS

static void
move_cards_to_shadow_table (mword start, mword size)
{
	guint8 *from = sgen_card_table_get_card_address (start);
	guint8 *to = sgen_card_table_get_shadow_card_address (start);
	size_t bytes = size >> CARD_BITS;
	memcpy (to, from, bytes);
}

#endif

static void
clear_cards (mword start, mword size)
{
	memset (sgen_card_table_get_card_address (start), 0, size >> CARD_BITS);
}

static void
scan_from_card_tables (void *start_nursery, void *end_nursery, GrayQueue *queue)
{
#ifdef OVERLAPPING_CARDS
	/*First we copy*/
	major.iterate_live_block_ranges (move_cards_to_shadow_table);
	los_iterate_live_block_ranges (move_cards_to_shadow_table);

	/*Then we clear*/
	card_table_clear ()
#endif
	major.scan_card_table (queue);
	los_scan_card_table (queue);
}

static void
card_table_clear (void)
{
	/*XXX we could do this in 2 ways. using mincore or iterating over all sections/los objects */
	major.iterate_live_block_ranges (clear_cards);
	los_iterate_live_block_ranges (clear_cards);
}

#if 0

#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

static void
collect_faulted_cards (void)
{
#define CARD_PAGES (CARD_COUNT_IN_BYTES / 4096)
	int i, count = 0;
	unsigned char faulted [CARD_PAGES] = { 0 };
	mincore (cardtable, CARD_COUNT_IN_BYTES, faulted);

	for (i = 0; i < CARD_PAGES; ++i) {
		if (faulted [i])
			++count;
	}

	printf ("TOTAL card pages %d faulted %d\n", CARD_PAGES, count);
}
#endif
