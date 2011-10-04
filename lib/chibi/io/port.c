
#include <stdio.h>
#include <chibi/eval.h>

#define SEXP_PORT_BUFFER_SIZE 1024
#define SEXP_LAST_CONTEXT_CHECK_LIMIT 256

#define sexp_cookie_ctx(vec) sexp_vector_ref((sexp)vec, SEXP_ZERO)
#define sexp_cookie_buffer(vec) sexp_vector_ref((sexp)vec, SEXP_ONE)
#define sexp_cookie_read(vec) sexp_vector_ref((sexp)vec, SEXP_TWO)
#define sexp_cookie_write(vec) sexp_vector_ref((sexp)vec, SEXP_THREE)
#define sexp_cookie_seek(vec) sexp_vector_ref((sexp)vec, SEXP_FOUR)
#define sexp_cookie_close(vec) sexp_vector_ref((sexp)vec, SEXP_FIVE)

#define sexp_cookie_ctx_set(vec, x) sexp_vector_set((sexp)vec, SEXP_ZERO, x)
#define sexp_cookie_buffer_set(vec, x) sexp_vector_set((sexp)vec, SEXP_ONE, x)
#define sexp_cookie_read_set(vec, x) sexp_vector_set((sexp)vec, SEXP_TWO, x)
#define sexp_cookie_write_set(vec, x) sexp_vector_set((sexp)vec, SEXP_THREE, x)
#define sexp_cookie_seek_set(vec, x) sexp_vector_set((sexp)vec, SEXP_FOUR, x)
#define sexp_cookie_close_set(vec, x) sexp_vector_set((sexp)vec, SEXP_FIVE, x)

#if ! SEXP_USE_BOEHM
static int in_heap_p (sexp_heap h, sexp p) {
  for ( ; h; h = h->next)
    if (((sexp)h < p) && (p < (sexp)((char*)h + h->size)))
      return 1;
  return 0;
}
#endif

static sexp sexp_last_context (sexp ctx, sexp *cstack) {
  sexp res=SEXP_FALSE;
#if ! SEXP_USE_BOEHM
  sexp p;
  sexp_sint_t i;
  sexp_heap h = sexp_context_heap(ctx);
  for (i=0; i<SEXP_LAST_CONTEXT_CHECK_LIMIT; i++) {
    p = cstack[i];
    if (p && (p != ctx) && sexp_pointerp(p) && in_heap_p(h, p)
        && (sexp_pointer_tag(p) == SEXP_CONTEXT)
        && (sexp_context_heap(p) == h)) {
      res = p;
      break;
    }
  }
#endif
  return res;
}

#if SEXP_BSD
static int sexp_cookie_reader (void *cookie, char *buffer, int size)
#else
static ssize_t sexp_cookie_reader (void *cookie, char *buffer, size_t size)
#endif
{
  sexp vec = (sexp)cookie, ctx, res;
  if (! sexp_procedurep(sexp_cookie_read(vec))) return -1;
  sexp_gc_var2(ctx2, args);
  ctx = sexp_cookie_ctx(vec);
  ctx2 = sexp_last_context(ctx, (sexp*)&cookie);
  sexp_gc_preserve2(ctx, ctx2, args);
  if (size > sexp_string_length(sexp_cookie_buffer(vec)))
    sexp_cookie_buffer_set(vec, sexp_make_string(ctx, sexp_make_fixnum(size), SEXP_VOID));
  args = sexp_list2(ctx, sexp_cookie_buffer(vec), sexp_make_fixnum(size));
  res = sexp_apply(ctx, sexp_cookie_read(vec), args);
  sexp_gc_release2(ctx);
  if (sexp_fixnump(res)) {
    memcpy(buffer, sexp_string_data(sexp_cookie_buffer(vec)), sexp_unbox_fixnum(res));
    return sexp_unbox_fixnum(res);
  } else {
    return -1;
  }
}

#if SEXP_BSD
static int sexp_cookie_writer (void *cookie, const char *buffer, int size)
#else
static ssize_t sexp_cookie_writer (void *cookie, const char *buffer, size_t size)
#endif
{
  sexp vec = (sexp)cookie, ctx, res;
  if (! sexp_procedurep(sexp_cookie_write(vec))) return -1;
  sexp_gc_var2(ctx2, args);
  ctx = sexp_cookie_ctx(vec);
  ctx2 = sexp_last_context(ctx, (sexp*)&cookie);
  sexp_gc_preserve2(ctx, ctx2, args);
  if (size > sexp_string_length(sexp_cookie_buffer(vec)))
    sexp_cookie_buffer_set(vec, sexp_make_string(ctx, sexp_make_fixnum(size), SEXP_VOID));
  memcpy(sexp_string_data(sexp_cookie_buffer(vec)), buffer, size);
  args = sexp_list2(ctx, sexp_cookie_buffer(vec), sexp_make_fixnum(size));
  res = sexp_apply(ctx, sexp_cookie_write(vec), args);
  sexp_gc_release2(ctx);
  return (sexp_fixnump(res) ? sexp_unbox_fixnum(res) : -1);
}

