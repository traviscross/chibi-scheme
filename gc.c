/*  gc.c -- simple mark&sweep garbage collector               */
/*  Copyright (c) 2009-2011 Alex Shinn.  All rights reserved. */
/*  BSD-style license: http://synthcode.com/license.txt       */

#include "chibi/sexp.h"

#if SEXP_USE_MMAP_GC
#include <sys/mman.h>
#endif

#define SEXP_BANNER(x) ("**************** GC "x"\n")

#define SEXP_MINIMUM_OBJECT_SIZE (sexp_heap_align(1))

#if SEXP_USE_GLOBAL_HEAP
sexp_heap sexp_global_heap;
#endif

#if SEXP_USE_CONSERVATIVE_GC
static sexp* stack_base;
#endif

static sexp_heap sexp_heap_last (sexp_heap h) {
  while (h->next) h = h->next;
  return h;
}

static size_t sexp_heap_total_size (sexp_heap h) {
  size_t total_size = 0;
  for (; h; h=h->next)
    total_size += h->size;
  return total_size;
}

sexp_uint_t sexp_allocated_bytes (sexp ctx, sexp x) {
  sexp_uint_t res;
  sexp t;
  if ((! sexp_pointerp(x)) || (sexp_pointer_tag(x) >= sexp_context_num_types(ctx)))
    return sexp_heap_align(1);
  t = sexp_object_type(ctx, x);
  res = sexp_type_size_of_object(t, x);
  return res;
}

#if SEXP_USE_SAFE_GC_MARK
static int sexp_in_heap_p(sexp ctx, sexp_uint_t x) {
  sexp_heap h;
  if (x & (sexp_heap_align(1)-1)) {
    fprintf(stderr, SEXP_BANNER("invalid heap alignment: %p %d"),
            (sexp)x, sexp_pointer_tag((sexp)x));
    return 0;
  }
  for (h=sexp_context_heap(ctx); h; h=h->next)
    if (((sexp_uint_t)h < x) && (x < (sexp_uint_t)(h->data + h->size)))
      return 1;
  fprintf(stderr, SEXP_BANNER("invalid object outside heap: %p %d"),
          (sexp)x, sexp_pointer_tag((sexp)x));
  return 0;
}
#endif

void sexp_mark (sexp ctx, sexp x) {
  sexp_sint_t i, len;
  sexp t, *p;
  struct sexp_gc_var_t *saves;
 loop:
  if ((! x) || (! sexp_pointerp(x)) || sexp_gc_mark(x))
    return;
#if SEXP_USE_SAFE_GC_MARK
  if (! sexp_in_heap_p(ctx, (sexp_uint_t)x))
    return;
#endif
#if SEXP_USE_HEADER_MAGIC
  if (sexp_pointer_magic(x) != SEXP_POINTER_MAGIC && sexp_pointer_tag(x) != SEXP_TYPE
      && sexp_pointer_tag(x) != SEXP_OPCODE && sexp_pointer_tag(x) != SEXP_CORE
      && sexp_pointer_tag(x) != SEXP_STACK)
    return;
#endif
  sexp_gc_mark(x) = 1;
  if (sexp_contextp(x))
    for (saves=sexp_context_saves(x); saves; saves=saves->next)
      if (saves->var) sexp_mark(ctx, *(saves->var));
  t = sexp_object_type(ctx, x);
  p = (sexp*) (((char*)x) + sexp_type_field_base(t));
  len = sexp_type_num_slots_of_object(t, x) - 1;
  if (len >= 0) {
    for (i=0; i<len; i++)
      sexp_mark(ctx, p[i]);
    x = p[len];
    goto loop;
  }
}

#if SEXP_USE_CONSERVATIVE_GC

int stack_references_pointer_p (sexp ctx, sexp x) {
  sexp *p;
  for (p=(&x)+1; p<stack_base; p++)
    if (*p == x)
      return 1;
  return 0;
}

void sexp_conservative_mark (sexp ctx) {
  sexp_heap h = sexp_context_heap(ctx);
  sexp p, end;
  sexp_free_list q, r;
  for ( ; h; h=h->next) {   /* just scan the whole heap */
    p = (sexp) (h->data + sexp_heap_align(sexp_sizeof(pair)));
    q = h->free_list;
    end = (sexp) ((char*)h->data + h->size - sexp_heap_align(sexp_sizeof(pair)));
    while (p < end) {
      for (r=q->next; r && ((char*)r<(char*)p); q=r, r=r->next)
        ;
      if ((char*)r == (char*)p) {
        p = (sexp) (((char*)p) + r->size);
        continue;
      }
      if (! sexp_gc_mark(p) && stack_references_pointer_p(ctx, p)) {
#if SEXP_USE_DEBUG_GC > 3
        if (p && sexp_pointerp(p)) {
          fprintf(stderr, SEXP_BANNER("MISS: %p: %s"), p,sexp_pointer_source(p));
          fflush(stderr);
        }
#endif
#if SEXP_USE_CONSERVATIVE_GC_PRESERVE_TAG
        if (sexp_pointer_tag(p) == SEXP_USE_CONSERVATIVE_GC_PRESERVE_TAG)
#endif
        sexp_mark(ctx, p);
      }
      p = (sexp) (((char*)p)+sexp_heap_align(sexp_allocated_bytes(ctx, p)));
    }
  }
}

