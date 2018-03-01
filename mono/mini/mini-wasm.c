#include "mini.h"
#include "mini-runtime.h"
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/seq-points-data.h>
#include <mono/mini/aot-runtime.h>
#include <mono/mini/seq-points.h>

#include <emscripten.h>


static int log_level = 1;

#define DEBUG_PRINTF(level, ...) do { if (G_UNLIKELY ((level) <= log_level)) { fprintf (stdout, __VA_ARGS__); } } while (0)


//XXX put this on mini-wasm.h
void mono_wasm_enable_debugging (void);
void wasm_breakpoint_hit (void);
void mono_wasm_set_timeout (int timeout, int id);

//functions exported to be used by JS
EMSCRIPTEN_KEEPALIVE int mono_wasm_set_breakpoint (char *mvid, int method_token, int il_offset);
EMSCRIPTEN_KEEPALIVE void mono_set_timeout_exec (int id);
EMSCRIPTEN_KEEPALIVE int mono_wasm_current_bp_id (void);
EMSCRIPTEN_KEEPALIVE void mono_wasm_enum_frames (void);

//JS functions imported that we use
extern void mono_wasm_add_frame (int il_offset, int method_token, const char *assembly_mvid);
extern void mono_wasm_fire_bp (void);
extern void mono_set_timeout (int t, int d);


gpointer
mono_arch_get_this_arg_from_call (mgreg_t *regs, guint8 *code)
{
	g_error ("mono_arch_get_this_arg_from_call");
}

gpointer
mono_arch_get_delegate_virtual_invoke_impl (MonoMethodSignature *sig, MonoMethod *method, int offset, gboolean load_imt_reg)
{
	g_error ("mono_arch_get_delegate_virtual_invoke_impl");
}


void
mono_arch_cpu_init (void)
{
	// printf ("mono_arch_cpu_init\n");
}

void
mono_arch_finish_init (void)
{
	// printf ("mono_arch_finish_init\n");
}

void
mono_arch_init (void)
{
	// printf ("mono_arch_init\n");
}

void
mono_arch_cleanup (void)
{
}

void
mono_arch_register_lowlevel_calls (void)
{
}

void
mono_arch_flush_register_windows (void)
{
}

void
mono_arch_free_jit_tls_data (MonoJitTlsData *tls)
{
}


MonoMethod*
mono_arch_find_imt_method (mgreg_t *regs, guint8 *code)
{
	g_error ("mono_arch_find_static_call_vtable");
	return (MonoMethod*) regs [MONO_ARCH_IMT_REG];
}

MonoVTable*
mono_arch_find_static_call_vtable (mgreg_t *regs, guint8 *code)
{
	g_error ("mono_arch_find_static_call_vtable");
	return (MonoVTable*) regs [MONO_ARCH_RGCTX_REG];
}

gpointer
mono_arch_build_imt_trampoline (MonoVTable *vtable, MonoDomain *domain, MonoIMTCheckItem **imt_entries, int count, gpointer fail_tramp)
{
	g_error ("mono_arch_build_imt_trampoline");
}

guint32
mono_arch_cpu_enumerate_simd_versions (void)
{
	return 0;
}

guint32
mono_arch_cpu_optimizations (guint32 *exclude_mask)
{
	return 0;
}

GSList*
mono_arch_get_delegate_invoke_impls (void)
{
	g_error ("mono_arch_get_delegate_invoke_impls");
	return NULL;
}

gpointer
mono_arch_get_delegate_invoke_impl (MonoMethodSignature *sig, gboolean has_target)
{
	g_error ("mono_arch_get_delegate_invoke_impl");
	return NULL;
}

mgreg_t
mono_arch_context_get_int_reg (MonoContext *ctx, int reg)
{
	g_error ("mono_arch_context_get_int_reg");
	return 0;
}

int
mono_arch_get_argument_info (MonoMethodSignature *csig, int param_count, MonoJitArgumentInfo *arg_info)
{
	g_error ("mono_arch_get_argument_info");
	return 0;

}

void
mono_runtime_setup_stat_profiler (void)
{
	g_error ("mono_runtime_setup_stat_profiler");
}


void
mono_runtime_shutdown_stat_profiler (void)
{
	g_error ("mono_runtime_shutdown_stat_profiler");
}