#if ! SEXP_BSD

#ifdef __CYGWIN__
#define off64_t off_t
#endif

static int sexp_cookie_seeker (void *cookie, off64_t *position, int whence) {
  sexp vec = (sexp)cookie, ctx, res;
  if (! sexp_procedurep(sexp_cookie_seek(vec))) return -1;
  sexp_gc_var2(ctx2, args);
  ctx = sexp_cookie_ctx(vec);
  ctx2 = sexp_last_context(ctx, (sexp*)&cookie);
  sexp_gc_preserve2(ctx, ctx2, args);
  args = sexp_make_integer(ctx, *position);
  args = sexp_list2(ctx, args, sexp_make_fixnum(whence));
  res = sexp_apply(ctx, sexp_cookie_seek(vec), args);
  if (sexp_fixnump(res))
    *position = sexp_unbox_fixnum(res);
  sexp_gc_release2(ctx);
  return sexp_fixnump(res);
}
#endif

static int sexp_cookie_cleaner (void *cookie) {
  sexp vec = (sexp)cookie, ctx, res;
  if (! sexp_procedurep(sexp_cookie_close(vec))) return 0;
  ctx = sexp_cookie_ctx(vec);
  res = sexp_apply(ctx, sexp_cookie_close(vec), SEXP_NULL);
  return (sexp_exceptionp(res) ? -1 : sexp_truep(res));
}

#if ! SEXP_BSD

static cookie_io_functions_t sexp_cookie = {
  .read  = (cookie_read_function_t*)sexp_cookie_reader,
  .write = (cookie_write_function_t*)sexp_cookie_writer,
  .seek  = (cookie_seek_function_t*)sexp_cookie_seeker,
  .close = (cookie_close_function_t*)sexp_cookie_cleaner,
};

static cookie_io_functions_t sexp_cookie_no_seek = {
  .read  = (cookie_read_function_t*)sexp_cookie_reader,
  .write = (cookie_write_function_t*)sexp_cookie_writer,
  .seek  = NULL,
  .close = (cookie_close_function_t*)sexp_cookie_cleaner,
};

#endif

#if SEXP_USE_STRING_STREAMS

static sexp sexp_make_custom_port (sexp ctx, sexp self, char *mode,
                                   sexp read, sexp write,
                                   sexp seek, sexp close) {
  FILE *in;
  sexp res;
  sexp_gc_var1(vec);
  if (sexp_truep(read) && ! sexp_procedurep(read))
    return sexp_type_exception(ctx, self, SEXP_PROCEDURE, read);
  if (sexp_truep(write) && ! sexp_procedurep(write))
    return sexp_type_exception(ctx, self, SEXP_PROCEDURE, write);
  if (sexp_truep(seek) && ! sexp_procedurep(seek))
    return sexp_type_exception(ctx, self, SEXP_PROCEDURE, seek);
  if (sexp_truep(close) && ! sexp_procedurep(close))
    return sexp_type_exception(ctx, self, SEXP_PROCEDURE, close);
  sexp_gc_preserve1(ctx, vec);
  vec = sexp_make_vector(ctx, SEXP_SIX, SEXP_VOID);
  sexp_cookie_ctx_set(vec, ctx);
  sexp_cookie_buffer_set(vec, sexp_make_string(ctx, sexp_make_fixnum(SEXP_PORT_BUFFER_SIZE), SEXP_VOID));
  sexp_cookie_read_set(vec, read);
  sexp_cookie_write_set(vec, write);
  sexp_cookie_seek_set(vec, seek);
  sexp_cookie_close_set(vec, close);
#if SEXP_BSD
  in = funopen(vec,
               (sexp_procedurep(read) ? sexp_cookie_reader : NULL),
               (sexp_procedurep(write) ? sexp_cookie_writer : NULL),
               NULL, /* (sexp_procedurep(seek) ? sexp_cookie_reader : NULL), */
               (sexp_procedurep(close) ? sexp_cookie_cleaner : NULL));
#else
  in = fopencookie(vec, mode, (sexp_truep(seek) ? sexp_cookie : sexp_cookie_no_seek));
#endif
  if (! in) {
    res = sexp_user_exception(ctx, self, "couldn't make custom port", read);
  } else {
    res = sexp_make_input_port(ctx, in, SEXP_FALSE);
    sexp_port_cookie(res) = vec;  /* for gc preserving */
  }
  sexp_gc_release1(ctx);
  return res;
}