#else
#define sexp_conservative_mark(ctx)
#endif

#if SEXP_USE_WEAK_REFERENCES
void sexp_reset_weak_references(sexp ctx) {
  int i, len, all_reset_p;
  sexp_heap h = sexp_context_heap(ctx);
  sexp p, t, end, *v;
  sexp_free_list q, r;
  for ( ; h; h=h->next) {   /* just scan the whole heap */
    p = (sexp) (h->data + sexp_heap_align(sexp_sizeof(pair)));
    q = h->free_list;
    end = (sexp) ((char*)h->data + h->size - sexp_heap_align(sexp_sizeof(pair)));
    while (p < end) {
      /* find the preceding and succeeding free list pointers */
      for (r=q->next; r && ((char*)r<(char*)p); q=r, r=r->next)
        ;
      if ((char*)r == (char*)p) { /* this is a free block, skip it */
        p = (sexp) (((char*)p) + r->size);
        continue;
      }
      if (sexp_gc_mark(p)) {
        t = sexp_object_type(ctx, p);
        if (sexp_type_weak_base(t) > 0) {
          all_reset_p = 1;
          v = (sexp*) ((char*)p + sexp_type_weak_base(t));
          len = sexp_type_num_weak_slots_of_object(t, p);
          for (i=0; i<len; i++) {
            if (v[i] && sexp_pointerp(v[i]) && ! sexp_gc_mark(v[i])) {
              v[i] = SEXP_FALSE;
              sexp_brokenp(p) = 1;
            } else {
              all_reset_p = 0;
            }
          }
          if (all_reset_p) {      /* ephemerons */
            len += sexp_type_weak_len_extra(t);
            for ( ; i<len; i++) v[i] = SEXP_FALSE;
          }
        }
      }
      p = (sexp) (((char*)p)+sexp_heap_align(sexp_allocated_bytes(ctx, p)));
    }
  }
}
#endif

sexp sexp_sweep (sexp ctx, size_t *sum_freed_ptr) {
  size_t freed, max_freed=0, sum_freed=0, size;
  sexp_heap h = sexp_context_heap(ctx);
  sexp p, end;
  sexp_free_list q, r, s;
  sexp_proc2 finalizer;
  /* scan over the whole heap */
  for ( ; h; h=h->next) {
    p = (sexp) (h->data + sexp_heap_align(sexp_sizeof(pair)));
    q = h->free_list;
    end = (sexp) ((char*)h->data + h->size - sexp_heap_align(sexp_sizeof(pair)));
    while (p < end) {
      /* find the preceding and succeeding free list pointers */
      for (r=q->next; r && ((char*)r<(char*)p); q=r, r=r->next)
        ;
#if SEXP_USE_HEADER_MAGIC
      if (sexp_pointer_magic(p) != SEXP_POINTER_MAGIC)
        fprintf(stderr, SEXP_BANNER("%p (heap: %p): bad magic at %p: %p"),
                ctx, sexp_context_heap(ctx), p, sexp_pointer_magic(p));
#endif
#if SEXP_USE_DEBUG_GC > 1
      if (sexp_pointer_tag(p) <= 0
          || sexp_pointer_tag(p) > sexp_context_num_types(ctx))
        fprintf(stderr, SEXP_BANNER("%p (heap: %p): bad object at %p: tag: %d"),
                ctx, sexp_context_heap(ctx), p, sexp_pointer_tag(p));
#endif
      if ((char*)r == (char*)p) { /* this is a free block, skip it */
        p = (sexp) (((char*)p) + r->size);
        continue;
      }
      size = sexp_heap_align(sexp_allocated_bytes(ctx, p));
#if SEXP_USE_DEBUG_GC
      if (r && ((char*)p)+size > (char*)r)
        fprintf(stderr, SEXP_BANNER("%p (heap: %p): bad size at %p + %d > %p"),
                ctx, sexp_context_heap(ctx), p, sexp_pointer_tag(p), r);
#endif
      if (! sexp_gc_mark(p)) {
        /* free p */
        finalizer = sexp_type_finalize(sexp_object_type(ctx, p));
        if (finalizer) finalizer(ctx sexp_api_pass(NULL, 1), p);
        sum_freed += size;
        if (((((char*)q) + q->size) == (char*)p) && (q != h->free_list)) {
          /* merge q with p */
          if (r && ((((char*)p)+size) == (char*)r)) {
            /* ... and with r */
            q->next = r->next;
            freed = q->size + size + r->size;
            p = (sexp) (((char*)p) + size + r->size);
          } else {
            freed = q->size + size;
            p = (sexp) (((char*)p)+size);
          }
          q->size = freed;
        } else {
          s = (sexp_free_list)p;
          if (r && ((((char*)p)+size) == (char*)r)) {
            /* merge p with r */
            s->size = size + r->size;
            s->next = r->next;
            q->next = s;
            freed = size + r->size;
          } else {
            s->size = size;
            s->next = r;
            q->next = s;
            freed = size;
          }
          p = (sexp) (((char*)p)+freed);
        }
        if (freed > max_freed)
          max_freed = freed;
      } else {
        sexp_gc_mark(p) = 0;
        p = (sexp) (((char*)p)+size);
      }
    }
  }
  if (sum_freed_ptr) *sum_freed_ptr = sum_freed;
  return sexp_make_fixnum(max_freed);
}

