/*
 * mono-profiler-aot.c: Ahead of Time Compiler Profiler for Mono.
 *
 *
 * Copyright 2008-2009 Novell, Inc (http://www.novell.com)
 *
 * This profiler collects profiling information usable by the Mono AOT compiler
 * to generate better code. It saves the information into files under ~/.mono. 
 * The AOT compiler can load these files during compilation.
 * Currently, only the order in which methods were compiled is saved, 
 * allowing more efficient function ordering in the AOT files.
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#include <config.h>
#include <mono/metadata/profiler.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/assembly.h>

#include <mono/metadata/domain-internals.h>
#include <mono/metadata/metadata-internals.h>
#include <mono/utils/mono-mmap.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/mman.h>

//OSX only
#include <malloc/malloc.h>
#include <execinfo.h>
#include <dlfcn.h>

//XXX for now all configuration is here
#define PRINT_ALLOCATION TRUE
#define PRINT_MEMDOM FALSE
#define PRINT_RT_ALLOC FALSE
#define STRAY_ALLOCS FALSE
#define POKE_HASH_TABLES FALSE

void mono_profiler_startup (const char *desc);
static void dump_alloc_stats (void);
static FILE* outfile;

struct _MonoProfiler {
	int filling;
};

/*config */
static bool log_malloc, log_valloc, log_memdom;

/* Misc stuff */
static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;


static void
alloc_lock (void)
{
	pthread_mutex_lock (&alloc_mutex);
}

static void
alloc_unlock (void)
{
	pthread_mutex_unlock (&alloc_mutex);
}

/* Hashtable implementation. Can't use glib's as it reports memory usage itself */

/* maybe we could make it dynamic later */
#define TABLE_SIZE 1019

typedef struct _HashNode HashNode;

struct _HashNode {
	const void *key;
	HashNode *next;
};

typedef bool (*ht_equals) (const void *keyA, const void *keyB);
typedef unsigned int (*ht_hash) (const void *key);

typedef struct {
	ht_hash hash;
	ht_equals equals;
	void* table [TABLE_SIZE];
} HashTable;

static void
hashtable_init (HashTable *table, ht_hash hash, ht_equals equals)
{
	table->hash = hash;
	table->equals = equals;
	memset (table->table, 0, TABLE_SIZE * sizeof (void*));
}

static void
hashtable_cleanup (HashTable *table)
{
	int i;

	for (i = 0; i < TABLE_SIZE; ++i) {
		HashNode *node = NULL;
		for (node = table->table [i]; node;) {
			HashNode *next = node->next;
			g_free (node);
			node = next;
		}
		table->table [i] = NULL;
	}
}

static void*
hashtable_find (HashTable *table, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;

	HashNode *node = NULL;
	for (node = table->table [bucket]; node; node = node->next) {
		if (table->equals (key, node->key))
			break;
	}
	return node;
}

static void
hashtable_add (HashTable *table, HashNode *node, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;
	node->key = key;
	node->next = table->table [bucket];
	table->table [bucket] = node;
}

static HashNode*
hashtable_remove (HashTable *table, const void *key)
{
	int bucket = table->hash (key) % TABLE_SIZE;

	HashNode **prev;

	for (prev = (HashNode**)&table->table [bucket]; *prev; prev = &(*prev)->next) {
		HashNode *node = *prev;
		if (table->equals (node->key, key)) {
			*prev = node->next;
			node->next = NULL;
			return node;
		}
	}
	return NULL;
}

#define HT_FOREACH(HT,NODE_TYPE,NODE_VAR,CODE_BLOCK) {	\
	int __i;	\
	for (__i = 0; __i < TABLE_SIZE; ++__i) {	\
		HashNode *__node;	\
		for (__node = (HT)->table [__i]; __node; __node = __node->next) { 	\
			NODE_TYPE *NODE_VAR = (NODE_TYPE *)__node; 	\
			CODE_BLOCK \
		}	\
	}	\
}

