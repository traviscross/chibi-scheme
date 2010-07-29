/*  ast.c -- interface to the Abstract Syntax Tree            */
/*  Copyright (c) 2009-2010 Alex Shinn.  All rights reserved. */
/*  BSD-style license: http://synthcode.com/license.txt       */

#include <chibi/eval.h>

static void sexp_define_type_predicate (sexp ctx, sexp env, char *cname, sexp_uint_t type) {
  sexp_gc_var2(name, op);
  sexp_gc_preserve2(ctx, name, op);
  name = sexp_c_string(ctx, cname, -1);
  op = sexp_make_type_predicate(ctx, name, sexp_make_fixnum(type));
  sexp_env_define(ctx, env, name=sexp_intern(ctx, cname, -1), op);
  sexp_gc_release2(ctx);
}

static void sexp_define_accessors (sexp ctx, sexp env, sexp_uint_t ctype,
                                   sexp_uint_t cindex, char* get, char *set) {
  sexp type, index;
  sexp_gc_var2(name, op);
  sexp_gc_preserve2(ctx, name, op);
  type = sexp_make_fixnum(ctype);
  index = sexp_make_fixnum(cindex);
  op = sexp_make_getter(ctx, name=sexp_c_string(ctx, get, -1), type, index);
  sexp_env_define(ctx, env, name=sexp_intern(ctx, get, -1), op);
  op = sexp_make_setter(ctx, name=sexp_c_string(ctx, set, -1), type, index);
  sexp_env_define(ctx, env, name=sexp_intern(ctx, set, -1), op);
  sexp_gc_release2(ctx);
}

static sexp sexp_get_env_cell (sexp ctx sexp_api_params(self, n), sexp env, sexp id) {
  sexp cell = sexp_env_cell(env, id);
  while ((! cell) && sexp_synclop(id)) {
    env = sexp_synclo_env(id);
    id = sexp_synclo_expr(id);
  }
  return cell ? cell : SEXP_FALSE;
}

static sexp sexp_get_opcode_name (sexp ctx sexp_api_params(self, n), sexp op) {
  if (! sexp_opcodep(op))
    return sexp_type_exception(ctx, self, SEXP_OPCODE, op);
  else if (! sexp_opcode_name(op))
    return SEXP_FALSE;
  else
    return sexp_intern(ctx, sexp_opcode_name(op), -1);
}

static sexp sexp_translate_opcode_type (sexp ctx, sexp type) {
  sexp_gc_var2(res, tmp);
  res = type;
  if (! res) {
    res = sexp_type_by_index(ctx, SEXP_OBJECT);
  } if (sexp_fixnump(res)) {
    res = sexp_type_by_index(ctx, sexp_unbox_fixnum(res));
  } else if (sexp_nullp(res)) {        /* opcode list types */
    sexp_gc_preserve2(ctx, res, tmp);
    tmp = sexp_intern(ctx, "or", -1);
    res = sexp_cons(ctx, sexp_make_fixnum(SEXP_PAIR), SEXP_NULL);
    res = sexp_cons(ctx, SEXP_NULL, res);
    res = sexp_cons(ctx, tmp, res);
    sexp_gc_release2(ctx);
  }
  return res;
}

static sexp sexp_get_opcode_ret_type (sexp ctx sexp_api_params(self, n), sexp op) {
  sexp res;
  if (! sexp_opcodep(op))
    return sexp_type_exception(ctx, self, SEXP_OPCODE, op);
  res = sexp_opcode_return_type(op);
  if (sexp_fixnump(res))
    res = sexp_type_by_index(ctx, sexp_unbox_fixnum(res));
  return sexp_translate_opcode_type(ctx, res);
}