gboolean
MONO_SIG_HANDLER_SIGNATURE (mono_chain_signal)
{
	g_error ("mono_chain_signal");
	
	return FALSE;
}

void
mono_runtime_install_handlers (void)
{
}

void
mono_runtime_cleanup_handlers (void)
{
}

gboolean
mono_thread_state_init_from_handle (MonoThreadUnwindState *tctx, MonoThreadInfo *info, void *sigctx)
{
	g_error ("WASM systems don't support mono_thread_state_init_from_handle");
	return FALSE;
}


EMSCRIPTEN_KEEPALIVE void
mono_set_timeout_exec (int id)
{
	ERROR_DECL (error);
	MonoClass *klass = mono_class_load_from_name (mono_defaults.corlib, "System.Threading", "WasmRuntime");
	g_assert (klass);

	MonoMethod *method = mono_class_get_method_from_name (klass, "TimeoutCallback", -1);
	g_assert (method);

	gpointer params[1] = { &id };
	MonoObject *exc = NULL;

	mono_runtime_try_invoke (method, NULL, params, &exc, error);

	//YES we swallow exceptions cuz there's nothing much we can do from here.
	//FIXME Maybe call the unhandled exception function?
	if (!is_ok (error)) {
		printf ("timeout callback failed due to %s\n", mono_error_get_message (error));
		mono_error_cleanup (error);
	}

	if (exc) {
		char *type_name = mono_type_get_full_name (mono_object_get_class (exc));
		printf ("timeout callback threw a %s\n", type_name);
		g_free (type_name);
	}
}

void
mono_wasm_set_timeout (int timeout, int id)
{
	mono_set_timeout (timeout, id);
}

void
mono_arch_register_icall (void)
{
	mono_add_internal_call ("System.Threading.WasmRuntime::SetTimeout", mono_wasm_set_timeout);
}

/*
The following functions don't belong here, but are due to laziness.
*/
gboolean mono_w32file_get_volume_information (const gunichar2 *path, gunichar2 *volumename, gint volumesize, gint *outserial, gint *maxcomp, gint *fsflags, gunichar2 *fsbuffer, gint fsbuffersize);
void * getgrnam (const char *name);
void * getgrgid (gid_t gid);
int inotify_init (void);
int inotify_rm_watch (int fd, int wd);
int inotify_add_watch (int fd, const char *pathname, uint32_t mask);
int sem_timedwait (sem_t *sem, const struct timespec *abs_timeout);


//w32file-wasm.c
gboolean
mono_w32file_get_volume_information (const gunichar2 *path, gunichar2 *volumename, gint volumesize, gint *outserial, gint *maxcomp, gint *fsflags, gunichar2 *fsbuffer, gint fsbuffersize)
{
	glong len;
	gboolean status = FALSE;

	gunichar2 *ret = g_utf8_to_utf16 ("memfs", -1, NULL, &len, NULL);
	if (ret != NULL && len < fsbuffersize) {
		memcpy (fsbuffer, ret, len * sizeof (gunichar2));
		fsbuffer [len] = 0;
		status = TRUE;
	}
	if (ret != NULL)
		g_free (ret);

	return status;
}


//llvm builtin's that we should not have used in the first place


//libc / libpthread missing bits from musl or shit we didn't detect :facepalm:
int pthread_getschedparam (pthread_t thread, int *policy, struct sched_param *param)
{
	g_error ("pthread_getschedparam");
	return 0;
}

int
pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param)
{
	return 0;
}


int
pthread_attr_getstacksize (const pthread_attr_t *restrict attr, size_t *restrict stacksize)
{
	return 65536; //wasm page size
}

int
pthread_sigmask (int how, const sigset_t * restrict set, sigset_t * restrict oset)
{
	return 0;
}


int
sigsuspend(const sigset_t *sigmask)
{
	g_error ("sigsuspend");
	return 0;
}

int
getdtablesize (void)
{
	return 256; //random constant that is the fd limit
}

void *
getgrnam (const char *name)
{
	return NULL;
}

void *
getgrgid (gid_t gid)
{
	return NULL;
}

int
inotify_init (void)
{
	g_error ("inotify_init");
}

int
inotify_rm_watch (int fd, int wd)
{
	g_error ("inotify_rm_watch");
	return 0;
}