sexp sexp_gc (sexp ctx, size_t *sum_freed) {
  sexp res;
#if SEXP_USE_GLOBAL_SYMBOLS
  int i;
  for (i=0; i<SEXP_SYMBOL_TABLE_SIZE; i++)
    sexp_mark(ctx, sexp_symbol_table[i]);
#endif
  sexp_mark(ctx, ctx);
  sexp_conservative_mark(ctx);
#if SEXP_USE_DEBUG_GC
  fprintf(stderr, SEXP_BANNER("%p (heap: %p size: %lu)"), ctx,
          sexp_context_heap(ctx), sexp_heap_total_size(sexp_context_heap(ctx)));
#endif
#if SEXP_USE_WEAK_REFERENCES
  sexp_reset_weak_references(ctx);
#endif
  res = sexp_sweep(ctx, sum_freed);
#if SEXP_USE_DEBUG_GC
  fprintf(stderr, SEXP_BANNER("%p (freed: %lu max_freed: %lu)"),
          ctx, *sum_freed, sexp_unbox_fixnum(res));
#endif
  return res;
}

sexp_heap sexp_make_heap (size_t size, size_t max_size) {
  sexp_free_list free, next;
  sexp_heap h;
#if SEXP_USE_MMAP_GC
  h =  mmap(NULL, sexp_heap_pad_size(size), PROT_READ|PROT_WRITE|PROT_EXEC,
            MAP_ANON|MAP_PRIVATE, 0, 0);
#else
  h =  malloc(sexp_heap_pad_size(size));
#endif
  if (! h) return NULL;
  h->size = size;
  h->max_size = max_size;
  h->data = (char*) sexp_heap_align(sizeof(h->data)+(sexp_uint_t)&(h->data));
  free = h->free_list = (sexp_free_list) h->data;
  h->next = NULL;
  next = (sexp_free_list) (((char*)free) + sexp_heap_align(sexp_sizeof(pair)));
  free->size = 0; /* actually sexp_sizeof(pair) */
  free->next = next;
  next->size = size - sexp_heap_align(sexp_sizeof(pair));
  next->next = NULL;
  return h;
}

int sexp_grow_heap (sexp ctx, size_t size) {
  size_t cur_size, new_size;
  sexp_heap h = sexp_heap_last(sexp_context_heap(ctx));
  cur_size = h->size;
  new_size = sexp_heap_align(((cur_size > size) ? cur_size : size) * 2);
  h->next = sexp_make_heap(new_size, h->max_size);
  return (h->next != NULL);
}

void* sexp_try_alloc (sexp ctx, size_t size) {
  sexp_free_list ls1, ls2, ls3;
  sexp_heap h;
  for (h=sexp_context_heap(ctx); h; h=h->next)
    for (ls1=h->free_list, ls2=ls1->next; ls2; ls1=ls2, ls2=ls2->next)
      if (ls2->size >= size) {
        if (ls2->size >= (size + SEXP_MINIMUM_OBJECT_SIZE)) {
          ls3 = (sexp_free_list) (((char*)ls2)+size); /* the tail after ls2 */
          ls3->size = ls2->size - size;
          ls3->next = ls2->next;
          ls1->next = ls3;
        } else {                  /* take the whole chunk */
          ls1->next = ls2->next;
        }
        memset((void*)ls2, 0, size);
        return ls2;
      }
  return NULL;
}