#else

static sexp sexp_make_custom_port (sexp ctx, sexp self,
                                   char *mode, sexp read, sexp write,
                                   sexp seek, sexp close) {
  return sexp_user_exception(ctx, self, "custom ports not supported in this configuration", SEXP_NULL);
}

#endif

static sexp sexp_make_custom_input_port (sexp ctx, sexp self,
                                         sexp read, sexp seek, sexp close) {
  return sexp_make_custom_port(ctx, self, "r", read, SEXP_FALSE, seek, close);
}

static sexp sexp_make_custom_output_port (sexp ctx, sexp self,
                                          sexp write, sexp seek, sexp close) {
  sexp res = sexp_make_custom_port(ctx, self, "w", SEXP_FALSE, write, seek, close);
  sexp_pointer_tag(res) = SEXP_OPORT;
  return res;
}

sexp sexp_bytes_to_string (sexp ctx, sexp vec) {
  sexp res;
#if SEXP_USE_PACKED_STRINGS
  res = sexp_c_string(ctx, sexp_bytes_data(vec), sexp_bytes_length(vec));
#else
  res = sexp_alloc_type(ctx, string, SEXP_STRING);
  sexp_string_bytes(res) = vec;
  sexp_string_offset(res) = 0;
  sexp_string_length(res) = sexp_bytes_length(vec);
#endif
  return res;
}

sexp sexp_open_input_bytevector (sexp ctx, sexp self, sexp vec) {
  sexp_gc_var2(str, res);
  sexp_assert_type(ctx, sexp_bytesp, SEXP_BYTES, vec);
  sexp_gc_preserve2(ctx, str, res);
  str = sexp_bytes_to_string(ctx, vec);
  res = sexp_make_input_string_port(ctx, str);
  sexp_port_binaryp(res) = 1;
  sexp_gc_release2(ctx);
  return res;
}

sexp sexp_open_output_bytevector (sexp ctx, sexp self) {
  sexp res = sexp_make_output_string_port(ctx);
  sexp_port_binaryp(res) = 1;
  return res;
}

sexp sexp_get_output_bytevector (sexp ctx, sexp self, sexp port) {
  sexp_gc_var1(res);
  sexp_assert_type(ctx, sexp_oportp, SEXP_OPORT, port);
  if (!sexp_port_binaryp(port))
    return sexp_xtype_exception(ctx, self, "not a binary port", port);
  sexp_gc_preserve1(ctx, res);
  res = sexp_get_output_string(ctx, port);
  res = sexp_string_to_bytes(ctx, res);
  sexp_gc_release1(ctx);
  return res;
}

sexp sexp_write_u8 (sexp ctx, sexp self, sexp u8, sexp out) {
  sexp_assert_type(ctx, sexp_fixnump, SEXP_FIXNUM, u8);
  if (sexp_unbox_fixnum(u8) < 0 || sexp_unbox_fixnum(u8) > 255)
    return sexp_xtype_exception(ctx, self, "not a u8 value", u8);
  sexp_assert_type(ctx, sexp_oportp, SEXP_OPORT, out);
  if (!sexp_port_binaryp(out))
    return sexp_xtype_exception(ctx, self, "not a binary port", out);
  sexp_write_char(ctx, sexp_unbox_fixnum(u8), out);
  return SEXP_VOID;
}

sexp sexp_read_u8 (sexp ctx, sexp self, sexp in) {
  int c;
  sexp_assert_type(ctx, sexp_iportp, SEXP_IPORT, in);
  if (!sexp_port_binaryp(in))
    return sexp_xtype_exception(ctx, self, "not a binary port", in);
  sexp_check_block_port(ctx, in, 0);
  c = sexp_read_char(ctx, in);
  return c == EOF ? SEXP_EOF : sexp_make_fixnum(c);
}

sexp sexp_peek_u8 (sexp ctx, sexp self, sexp in) {
  int c;
  sexp_assert_type(ctx, sexp_iportp, SEXP_IPORT, in);
  if (!sexp_port_binaryp(in))
    return sexp_xtype_exception(ctx, self, "not a binary port", in);
  sexp_check_block_port(ctx, in, 0);
  c = sexp_read_char(ctx, in);
  if (c == EOF)
    return SEXP_EOF;
  sexp_push_char(ctx, c, in);
  return sexp_make_fixnum(c);
}