static unsigned int
hash_ptr (const void *ptr)
{
	size_t addr = (size_t)ptr;
	return abs ((int)((addr * 1737350767) ^ ((addr * 196613) >> 16)));
}


static unsigned int
hash_str (const void *key)
{
	const char *str = key;
	int hash = 0;

	while (*str++)
		hash = (hash << 5) - (hash + *str);

	return abs (hash);
}

static bool
equals_ptr (const void *a, const void *b)
{
	return a == b;
}

static bool
equals_str (const void *a, const void *b)
{
	return !strcmp (a, b);
}

typedef struct {
	HashNode node;
	size_t size;
	size_t waste;
	const char *tag;
	const char *alloc_func;
} AllocInfo;

/* malloc tracking */
typedef struct {
	HashNode node;
	size_t size;
	size_t waste;
	size_t count;
} TagInfo;

typedef struct {
	HashTable table;
	size_t alloc_bytes;
	size_t alloc_waste;
	size_t alloc_count;
} TagBag;

typedef struct {
	HashNode node;
	int kind;
	TagBag tags;
} MemDomInfo;

static TagBag malloc_tags;
static HashTable alloc_table;
static size_t alloc_bytes, alloc_count, alloc_waste;

static void
tagbag_init (TagBag *bag)
{
	hashtable_init (&bag->table, hash_str, equals_str);
	bag->alloc_bytes = bag->alloc_waste = bag->alloc_count = 0;
}

static void
tagbag_cleanup (TagBag *bag)
{
	hashtable_cleanup (&bag->table);
}

static void __attribute__((noinline))
break_on_zero_size (void)
{
}

static void
update_tag (TagBag *tag_bag, const char *tag, ssize_t size, ssize_t waste)
{
	TagInfo *info = hashtable_find (&tag_bag->table, tag);

	if (size == 0)
		break_on_zero_size ();
	// if (!strcmp ("class:fields", tag)) {
	// 	printf ("update %s root %d size %zd\n", tag, tag_bag == &malloc_tags, size);
	// }

	if (!info) {
		info = malloc (sizeof (TagInfo));
		info->size = info->waste = info->count = 0;
		hashtable_add (&tag_bag->table, &info->node, tag);
	}

	info->size += size;
	info->waste += waste;

	tag_bag->alloc_bytes += size;
	tag_bag->alloc_waste += waste;

	if (size >= 0) {
		++info->count;
		++tag_bag->alloc_count;
	} else {
		--info->count;
		--tag_bag->alloc_count;
	}
}

static void __attribute__((noinline))
break_on_bad_runtime_malloc (void)
{
}

static const char *memdom_name[] = {
	"invalid",
	"appdomain",
	"image",
	"image-set",
};

#define MEMDOM_MAX 4
static size_t memdom_count [MEMDOM_MAX];
static HashTable memdom_table;

static void
memdom_new (MonoProfiler *prof, void* memdom, MonoProfilerMemoryDomain kind)
{
	alloc_lock ();
	MemDomInfo *info = malloc (sizeof (MemDomInfo));
	info->kind = kind;
	tagbag_init (&info->tags);

	hashtable_add (&memdom_table, &info->node, memdom);
	++memdom_count [kind];


	if (PRINT_MEMDOM)
		printf ("memdom new %p type %s\n", memdom, memdom_name [kind]);
	alloc_unlock ();
}

static void
memdom_destroy (MonoProfiler *prof, void* memdom)
{
	alloc_lock ();
	MemDomInfo *info = (MemDomInfo*)hashtable_remove (&memdom_table, memdom);

	if (info) {
		--memdom_count [info->kind];
		tagbag_cleanup (&info->tags);
		g_free (info);
	}

	if (PRINT_MEMDOM)
		printf ("memdom destroy %p\n", memdom);
	alloc_unlock ();
}