int
inotify_add_watch (int fd, const char *pathname, uint32_t mask)
{
	g_error ("inotify_add_watch");
	return 0;
}

int
sem_timedwait (sem_t *sem, const struct timespec *abs_timeout)
{
	g_error ("sem_timedwait");
	return 0;
	
}


typedef struct {
	//request data
	const char *mvid;
	int method_token;
	int il_offset;

	//bp id
	int bp_id;

	//resolved data
	MonoAssembly *assembly;
	MonoMethod *method;

	GPtrArray *children;
} BreakPointRequest;

typedef struct {
	long il_offset, native_offset;
	guint8 *ip;
	MonoJitInfo *ji;
	MonoDomain *domain;
} BreakpointInstance;


//FIXME move all of those fields to the profiler object
static gboolean debugger_enabled;
static GHashTable *mvid_to_assembly;
static int bp_id_count;
static GHashTable *bp_locs;
static GPtrArray *active_breakpoints;

static void
breakpoint_request_free (BreakPointRequest *bp)
{
	g_free ((char*)bp->mvid);
	g_free (bp);
}

static void
inplace_tolower (char *c)
{
	int i;
	for (i = strlen (c) - 1; i >= 0; --i)
		c [i] = tolower (c [i]);
}
static BreakPointRequest *
breakpoint_request_new (char *mvid, int method_token, int il_offset)
{
	//dup and lower
	mvid = g_strdup (mvid);
	inplace_tolower (mvid);

	BreakPointRequest *req = g_new0(BreakPointRequest, 1);
	req->mvid = mvid;
	req->method_token = method_token;
	req->il_offset = il_offset;

	return req;
}

static gboolean
breakpoint_matches (BreakPointRequest *bp, MonoMethod *method)
{
	if (!bp->method)
		return FALSE;
	if (method == bp->method)
		return TRUE;
	if (method->is_inflated && ((MonoMethodInflated*)method)->declaring == bp->method)
		return TRUE;
	//XXX we don't support setting a breakpoint on a specif ginst, so whatever

	return FALSE;
}
//LOCKING: loader lock must be held
static void
find_applicable_methods (BreakPointRequest *bp, GPtrArray *methods, GPtrArray *method_seq_points)
{
	GHashTableIter iter;
	MonoMethod *method;
	MonoSeqPointInfo *seq_points;

	mono_domain_lock (mono_get_root_domain ());
	g_hash_table_iter_init (&iter, domain_jit_info (mono_get_root_domain ())->seq_points);
	while (g_hash_table_iter_next (&iter, (void**)&method, (void**)&seq_points)) {
		if (breakpoint_matches (bp, method)) {
			g_ptr_array_add (methods, method);
			g_ptr_array_add (method_seq_points, seq_points);
		}
	}
	mono_domain_unlock (mono_get_root_domain ());
}