static sexp sexp_get_opcode_param_type (sexp ctx sexp_api_params(self, n), sexp op, sexp k) {
  sexp res;
  if (! sexp_opcodep(op))
    return sexp_type_exception(ctx, self, SEXP_OPCODE, op);
  else if (! sexp_fixnump(k))
    return sexp_type_exception(ctx, self, SEXP_FIXNUM, k);
  switch (sexp_unbox_fixnum(k)) {
  case 0:
    res = sexp_opcode_arg1_type(op);
    break;
  case 1:
    res = sexp_opcode_arg2_type(op);
    break;
  default:
    res = sexp_opcode_arg3_type(op);
    if (sexp_vectorp(res)) {
      if (sexp_vector_length(res) > (sexp_unbox_fixnum(k)-2))
        res = sexp_vector_ref(res, sexp_fx_sub(k, SEXP_TWO));
      else
        res = sexp_type_by_index(ctx, 0);
    }
    break;
  }
  return sexp_translate_opcode_type(ctx, res);
}

static sexp sexp_get_opcode_num_params (sexp ctx sexp_api_params(self, n), sexp op) {
  if (! sexp_opcodep(op))
    return sexp_type_exception(ctx, self, SEXP_OPCODE, op);
  return sexp_make_fixnum(sexp_opcode_num_args(op));
}

static sexp sexp_analyze_op (sexp ctx sexp_api_params(self, n), sexp x, sexp e) {
  sexp ctx2 = ctx;
  if (sexp_envp(e)) {
    ctx2 = sexp_make_child_context(ctx, NULL);
    sexp_context_env(ctx2) = e;
  }
  return sexp_analyze(ctx2, x);
}

static sexp sexp_optimize (sexp ctx sexp_api_params(self, n), sexp x) {
  sexp_gc_var2(ls, res);
  sexp_gc_preserve2(ctx, ls, res);
  res = x;
  ls = sexp_global(ctx, SEXP_G_OPTIMIZATIONS);
  for ( ; sexp_pairp(ls); ls=sexp_cdr(ls))
    res = sexp_apply1(ctx, sexp_cdar(ls), res);
  sexp_free_vars(ctx, res, SEXP_NULL);
  sexp_gc_release2(ctx);
  return res;
}

#define sexp_define_type(ctx, name, tag) \
  sexp_env_define(ctx, env, sexp_intern(ctx, name, -1), sexp_type_by_index(ctx, tag));