static void
memdom_alloc (MonoProfiler *prof, void* memdom, size_t size, const char *tag)
{
	alloc_lock ();
	MemDomInfo *info = hashtable_find (&memdom_table, memdom);
	if (info)
		update_tag (&info->tags, tag, size, 0);
	alloc_unlock ();
	if (PRINT_RT_ALLOC)
		printf ("memdom %p alloc %zu %s\n", memdom, size, tag);

	// dump_alloc_stats ();
}

static const char*
filter_bt (int skip, const char *filter_funcs)
{
	void *bt_ptrs [10];
 	int c = backtrace (bt_ptrs, 10);
	if (c) {
		int i;
		const char *it = NULL;
		for (i = 0; i < c - (skip + 1); ++i) {
			Dl_info info;
			if (!dladdr (bt_ptrs [skip + i], &info))
				continue;
			if (strstr (info.dli_sname, "_malloc") || strstr (info.dli_sname, "report_alloc"))
				continue;
			if (strstr (info.dli_sname, filter_funcs))
				continue;
			it = info.dli_sname;
			break;
		}
		return it;
	}
	return NULL;
}

static void
runtime_malloc_event (MonoProfiler *prof, void *address, size_t size, const char *tag)
{
	alloc_lock ();
	AllocInfo *info = hashtable_find (&alloc_table, address);
	if (!info) {
		printf ("stray alloc that didn't come from g_malloc %p\n", address);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	if (info->tag) {
		printf ("runtime reported same pointer twice %p %s x %s\n", address, info->tag, tag);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	if (info->size != size) {
		printf ("runtime reported pointer with different sizes %p %zu x %zu\n", address, info->size, size);
		break_on_bad_runtime_malloc ();
		goto done;
	}

	info->tag = tag;
	update_tag (&malloc_tags, tag, info->size, info->waste);

	// if (!strcmp (tag, "ghashtable")) {
	// 	const char *f = filter_bt (3, "g_hash_table");
	// 	// printf ("hashtable %p allocated from %s\n", address, f);
	// 	info->alloc_func = f;
	// }

done:
	alloc_unlock ();

	dump_alloc_stats ();
}

enum {
	RECORD_VALLOC = 1 << 10,
	RECORD_VFREE = 1 << 11,
	RECORD_MPROTECT = 1 << 12,
};

typedef struct {
	char *address;
	const char *tag;
	size_t length;
	int flags;
} VAllocRecord;

#define VALLOC_ENTRIES (4096 / sizeof (VAllocRecord) - 1)

typedef struct _VAllocRecordBucket VAllocRecordBucket;

struct _VAllocRecordBucket {
	int index;
	VAllocRecordBucket *next_bucket;
	VAllocRecord entries [VALLOC_ENTRIES];
};

static volatile int valloc_log_state;

static void __attribute__((noinline))
break_on_fail (void)
{
}


#define FAIL(ERR) do { printf ("FAILED: %s\n", ERR); break_on_fail (); exit (-1); } while (0)

static gboolean
lock_exclusive (void)
{
	do {
		/*
		A positive value means the lock is taken.
		TODO: Have a write-requested state to avoid writer starvation
		*/
		if (valloc_log_state == -1)
			FAIL ("Somebody messed with the exclusive lock, already locked");

		if (valloc_log_state) {
			usleep (1);
			continue;
		}
	} while (__sync_val_compare_and_swap (&valloc_log_state, 0, -1) != 0);
	__sync_synchronize ();
	return TRUE;
}

static void
unlock_exclusive (void)
{
	__sync_synchronize ();

	if (__sync_val_compare_and_swap (&valloc_log_state, -1, 0) != -1)
		FAIL ("Somebody messed with the exclusive lock, already unlocked");
}

static void
lock_shared (void)
{
	int old_count;
	do {
	retry:
		old_count = valloc_log_state;
		if (old_count < 0) {
			/* Exclusively locked - retry */
			/* FIXME: short back-off */
			goto retry;
		}
	} while (__sync_val_compare_and_swap (&valloc_log_state, old_count, old_count + 1) != old_count);

	__sync_synchronize ();
}

static void
unlock_shared (void)
{
	int old_count;
	__sync_synchronize ();

	do {
		old_count = valloc_log_state;
		if (old_count < 1)
			FAIL ("unlocking shared but lock count < 1");
	} while (__sync_val_compare_and_swap (&valloc_log_state, old_count, old_count - 1) != old_count);
}

	
static VAllocRecordBucket *current_bucket;

static VAllocRecordBucket*
alloc_bucket (void)
{
	VAllocRecordBucket *old, *bucket;

	old = current_bucket;
	if (old && old->index < VALLOC_ENTRIES)
		return old;

	bucket = mmap (NULL, sizeof (VAllocRecordBucket), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	bucket->index = 0;

	do {
		old = current_bucket;
		bucket->next_bucket = old;
	} while (__sync_val_compare_and_swap (&current_bucket, old, bucket) != old);
	return bucket;
}

static VAllocRecord*
get_next_record (void)
{
	VAllocRecordBucket *bucket;
	int index;

retry:
	bucket = current_bucket;
	if (!current_bucket)
		bucket = alloc_bucket ();
	while (bucket->index >= VALLOC_ENTRIES)
		bucket = alloc_bucket ();

	index = __sync_add_and_fetch (&bucket->index, 1) - 1;
	if (index >= VALLOC_ENTRIES)
		goto retry;	

	return &bucket->entries [index];
}


#define ALIGN_TO(val,align) ((((guint64)val) + ((align) - 1)) & ~((align) - 1))

static void
add_vm_record (void *address, size_t size, const char *tag, int flags)
{
	lock_shared ();

	VAllocRecord *record = get_next_record ();
	record->address = address;
	record->length = ALIGN_TO (size, 4096);
	record->flags = flags;
	record->tag = tag;

	unlock_shared ();
}

static const char *
op_name (int flags)
{
	if (flags & RECORD_VALLOC)
		return "valloc";
	if (flags & RECORD_VFREE)
		return "vfree";
	if (flags & RECORD_MPROTECT)
		return "mprotect";

	return "no-clue";
}

static const char*
op_flags (int flags)
{
	//super evil, callers will hate me for that :D
	static char buffer [1024];

	buffer [0] = 0;

	if (flags & MONO_MMAP_READ)
		strlcat (buffer, "read ", sizeof (buffer));
	if (flags & MONO_MMAP_WRITE)
		strlcat (buffer, "write ", sizeof (buffer));
	if (flags & MONO_MMAP_EXEC)
		strlcat (buffer, "exec ", sizeof (buffer));
	if (flags & MONO_MMAP_DISCARD)
		strlcat (buffer, "discard ", sizeof (buffer));
	if (flags & MONO_MMAP_PRIVATE)
		strlcat (buffer, "private ", sizeof (buffer));
	if (flags & MONO_MMAP_SHARED)
		strlcat (buffer, "shared ", sizeof (buffer));
	if (flags & MONO_MMAP_ANON)
		strlcat (buffer, "anon ", sizeof (buffer));
	if (flags & MONO_MMAP_FIXED)
		strlcat (buffer, "fixed ", sizeof (buffer));
	if (flags & MONO_MMAP_32BIT)
		strlcat (buffer, "32bits ", sizeof (buffer));

	return buffer;
}

typedef struct _VMEntry VMEntry;
struct _VMEntry {
	char *address;
	size_t length;
	int prot;
	const char *tag;
	VMEntry *next;
};

static VMEntry *vm_entries;

static VMEntry*
find_overlap (char *address, size_t size, VMEntry ***prev_entry_slot)
{
	VMEntry **prev;

	for (prev = &vm_entries; *prev; prev = &(*prev)->next) {
		VMEntry *e = *prev;
		if ((e->address <= address && (e->address + e->length) > address) ||
		(e->address > address && (e->address < address + size))) {
			if (prev_entry_slot)
				*prev_entry_slot = prev;
			return e;
		}
	}
	if (prev_entry_slot)
		*prev_entry_slot = NULL;
	return NULL;

}

#define PROT_MASK (MONO_MMAP_READ | MONO_MMAP_WRITE | MONO_MMAP_EXEC)

static void
record_op (VAllocRecord *rec)
{
	if (rec->flags & RECORD_VALLOC) {
		VMEntry * entry = find_overlap (rec->address, rec->length, NULL);
		if (entry) {
			fprintf (outfile, "found alloc to existing entry [%p %p] new [%p %p]\n",
				entry->address, entry->address + entry->length,
				rec->address, rec->address + rec->length);
			FAIL ("BAD ALLOC FOUND\n");
		}
		entry = malloc (sizeof (VMEntry));
		entry->address = rec->address;
		entry->length = rec->length;
		entry->prot = rec->flags & PROT_MASK;
		entry->tag = rec->tag;
		entry->next = vm_entries;
		vm_entries = entry;
	} else if (rec->flags & RECORD_VFREE) {
		//this is terribly naive, but given how low frequency and volume this is, meh.
		VMEntry **prev = NULL;
		VMEntry * entry = find_overlap (rec->address, rec->length, &prev);
		if (!entry) {
			fprintf (outfile, "bad vfree [%p %p] to unmmaped region\n", rec->address, rec->address + rec->length);
			FAIL ("BAD VFREE FOUND\n");
		}

		//the vfree must be fully covered by the region
		if (rec->address < entry->address || (rec->address + rec->length) > (entry->address + entry->length)) {
			fprintf (outfile, "bad vfree [%p %p] escapes corresponding regions [%p %p]\n",
				rec->address, rec->address + rec->length,
				entry->address, entry->address + entry->length);
			FAIL ("BAD VFREE FOUND\n");
		}

		//full covered region
		if (rec->length == entry->length) {
			*prev = entry->next;
			free (entry);
		} else  {
			//are we splitting the region?
			if (rec->address > entry->address && (rec->address + rec->length) < (entry->address + entry->length)) {
				VMEntry *tail = malloc (sizeof (VMEntry));
				tail->address = rec->address + rec->length;
				tail->length = (entry->address + entry->length) - tail->address;
				tail->prot = entry->prot;
				tail->tag = entry->tag;
				tail->next = entry->next;
				entry->next = tail;

				//now trim entry
				entry->length = rec->address - entry->address;
			} else {
				//no splitting needed
				entry->length = entry->length - rec->length;
				if (rec->address == entry->address)
					entry->address = rec->address + rec->length; //freed the beginning
			}
		}
	} else if (rec->flags & RECORD_MPROTECT) {
		//FIXME
	}

}

static void
dump_actual_map (void)
{
	VMEntry *entry;
	fprintf (outfile, "VM Map:\n");
	for (entry = vm_entries; entry; entry = entry->next) {
		fprintf (outfile, "[%p %p] ( %s) [%s]\n", entry->address, entry->address + entry->length, op_flags (entry->prot), entry->tag);
	}
}

static void
dump_vmmap (void)
{
	fprintf (outfile, "do we have records to dump? %p\n", current_bucket);

	VAllocRecordBucket *bucket;
	lock_exclusive ();
	bucket = current_bucket;
	current_bucket = NULL;
	unlock_exclusive ();

	while (bucket) {
		int i;
		for (i = 0; i < VALLOC_ENTRIES; ++i) {
			VAllocRecord *rec = &bucket->entries [i];
			if (rec->flags) {
				fprintf (outfile, "\t%s [%p - %p] (%zd bytes) { %s} (%s)\n",
					op_name (rec->flags),
					rec->address, (char*)rec->address + rec->length, rec->length, op_flags (rec->flags), rec->tag ? rec->tag : "-"); 

				record_op (rec);
			}
		}
		VAllocRecordBucket *next = bucket->next_bucket;
		munmap (bucket, sizeof (VAllocRecordBucket));

		bucket = next;
	}

	dump_actual_map ();
}

static void
runtime_valloc_event (MonoProfiler *prof, void *address, size_t size, int flags, const char *tag)
{
	add_vm_record (address, size, tag, flags | RECORD_VALLOC);
}

static void
runtime_vfree_event (MonoProfiler *prof, void *address, size_t size)
{
	add_vm_record (address, size, NULL, RECORD_VFREE);
}

static void
runtime_mprotect_event (MonoProfiler *prof, void *address, size_t size, int flags)
{
	add_vm_record (address, size, NULL, flags | RECORD_MPROTECT);
}


static void __attribute__((noinline))
break_on_malloc_waste (void)
{
}

static void __attribute__((noinline))
break_on_large_alloc (void)
{
}

static void
del_alloc (void *address)
{
	alloc_lock ();
	AllocInfo *info = (AllocInfo*)hashtable_remove (&alloc_table, address);

	if (info) {
		alloc_bytes -= info->size;
		--alloc_count;
		alloc_waste -= info->waste;

		if (info->tag)
			update_tag (&malloc_tags, info->tag, -info->size, -info->waste);

		g_free (info);
	}

	alloc_unlock ();
}

static void
add_alloc (void *address, size_t size, const char *tag)
{
	AllocInfo *info = malloc (sizeof (AllocInfo));
	info->size = size;
	info->tag = NULL;
	info->waste = malloc_size (address) - size;

	alloc_lock ();
	hashtable_add (&alloc_table, &info->node, address);

	alloc_bytes += size;
	++alloc_count;
	alloc_waste += info->waste;

	if (STRAY_ALLOCS) {
		int skip = 2;
		void *bt_ptrs [10];
	 	int c = backtrace (bt_ptrs, 10);
		if (c) {
			int i;
			const char *it = NULL;
			for (i = 0; i < c - (skip + 1); ++i) {
				Dl_info info;
				if (!dladdr (bt_ptrs [skip + i], &info))
					continue;
				if (strstr (info.dli_sname, "g_malloc") || strstr (info.dli_sname, "g_calloc") || strstr (info.dli_sname, "m_malloc")
					|| strstr (info.dli_sname, "g_realloc") || strstr (info.dli_sname, "g_memdup") || strstr (info.dli_sname, "g_slist_alloc") || strstr (info.dli_sname, "g_list_alloc")
					|| strstr (info.dli_sname, "g_strndup") || strstr (info.dli_sname, "g_vasprintf") || strstr (info.dli_sname, "g_slist_prepend"))
					continue;
				it = info.dli_sname;
				break;
			}
			info->alloc_func = it;
		}
	}

	alloc_unlock ();

	if (info->waste > 10)
		break_on_malloc_waste ();
	if (size > 1000)
		break_on_large_alloc ();
}

static void
dump_tag_bag (TagBag *bag, const char *name, size_t mempool_size)
{
	fprintf (outfile, ",\n");
	fprintf (outfile, "\t\t\"%s\": {\n", name);
	fprintf (outfile,"\t\t\t\"alloc-bytes\": %zu,\n", bag->alloc_bytes);
	fprintf (outfile,"\t\t\t\"alloc-count\": %zu,\n", bag->alloc_count);
	fprintf (outfile,"\t\t\t\"alloc-waste\": %zu,\n", bag->alloc_waste);
	if (mempool_size)
		fprintf (outfile,"\t\t\t\"mempool-size\": %zu,\n", mempool_size);
	fprintf (outfile, "\t\t\t\"tags\": [\n");


	size_t size, waste, count;
	size = waste = count = 0;
	HT_FOREACH (&bag->table, TagInfo, info, {
		if (info->size) {
			fprintf (outfile, "\t\t\t\t[ \"%s\", %zu, %zu, %zu],\n", info->node.key, info->size, info->waste, info->count);
			size += info->size;
			waste += info->waste;
			count += info->count;
		}
	});
	fprintf (outfile, "\t\t\t\t[\"total\", %zu, %zu, %zu]\n", size, waste, count);

	fprintf (outfile, "\t\t\t]\n");
	fprintf (outfile, "\t\t}");
}

static void
dump_memdom (MemDomInfo *memdom)
{
	char name [1024];
	name [0] = 0;
	MonoMemPool *mempool = NULL;
	switch (memdom->kind) {
	case MONO_PROFILE_MEMDOM_APPDOMAIN: {
		MonoDomain *domain = (MonoDomain *)memdom->node.key;
		snprintf (name, 1023, "domain_%d", domain->domain_id);
		mempool = domain->mp;
		break;
	}

	case MONO_PROFILE_MEMDOM_IMAGE: {
		MonoImage *image = (MonoImage *)memdom->node.key;
		snprintf (name, 1023, "image_%s", image->module_name);
		mempool = image->mempool;
		break;
	}

	case MONO_PROFILE_MEMDOM_IMAGE_SET: {
		MonoImageSet *set = (MonoImageSet*)memdom->node.key;
		strlcat (name, "imageset", sizeof (name));
		int i;
		for (i = 0; i < set->nimages; ++i) {
			if (strlcat (name, "_", sizeof (name)) >= sizeof(name))
				break;
			if (strlcat (name, set->images [i]->module_name, sizeof (name)) >= sizeof(name))
				break;
		}
		mempool = set->mempool;
		break;
	}
	}

	dump_tag_bag (&memdom->tags, name, mempool ? mono_mempool_get_allocated (mempool) : 0);
}

static void
dump_stats (void)
{
	alloc_lock ();

	if (!outfile) {
		alloc_unlock ();
		return;
	}

	static int dump_count;
	if (dump_count)
		fprintf (outfile, ",\n");
	++dump_count;

	fprintf (outfile, "\t{\n");

	if (log_malloc) {
		fprintf (outfile, "\t\t\"alloc\": %zu,\n", alloc_bytes);
		fprintf (outfile, "\t\t\"alloc-count\": %zu,\n", alloc_count);
		fprintf (outfile, "\t\t\"alloc-waste\": %zu", alloc_waste);

		dump_tag_bag (&malloc_tags, "malloc", 0);

		if (STRAY_ALLOCS) {
			HT_FOREACH (&alloc_table, AllocInfo, info, {
					if (!info->tag)
						printf ("stay alloc from %s\n", info->alloc_func);
			});
		}

		if (POKE_HASH_TABLES) {
			HT_FOREACH (&alloc_table, AllocInfo, info, {
				if (!info->tag || strcmp ("ghashtable", info->tag))
					continue;
				printf ("hashtable size %d from %s\n", g_hash_table_size ((GHashTable*)info->node.key), info->alloc_func);
			});
		}
	}

	if (log_memdom) {
		HT_FOREACH (&memdom_table, MemDomInfo, info, {
			dump_memdom (info);
		});
	}

	if (log_valloc)
		dump_vmmap ();

	fprintf (outfile, "\n\t}");


	alloc_unlock ();
}

static void
icall_dump_allocs (void)
{
	dump_stats ();
}

static void
dump_alloc_stats (void)
{
	static int last_time;

	if (!PRINT_ALLOCATION)
		return;
	++last_time;
	if (last_time % 10000)
		return;
	dump_stats ();
}

static void *
platform_malloc (size_t size)
{	
	void * res = malloc (size);
	add_alloc (res, size, NULL);

	// dump_alloc_stats ();
	return res;
}

static void *
platform_realloc (void *mem, size_t count)
{
	AllocInfo *old = hashtable_find (&alloc_table, mem);
	const char *tag = old ? old->tag : NULL;

	del_alloc (mem);
	void * res = realloc (mem, count);
	add_alloc (res, count, tag);

	// dump_alloc_stats ();
	return res;
 }

static void
platform_free (void *mem)
{
	del_alloc (mem);
	free (mem);

	// dump_alloc_stats ();
}

static void*
platform_calloc (size_t count, size_t size)
{
	void * res = calloc (count, size);
	add_alloc (res, count * size, NULL);

	// dump_alloc_stats ();
	return res;
}

static void
runtime_inited (MonoProfiler *prof)
{
	mono_add_internal_call ("Mono.MemProf::DumpAllocationInfo", icall_dump_allocs);
}

static void
prof_shutdown (MonoProfiler *prof)
{
	alloc_lock ();
	fprintf (outfile, "\n]\n");
	fflush (outfile);
	fclose (outfile);
	outfile = NULL;
	alloc_unlock ();
}

static void
usage (bool do_exit)
{
	printf ("Usage: mono --profile=malloc[:OPTION1[,OPTION2...]] program.exe\n");
	printf ("Options:\n");
	printf ("\thelp                 show this usage info\n");

	if (do_exit)
		exit (1);
}


static const char*
match_option (const char* p, const char *opt, char **rval)
{
	int len = strlen (opt);
	if (strncmp (p, opt, len) == 0) {
		if (rval) {
			if (p [len] == '=' && p [len + 1]) {
				const char *opt = p + len + 1;
				const char *end = strchr (opt, ',');
				char *val;
				int l;
				if (end == NULL) {
					l = strlen (opt);
				} else {
					l = end - opt;
				}
				val = (char *)malloc (l + 1);
				memcpy (val, opt, l);
				val [l] = 0;
				*rval = val;
				return opt + l;
			}
			if (p [len] == 0 || p [len] == ',') {
				*rval = NULL;
				return p + len + (p [len] == ',');
			}
			usage (true);
		} else {
			if (p [len] == 0)
				return p + len;
			if (p [len] == ',')
				return p + len + 1;
		}
	}
	return p;
}

/* the entry point */
void
mono_profiler_startup (const char *desc)
{
	char filename[1024];
	MonoProfiler *prof;

	prof = g_new0 (MonoProfiler, 1);

	snprintf (filename, 1023, "malloc_log_%d.txt", getpid ());

	const char *p = desc, *opt = NULL;
	if (strncmp (p, "malloc", 6))
		usage (true);
	p += 6;
	if (*p == ':')
		p++;
	for (; *p; p = opt) {
		// char *val;
		if (*p == ',') {
			opt = p + 1;
			continue;
		}

		if ((opt = match_option (p, "help", NULL)) != p) {
			usage (false);
		} else if ((opt = match_option (p, "log-malloc", NULL)) != p) {
			log_malloc = TRUE;
		} else if ((opt = match_option (p, "log-valloc", NULL)) != p) {
			log_valloc = TRUE;
		}  else if ((opt = match_option (p, "log-memdom", NULL)) != p) {
			log_memdom = TRUE;
		}
	}
	

	// outfile = fopen (filename, "w+");
	outfile = stdout;
	if (!outfile) {
		printf ("failed to open! due to %s\n", strerror (errno));
		exit (-2);
	}
	fprintf (outfile, "[\n");

	tagbag_init (&malloc_tags);
	hashtable_init (&alloc_table, hash_ptr, equals_ptr);
	hashtable_init (&memdom_table, hash_ptr, equals_ptr);

	mono_profiler_install (prof, prof_shutdown);
	mono_profiler_install_memdom (memdom_new, memdom_destroy, memdom_alloc);
	mono_profiler_install_malloc (runtime_malloc_event);
	mono_profiler_install_valloc (runtime_valloc_event, runtime_vfree_event, runtime_mprotect_event);
	mono_profiler_install_runtime_initialized (runtime_inited);

	MonoAllocatorVTable alloc_vt = {
		.version = MONO_ALLOCATOR_VTABLE_VERSION,
		.malloc = platform_malloc,
		.realloc = platform_realloc,
		.free = platform_free,
		.calloc = platform_calloc
	};

	if (!mono_set_allocator_vtable (&alloc_vt))
		printf ("set allocator failed :(\n");

}
