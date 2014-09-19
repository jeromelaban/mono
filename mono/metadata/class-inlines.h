/* 
 * Copyright 2014 Xamarin Inc
 */
#ifndef __MONO_METADATA_CLASS_INLINES_H__
#define __MONO_METADATA_CLASS_INLINES_H__

#include <mono/metadata/class-internals.h>

static inline gboolean
mono_class_is_boring (MonoClass *class)
{
	return class->class_kind == MONO_CLASS_BORING;
}

static inline gboolean
mono_class_is_gtd (MonoClass *class)
{
	return class->class_kind == MONO_CLASS_GTD;
}

static inline gboolean
mono_class_is_ginst (MonoClass *class)
{
	return class->class_kind == MONO_CLASS_GINST;
}

static inline gboolean
mono_class_is_array (MonoClass *class)
{
	return class->class_kind == MONO_CLASS_ARRAY;
}

static inline gboolean
mono_class_is_pointer (MonoClass *class)
{
	return class->class_kind == MONO_CLASS_POINTER;
}


#endif