void* sexp_alloc (sexp ctx, size_t size) {
  void *res;
  size_t max_freed, sum_freed, total_size;
  sexp_heap h;
  size = sexp_heap_align(size);
  res = sexp_try_alloc(ctx, size);
  if (! res) {
    max_freed = sexp_unbox_fixnum(sexp_gc(ctx, &sum_freed));
    total_size = sexp_heap_total_size(sexp_context_heap(ctx));
    if (((max_freed < size)
         || ((total_size > sum_freed)
             && (total_size - sum_freed) > (total_size*SEXP_GROW_HEAP_RATIO)))
        && ((!h->max_size) || (total_size < h->max_size)))
      sexp_grow_heap(ctx, size);
    res = sexp_try_alloc(ctx, size);
    if (! res)
      res = sexp_global(ctx, SEXP_G_OOM_ERROR);
  }
  return res;
}

#if ! SEXP_USE_GLOBAL_HEAP

sexp sexp_copy_context (sexp ctx, sexp dst, sexp flags) {
  sexp_sint_t i, off, len, freep;
  sexp_heap to, from = sexp_context_heap(ctx);
  sexp_free_list q;
  sexp p, p2, t, end, *v;
  freep = sexp_unbox_fixnum(flags) & sexp_unbox_fixnum(SEXP_COPY_FREEP);

  /* validate input, creating a new heap if needed */
  if (from->next) {
    return sexp_user_exception(ctx, NULL, "can't copy a non-contiguous heap", ctx);
  } else if (! dst || sexp_not(dst)) {
    to = sexp_make_heap(from->size, from->max_size);
    dst = (sexp) ((char*)ctx + ((char*)to - (char*)from));
  } else if (! sexp_contextp(dst)) {
    return sexp_type_exception(ctx, NULL, SEXP_CONTEXT, dst);
  } else if (sexp_context_heap(dst)->size < from->size) {
    return sexp_user_exception(ctx, NULL, "destination context too small", dst);
  } else {
    to = sexp_context_heap(dst);
  }

  /* copy the raw data */
  off = (char*)to - (char*)from;
  memcpy(to, from, sexp_heap_pad_size(from->size));
  to->free_list = (sexp_free_list) ((char*)to->free_list + off);
  to->data += off;
  end = (sexp) (from->data + from->size);

  /* adjust the free list */
  for (q=to->free_list; q->next; q=q->next)
    q->next = (sexp_free_list) ((char*)q->next + off);

  /* adjust if the destination is larger */
  if (from->size < to->size) {
    if (((char*)q + q->size - off) >= (char*)end) {
      q->size += (to->size - from->size);
    } else {
      q->next = (sexp_free_list) ((char*)end + off);
      q->next->next = NULL;
      q->next->size = (to->size - from->size);
    }
  }

  /* adjust data by traversing over the _original_ heap */
  p = (sexp) (from->data + sexp_heap_align(sexp_sizeof(pair)));
  q = from->free_list;
  while (p < end) {
    /* find the next free list pointer */
    for ( ; q && ((char*)q < (char*)p); q=q->next)
      ;
    if ((char*)q == (char*)p) { /* this is a free block, skip it */
      p = (sexp) (((char*)p) + q->size);
    } else {
      t = sexp_object_type(ctx, p);
      len = sexp_type_num_slots_of_object(t, p);
      p2 = (sexp)((char*)p + off);
      v = (sexp*) ((char*)p2 + sexp_type_field_base(t));
      /* offset any pointers in the _destination_ heap */
      for (i=0; i<len; i++)
        if (v[i] && sexp_pointerp(v[i]))
          v[i] = (sexp) ((char*)v[i] + off);
      /* don't free unless specified - only the original cleans up */
      if (! freep)
        sexp_freep(p2) = 0;
      /* adjust context heaps, don't copy saved sexp_gc_vars */
      if (sexp_contextp(p2)) {
        sexp_context_saves(p2) = NULL;
        if (sexp_context_heap(p2) == from)
          sexp_context_heap(p2) = to;
      }
      p = (sexp) (((char*)p)+sexp_heap_align(sexp_allocated_bytes(ctx, p)));
    }
  }

  return dst;
}

#endif

void sexp_gc_init (void) {
#if SEXP_USE_GLOBAL_HEAP || SEXP_USE_CONSERVATIVE_GC
  sexp_uint_t size = sexp_heap_align(SEXP_INITIAL_HEAP_SIZE);
#endif
#if SEXP_USE_GLOBAL_HEAP
  sexp_global_heap = sexp_make_heap(size, SEXP_MAXIMUM_HEAP_SIZE);
#endif
#if SEXP_USE_CONSERVATIVE_GC
  /* the +32 is a hack, but this is just for debugging anyway */
  stack_base = ((sexp*)&size) + 32;
#endif
}