sexp sexp_init_library (sexp ctx sexp_api_params(self, n), sexp env) {
  sexp_define_type(ctx, "object", SEXP_OBJECT);
  sexp_define_type(ctx, "number", SEXP_NUMBER);
  sexp_define_type(ctx, "bignum", SEXP_BIGNUM);
  sexp_define_type(ctx, "flonum", SEXP_FLONUM);
  sexp_define_type(ctx, "integer", SEXP_FIXNUM);
  sexp_define_type(ctx, "opcode", SEXP_OPCODE);
  sexp_define_type(ctx, "procedure", SEXP_PROCEDURE);
  sexp_define_type(ctx, "bytecode", SEXP_BYTECODE);
  sexp_define_type(ctx, "env", SEXP_ENV);
  sexp_define_type(ctx, "macro", SEXP_MACRO);
  sexp_define_type(ctx, "lam", SEXP_LAMBDA);
  sexp_define_type(ctx, "cnd", SEXP_CND);
  sexp_define_type(ctx, "set", SEXP_SET);
  sexp_define_type(ctx, "ref", SEXP_REF);
  sexp_define_type(ctx, "seq", SEXP_SEQ);
  sexp_define_type(ctx, "lit", SEXP_LIT);
  sexp_define_type(ctx, "sc", SEXP_SYNCLO);
  sexp_define_type_predicate(ctx, env, "environment?", SEXP_ENV);
  sexp_define_type_predicate(ctx, env, "bytecode?", SEXP_BYTECODE);
  sexp_define_type_predicate(ctx, env, "exception?", SEXP_EXCEPTION);
  sexp_define_type_predicate(ctx, env, "macro?", SEXP_MACRO);
  sexp_define_type_predicate(ctx, env, "syntactic-closure?", SEXP_SYNCLO);
  sexp_define_type_predicate(ctx, env, "lambda?", SEXP_LAMBDA);
  sexp_define_type_predicate(ctx, env, "cnd?", SEXP_CND);
  sexp_define_type_predicate(ctx, env, "set?", SEXP_SET);
  sexp_define_type_predicate(ctx, env, "ref?", SEXP_REF);
  sexp_define_type_predicate(ctx, env, "seq?", SEXP_SEQ);
  sexp_define_type_predicate(ctx, env, "lit?", SEXP_LIT);
  sexp_define_type_predicate(ctx, env, "opcode?", SEXP_OPCODE);
  sexp_define_type_predicate(ctx, env, "type?", SEXP_TYPE);
  sexp_define_accessors(ctx, env, SEXP_PAIR, 2, "pair-source", "pair-source-set!");
  sexp_define_accessors(ctx, env, SEXP_SYNCLO, 0, "syntactic-closure-env", "syntactic-closure-env-set!");
  sexp_define_accessors(ctx, env, SEXP_SYNCLO, 1, "syntactic-closure-vars", "syntactic-closure-vars-set!");
  sexp_define_accessors(ctx, env, SEXP_SYNCLO, 2, "syntactic-closure-expr", "syntactic-closure-expr-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 0, "lambda-name", "lambda-name-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 1, "lambda-params", "lambda-params-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 2, "lambda-body", "lambda-body-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 3, "lambda-defs", "lambda-defs-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 4, "lambda-locals", "lambda-locals-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 5, "lambda-flags", "lambda-flags-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 6, "lambda-free-vars", "lambda-free-vars-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 7, "lambda-set-vars", "lambda-set-vars-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 8, "lambda-return-type", "lambda-return-type-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 9, "lambda-param-types", "lambda-param-types-set!");
  sexp_define_accessors(ctx, env, SEXP_LAMBDA, 10, "lambda-source", "lambda-source-set!");
  sexp_define_accessors(ctx, env, SEXP_CND, 0, "cnd-test", "cnd-test-set!");
  sexp_define_accessors(ctx, env, SEXP_CND, 1, "cnd-pass", "cnd-pass-set!");
  sexp_define_accessors(ctx, env, SEXP_CND, 2, "cnd-fail", "cnd-fail-set!");
  sexp_define_accessors(ctx, env, SEXP_SET, 0, "set-var", "set-var-set!");
  sexp_define_accessors(ctx, env, SEXP_SET, 1, "set-value", "set-value-set!");
  sexp_define_accessors(ctx, env, SEXP_REF, 0, "ref-name", "ref-name-set!");
  sexp_define_accessors(ctx, env, SEXP_REF, 1, "ref-cell", "ref-cell-set!");
  sexp_define_accessors(ctx, env, SEXP_SEQ, 0, "seq-ls", "seq-ls-set!");
  sexp_define_accessors(ctx, env, SEXP_LIT, 0, "lit-value", "lit-value-set!");
  sexp_define_accessors(ctx, env, SEXP_PROCEDURE, 1, "procedure-code", "procedure-code-set!");
  sexp_define_accessors(ctx, env, SEXP_PROCEDURE, 2, "procedure-vars", "procedure-vars-set!");
  sexp_define_accessors(ctx, env, SEXP_BYTECODE, 1, "bytecode-name", "bytecode-name-set!");
  sexp_define_foreign_opt(ctx, env, "analyze", 2, sexp_analyze_op, SEXP_FALSE);
  sexp_define_foreign(ctx, env, "extend-env", 2, sexp_extend_env);
  sexp_define_foreign(ctx, env, "env-cell", 2, sexp_get_env_cell);
  sexp_define_foreign(ctx, env, "opcode-name", 1, sexp_get_opcode_name);
  sexp_define_foreign(ctx, env, "opcode-num-params", 1, sexp_get_opcode_num_params);
  sexp_define_foreign(ctx, env, "opcode-return-type", 1, sexp_get_opcode_ret_type);
  sexp_define_foreign(ctx, env, "opcode-param-type", 2, sexp_get_opcode_param_type);
  sexp_define_foreign(ctx, env, "optimize", 1, sexp_optimize);
  return SEXP_VOID;
}