static gboolean
insert_breakpoint (MonoSeqPointInfo *seq_points, MonoDomain *domain, MonoJitInfo *ji, BreakPointRequest *bp, MonoError *error)
{
	int count;
	SeqPointIterator it;
	gboolean it_has_sp = FALSE;

	error_init (error);
	
	DEBUG_PRINTF (1, "insert_breakpoint: JI [%p] method %s at %d SP %p\n", ji, jinfo_get_method (ji)->name, bp->il_offset, seq_points);

	mono_seq_point_iterator_init (&it, seq_points);
	while (mono_seq_point_iterator_next (&it)) {
		if (it.seq_point.il_offset == bp->il_offset) {
			it_has_sp = TRUE;
			break;
		}
	}

	if (!it_has_sp) {
		/*
		 * The set of IL offsets with seq points doesn't completely match the
		 * info returned by CMD_METHOD_GET_DEBUG_INFO (#407).
		 */
		mono_seq_point_iterator_init (&it, seq_points);
		while (mono_seq_point_iterator_next (&it)) {
			if (it.seq_point.il_offset != METHOD_ENTRY_IL_OFFSET &&
				it.seq_point.il_offset != METHOD_EXIT_IL_OFFSET &&
				it.seq_point.il_offset + 1 == bp->il_offset) {
				it_has_sp = TRUE;
				break;
			}
		}
	}

	if (!it_has_sp) {
		DEBUG_PRINTF (1, "Unable to insert breakpoint at %s:%d. SeqPoint data:", mono_method_full_name (jinfo_get_method (ji), TRUE), bp->il_offset);

		mono_seq_point_iterator_init (&it, seq_points);
		while (mono_seq_point_iterator_next (&it))
			DEBUG_PRINTF (1, "\t%d\n", it.seq_point.il_offset);

		DEBUG_PRINTF (1, "End of data\n");
		mono_error_set_error (error, MONO_ERROR_GENERIC, "Failed to find the SP for the given il offset");
		return FALSE;
	}

	BreakpointInstance *inst = g_new0 (BreakpointInstance, 1);
	inst->il_offset = it.seq_point.il_offset;
	inst->native_offset = it.seq_point.native_offset;
	inst->ip = (guint8*)ji->code_start + it.seq_point.native_offset;
	inst->ji = ji;
	inst->domain = mono_get_root_domain ();

	mono_loader_lock ();

	if (!bp->children)
		bp->children = g_ptr_array_new ();
	g_ptr_array_add (bp->children, inst);

	mono_loader_unlock ();

	// dbg_lock ();
	count = GPOINTER_TO_INT (g_hash_table_lookup (bp_locs, inst->ip));
	g_hash_table_insert (bp_locs, inst->ip, GINT_TO_POINTER (count + 1));
	// dbg_unlock ();

	if (it.seq_point.native_offset == SEQ_POINT_NATIVE_OFFSET_DEAD_CODE) {
		DEBUG_PRINTF (1, "Attempting to insert seq point at dead IL offset %d, ignoring.\n", (int)bp->il_offset);
	} else if (count == 0) {
		DEBUG_PRINTF (1, "ACTIVATING BREAKPOINT in %s\n", jinfo_get_method (ji)->name);
		if (ji->is_interp) {
			mini_get_interp_callbacks ()->set_breakpoint (ji, inst->ip);
		} else {
			g_error ("no idea how to deal with compiled code");
#ifdef MONO_ARCH_SOFT_DEBUG_SUPPORTED
			mono_arch_set_breakpoint (ji, inst->ip);
#else
			NOT_IMPLEMENTED;
#endif
		}
	}

	return TRUE;
}

static gboolean
resolve_request (BreakPointRequest *bp)
{
	ERROR_DECL (error);

	MonoAssembly *assembly = bp->assembly ? bp->assembly : g_hash_table_lookup (mvid_to_assembly, bp->mvid);
	if (!assembly) {
		DEBUG_PRINTF (1, "coult not find assembly with mvid %s\n", bp->mvid);
		return FALSE;
	}
	bp->assembly = assembly;

	MonoMethod *method = bp->method ? bp->method : mono_get_method_checked (assembly->image, MONO_TOKEN_METHOD_DEF | bp->method_token, NULL, NULL, error);
	if (!method) {
		//FIXME don't swallow the error
		DEBUG_PRINTF (1, "Could not find method due to %s\n", mono_error_get_message (error));
		mono_error_cleanup (error);
		return FALSE;
	}
	bp->method = method;
	return TRUE;
}


static gboolean
set_breakpoint (MonoMethod *method, MonoSeqPointInfo *seq_points, BreakPointRequest *bp, MonoError *error)
{
	MonoJitInfo *ji = NULL;

	error_init (error);

	MonoDomain *domain = mono_get_root_domain ();
	gpointer code = mono_jit_find_compiled_method_with_jit_info (domain, method, &ji);
	if (!code) {
		/* Might be AOTed code */
		mono_class_init (method->klass);
		code = mono_aot_get_method (domain, method, error);
		if (code) {
			mono_error_assert_ok (error);
			ji = mono_jit_info_table_find (domain, code);
		} else {
			/* Might be interpreted */
			ji = mini_get_interp_callbacks ()->find_jit_info (domain, method);
		}
		g_assert (ji);
	}

	return insert_breakpoint (seq_points, domain, ji, bp, error);
}

