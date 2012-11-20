/*
 * Copyright 2001-2003 Ximian, Inc
 * Copyright 2003-2010 Novell, Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
extern long long stat_copy_object_called_nursery;
extern long long stat_objects_copied_nursery;

extern long long stat_nursery_copy_object_failed_from_space;
extern long long stat_nursery_copy_object_failed_forwarded;
extern long long stat_nursery_copy_object_failed_pinned;

extern long long stat_slots_allocated_in_vain;

/*
 * This function can be used even if the vtable of obj is not valid
 * anymore, which is the case in the parallel collector.
 */
static inline void
general_copy_object_no_checks (char *destination, MonoVTable *vt, void *obj, mword objsize)
{
#ifdef __GNUC__
	static const void *copy_labels [] = { &&LAB_0, &&LAB_1, &&LAB_2, &&LAB_3, &&LAB_4, &&LAB_5, &&LAB_6, &&LAB_7, &&LAB_8 };
#endif

	SGEN_ASSERT (9, vt->klass->inited, "vtable %p for class %s:%s was not initialized", vt, vt->klass->name_space, vt->klass->name);
	SGEN_LOG (9, " (to %p, %s size: %lu)", destination, ((MonoObject*)obj)->vtable->klass->name, (unsigned long)objsize);
	binary_protocol_copy (obj, destination, vt, objsize);

	if (G_UNLIKELY (MONO_GC_OBJ_MOVED_ENABLED ())) {
		int dest_gen = sgen_ptr_in_nursery (destination) ? GENERATION_NURSERY : GENERATION_OLD;
		int src_gen = sgen_ptr_in_nursery (obj) ? GENERATION_NURSERY : GENERATION_OLD;
		MONO_GC_OBJ_MOVED ((mword)destination, (mword)obj, dest_gen, src_gen, objsize, vt->klass->name_space, vt->klass->name);
	}

#ifdef __GNUC__
	if (objsize <= sizeof (gpointer) * 8) {
		mword *dest = (mword*)destination;
		goto *copy_labels [objsize / sizeof (gpointer)];
	LAB_8:
		(dest) [7] = ((mword*)obj) [7];
	LAB_7:
		(dest) [6] = ((mword*)obj) [6];
	LAB_6:
		(dest) [5] = ((mword*)obj) [5];
	LAB_5:
		(dest) [4] = ((mword*)obj) [4];
	LAB_4:
		(dest) [3] = ((mword*)obj) [3];
	LAB_3:
		(dest) [2] = ((mword*)obj) [2];
	LAB_2:
		(dest) [1] = ((mword*)obj) [1];
	LAB_1:
		;
	LAB_0:
		;
	} else {
		/*can't trust memcpy doing word copies */
		mono_gc_memmove (destination + sizeof (mword), (char*)obj + sizeof (mword), objsize - sizeof (mword));
	}
#else
		mono_gc_memmove (destination + sizeof (mword), (char*)obj + sizeof (mword), objsize - sizeof (mword));
#endif
	/* adjust array->bounds */
	SGEN_ASSERT (9, vt->gc_descr, "vtable %p for class %s:%s has no gc descriptor", vt, vt->klass->name_space, vt->klass->name);

	if (G_UNLIKELY (vt->rank && ((MonoArray*)obj)->bounds)) {
		MonoArray *array = (MonoArray*)destination;
		array->bounds = (MonoArrayBounds*)((char*)destination + ((char*)((MonoArray*)obj)->bounds - (char*)obj));
		SGEN_LOG (9, "Array instance %p: size: %lu, rank: %d, length: %lu", array, (unsigned long)objsize, vt->rank, (unsigned long)mono_array_length (array));
	}
	if (G_UNLIKELY (mono_profiler_events & MONO_PROFILE_GC_MOVES))
		sgen_register_moved_object (obj, destination);
}

static inline void
par_copy_object_no_checks (char *destination, MonoVTable *vt, void *obj, mword objsize, SgenGrayQueue *queue)
{
	general_copy_object_no_checks (destination, vt, obj, objsize);
	if (queue) {
		SGEN_LOG (9, "Enqueuing gray object %p (%s)", obj, sgen_safe_name (obj));
		GRAY_OBJECT_ENQUEUE (queue, destination);
	}
}

/*
 * This can return OBJ itself on OOM.
 */
#ifdef _MSC_VER
static __declspec(noinline) void*
#else
static G_GNUC_UNUSED void* __attribute__((noinline))
#endif
serial_copy_object_no_checks (void *obj, SgenGrayQueue *queue)
{
	MonoVTable *vt = ((MonoObject*)obj)->vtable;
	gboolean has_references = SGEN_VTABLE_HAS_REFERENCES (vt);
	mword objsize = SGEN_ALIGN_UP (sgen_par_object_get_size (vt, (MonoObject*)obj));
	char *destination = COLLECTOR_SERIAL_ALLOC_FOR_PROMOTION (obj, objsize, has_references);

	if (G_UNLIKELY (!destination)) {
		collector_pin_object (obj, queue);
		sgen_set_pinned_from_failed_allocation (objsize);
		return obj;
	}

	*(MonoVTable**)destination = vt;
	general_copy_object_no_checks (destination, vt, obj, objsize);
	if (has_references)
		GRAY_OBJECT_ENQUEUE (queue, destination);

	/* set the forwarding pointer */
	SGEN_FORWARD_OBJECT (obj, destination);

	return destination;
}