static void
add_breakpoint (BreakPointRequest *bp)
{
	int i;
	ERROR_DECL (error);
	bp->bp_id = ++bp_id_count;

	error_init (error);

	GPtrArray *methods = g_ptr_array_new ();
	GPtrArray *method_seq_points = g_ptr_array_new ();

	if (!resolve_request (bp)) {
		mono_loader_lock ();		
	} else {
		mono_loader_lock ();

		find_applicable_methods (bp, methods, method_seq_points);

		for (i = 0; i < methods->len; ++i) {
			MonoMethod *method = (MonoMethod *)g_ptr_array_index (methods, i);
			MonoSeqPointInfo *seq_points = (MonoSeqPointInfo *)g_ptr_array_index (method_seq_points, i);

			if (!set_breakpoint (method, seq_points, bp, error)) {
				//FIXME don't swallow the error
				DEBUG_PRINTF (1, "Error setting breaking due to %s\n", mono_error_get_message (error));
				mono_error_cleanup (error);
				return;
			}
		}
	}

	g_ptr_array_add (active_breakpoints, bp);

	mono_loader_unlock ();

	g_ptr_array_free (methods, TRUE);
	g_ptr_array_free (method_seq_points, TRUE);
}

static void
add_pending_breakpoints (MonoMethod *method, MonoJitInfo *ji)
{
	int i, j;
	MonoSeqPointInfo *seq_points;
	MonoDomain *domain;
	MonoMethod *jmethod;

	if (!active_breakpoints)
		return;

	domain = mono_domain_get ();

	mono_loader_lock ();

	for (i = 0; i < active_breakpoints->len; ++i) {
		BreakPointRequest *bp = (BreakPointRequest *)g_ptr_array_index (active_breakpoints, i);
		gboolean found = FALSE;

		if (!resolve_request (bp))
			continue;

		if (!breakpoint_matches (bp, method))
			continue;

		for (j = 0; j < bp->children->len; ++j) {
			BreakpointInstance *inst = (BreakpointInstance *)g_ptr_array_index (bp->children, j);

			if (inst->ji == ji)
				found = TRUE;
		}

		if (!found) {
			ERROR_DECL (error);
			MonoMethod *declaring = NULL;

			jmethod = jinfo_get_method (ji);
			if (jmethod->is_inflated)
				declaring = mono_method_get_declaring_generic_method (jmethod);

			mono_domain_lock (domain);
			seq_points = (MonoSeqPointInfo *)g_hash_table_lookup (domain_jit_info (domain)->seq_points, jmethod);
			if (!seq_points && declaring)
				seq_points = (MonoSeqPointInfo *)g_hash_table_lookup (domain_jit_info (domain)->seq_points, declaring);
			mono_domain_unlock (domain);
			if (!seq_points) {
				/* Could be AOT code */
				continue;
			}
			g_assert (seq_points);

			if (!insert_breakpoint (seq_points, domain, ji, bp, error)) {
				DEBUG_PRINTF (1, "Failed to resolve pending BP due to %s\n", mono_error_get_message (error));
				mono_error_cleanup (error);
			}
		}
	}

	mono_loader_unlock ();
}

static void
jit_done (MonoProfiler *prof, MonoMethod *method, MonoJitInfo *jinfo)
{
	add_pending_breakpoints (method, jinfo);
}

static void
assembly_loaded (MonoProfiler *prof, MonoAssembly *assembly)
{
	char *mvid = mono_guid_to_string ((uint8_t*)assembly->image->heap_guid.data);
	inplace_tolower (mvid);

	DEBUG_PRINTF (1, "ASSEMBLY %s loaded mvid: %s\n", assembly->image->name, mvid);
	g_hash_table_insert (mvid_to_assembly, mvid, assembly);
}


void
mono_wasm_debugger_init (void)
{
	if (!debugger_enabled)
		return;

	mono_debug_init (MONO_DEBUG_FORMAT_MONO);
	mini_get_debug_options ()->gen_sdb_seq_points = TRUE;
	mini_get_debug_options ()->mdb_optimizations = TRUE;
	mono_disable_optimizations (MONO_OPT_LINEARS);
	mini_get_debug_options ()->load_aot_jit_info_eagerly = TRUE;

	MonoProfilerHandle prof = mono_profiler_create (NULL);
	mono_profiler_set_assembly_loaded_callback (prof, assembly_loaded);
	mono_profiler_set_jit_done_callback (prof, jit_done);

	mvid_to_assembly = g_hash_table_new (g_str_hash, g_str_equal);
	bp_locs = g_hash_table_new (NULL, NULL);
	active_breakpoints = g_ptr_array_new ();
}

void
mono_wasm_enable_debugging (void)
{
	DEBUG_PRINTF (1, "DEBUGGING ENABLED");
	debugger_enabled = TRUE;
}


EMSCRIPTEN_KEEPALIVE int
mono_wasm_set_breakpoint (char *mvid, int method_token, int il_offset)
{
	DEBUG_PRINTF (1, "SET BREAKPOINT: assembly %s method %x offset %x\n", mvid, method_token, il_offset);

	BreakPointRequest *req = breakpoint_request_new (mvid, method_token, il_offset);

	add_breakpoint (req);
	return req->bp_id;
}

//trampoline

void
wasm_breakpoint_hit (void)
{
	mono_wasm_fire_bp ();
}

EMSCRIPTEN_KEEPALIVE int
mono_wasm_current_bp_id (void)
{
	int i, j;

	DEBUG_PRINTF (1, "COMPUTING breapoint ID\n");
	//FIXME handle compiled case

	/* Interpreter */
	MonoLMF *lmf = mono_get_lmf ();

	g_assert (((guint64)lmf->previous_lmf) & 2);
	MonoLMFExt *ext = (MonoLMFExt*)lmf;

	g_assert (ext->interp_exit);
	MonoInterpFrameHandle *frame = ext->interp_exit_data;
	MonoJitInfo *ji = mini_get_interp_callbacks ()->frame_get_jit_info (frame);
	guint8 *ip = mini_get_interp_callbacks ()->frame_get_ip (frame);

	g_assert (ji && !ji->is_trampoline);
	MonoMethod *method = jinfo_get_method (ji);

	/* Compute the native offset of the breakpoint from the ip */
	guint32 native_offset = ip - (guint8*)ji->code_start;

	MonoSeqPointInfo *info = NULL;
	SeqPoint sp;
	gboolean found_sp = mono_find_prev_seq_point_for_native_offset (mono_domain_get (), method, native_offset, &info, &sp);
	if (!found_sp)
		DEBUG_PRINTF (1, "Could not find SP\n");

	for (i = 0; i < active_breakpoints->len; ++i) {
		BreakPointRequest *bp = (BreakPointRequest *)g_ptr_array_index (active_breakpoints, i);

		if (!bp->method)
			continue;

		for (j = 0; j < bp->children->len; ++j) {
			BreakpointInstance *inst = (BreakpointInstance *)g_ptr_array_index (bp->children, j);
			if (inst->ji == ji && inst->il_offset == sp.il_offset && inst->native_offset == sp.native_offset) {
				DEBUG_PRINTF (1, "FOUND BREAKPOINT idx %d ID %d\n", i, bp->bp_id);
				return bp->bp_id;
			}
		}
	}
	DEBUG_PRINTF (1, "BP NOT FOUND for method %s JI %p il_offset %d\n", method->name, ji, sp.il_offset);

	return -1;
}

static gboolean
list_frames (MonoStackFrameInfo *info, MonoContext *ctx, gpointer data)
{
	SeqPoint sp;
	MonoMethod *method;

	if (info->ji)
		method = jinfo_get_method (info->ji);
	else
		method = info->method;

	if (!method)
		return FALSE;

	DEBUG_PRINTF (2, "Reporting method %s (%p %d)\n", method->name, mono_get_root_domain (), info->native_offset);

	if (!mono_find_prev_seq_point_for_native_offset (mono_get_root_domain (), method, info->native_offset, NULL, &sp))
		DEBUG_PRINTF (1, "FAILED TO LOOKUP SEQ POINT\n");

	while (method->is_inflated)
		method = ((MonoMethodInflated*)method)->declaring;

	char *mvid = mono_guid_to_string ((uint8_t*)method->klass->image->heap_guid.data);
	inplace_tolower (mvid);

	DEBUG_PRINTF (2, "adding off %d token %d mvid %s\n", sp.il_offset, mono_metadata_token_index (method->token), mvid);
	if (method->wrapper_type == MONO_WRAPPER_NONE)
		mono_wasm_add_frame (sp.il_offset, mono_metadata_token_index (method->token), mvid);

	g_free (mvid);

	return FALSE;
}

EMSCRIPTEN_KEEPALIVE void
mono_wasm_enum_frames (void)
{
	mono_get_eh_callbacks ()->mono_walk_stack_with_ctx (list_frames, NULL, MONO_UNWIND_NONE, NULL);
}
