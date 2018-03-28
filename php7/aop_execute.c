/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: pangudashu                                                   |
  +----------------------------------------------------------------------+
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"
#include "ext/pcre/php_pcre.h"

#include "php_aop.h"
#include "aop_joinpoint.h"

static int pointcut_match_zend_class_entry(pointcut *pc, zend_class_entry *ce) /*{{{*/
{
    int i, matches;

    matches = pcre_exec(pc->re_class, NULL, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), 0, 0, NULL, 0);
    if (matches >= 0) {
        return 1;
    }           
    for (i = 0; i < (int) ce->num_interfaces; i++) {
        matches = pcre_exec(pc->re_class, NULL, ZSTR_VAL(ce->interfaces[i]->name), ZSTR_LEN(ce->interfaces[i]->name), 0, 0, NULL, 0);
        if (matches >= 0) {
            return 1;
        }
    }
    
    for (i = 0; i < (int) ce->num_traits; i++) {
        matches = pcre_exec(pc->re_class, NULL, ZSTR_VAL(ce->traits[i]->name), ZSTR_LEN(ce->traits[i]->name), 0, 0, NULL, 0);
        if (matches>=0) {
            return 1;
        }
    }
    
    ce = ce->parent;
    while (ce != NULL) {
        matches = pcre_exec(pc->re_class, NULL, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), 0, 0, NULL, 0);
        if (matches >= 0) {
            return 1;
        }
        ce = ce->parent;
    }
    return 0; 
}
/*}}}*/

static zend_array *calculate_class_pointcuts(zend_class_entry *ce, int kind_of_advice) /*{{{*/
{
    pointcut *pc;
    zend_array *ht;
    zval *pc_value;
    
    ALLOC_HASHTABLE(ht);
    zend_hash_init(ht, 16, NULL, NULL , 0);
   
    ZEND_HASH_FOREACH_VAL(AOP_G(pointcuts_table), pc_value) {
        pc = (pointcut *)Z_PTR_P(pc_value); 
        if (!(pc->kind_of_advice & kind_of_advice)) {
            continue;
        }
        if ((ce == NULL && pc->kind_of_advice & AOP_KIND_FUNCTION) 
            || (ce != NULL && pointcut_match_zend_class_entry(pc, ce))) {
	        zend_hash_next_index_insert(ht, pc_value);
        }
    } ZEND_HASH_FOREACH_END(); 
    
    return ht;
}
/*}}}*/

static int pointcut_match_zend_function(pointcut *pc, zend_execute_data *ex) /*{{{*/
{
    int comp_start = 0;
    zend_function *curr_func = ex->func;
    
    //check static
    if (pc->static_state != 2) {
        if (pc->static_state) {
            if (!(curr_func->common.fn_flags & ZEND_ACC_STATIC)) {
                return 0;
            }
        } else {
            if ((curr_func->common.fn_flags & ZEND_ACC_STATIC)) {
                return 0;
            }
        }
    }
    //check public/protect/private
    if (pc->scope != 0 && !(pc->scope & (curr_func->common.fn_flags & ZEND_ACC_PPP_MASK))) {
        return 0;
    } 

    if (pc->class_name == NULL && ZSTR_VAL(pc->method)[0] == '*' && ZSTR_VAL(pc->method)[1] == '\0') {
        return 1;
    }
    if (pc->class_name == NULL && curr_func->common.scope != NULL) {
        return 0;
    }
    if (pc->method_jok) {
        int matches = pcre_exec(pc->re_method, NULL, ZSTR_VAL(curr_func->common.function_name), ZSTR_LEN(curr_func->common.function_name), 0, 0, NULL, 0);
    
        if (matches < 0) {
            return 0;
        }
    } else {
        if (ZSTR_VAL(pc->method)[0] == '\\') {
            comp_start = 1;
        }
        if (strcasecmp(ZSTR_VAL(pc->method) + comp_start, ZSTR_VAL(curr_func->common.function_name))) {
            return 0;
        }
    }
    return 1;
}
/*}}}*/

static zend_array *calculate_function_pointcuts(zend_execute_data *ex) /*{{{*/
{
    zend_object *object = NULL;
    zend_class_entry *ce = NULL;
    zend_array *class_pointcuts;
    zval *pc_value;
    pointcut *pc;
    zend_ulong h;

    object = Z_OBJ(ex->This);
    if (object != NULL) {
        ce = Z_OBJCE(ex->This);
    }

    if (ce == NULL && ex->func->common.fn_flags & ZEND_ACC_STATIC) {
        ce = ex->called_scope;
    }
    
    class_pointcuts = calculate_class_pointcuts(ce, AOP_KIND_FUNCTION | AOP_KIND_METHOD);

    ZEND_HASH_FOREACH_NUM_KEY_VAL(class_pointcuts, h, pc_value) {
        pc = (pointcut *)Z_PTR_P(pc_value);
        if (pointcut_match_zend_function(pc, ex)) {
            continue;
        }
        //delete unmatch element
        zend_hash_index_del(class_pointcuts, h);
    } ZEND_HASH_FOREACH_END(); 

    return class_pointcuts;
}
/*}}}*/

object_cache *get_object_cache (zend_object *object) /*{{{*/
{
    int i;
    uint32_t handle;

    handle = object->handle;
    if (handle >= AOP_G(object_cache_size)) {
        AOP_G(object_cache) = erealloc(AOP_G(object_cache), sizeof(object_cache)*handle + 1);
        for (i = AOP_G(object_cache_size); i <= handle; i++) {
            AOP_G(object_cache)[i] = NULL;
        }
        AOP_G(object_cache_size) = handle+1;
    }
    if (AOP_G(object_cache)[handle] == NULL) {
        AOP_G(object_cache)[handle] = emalloc(sizeof(object_cache));
        AOP_G(object_cache)[handle]->write = NULL;
        AOP_G(object_cache)[handle]->read = NULL;
        AOP_G(object_cache)[handle]->func = NULL;
    }
    return AOP_G(object_cache)[handle];
}
/*}}}*/

zend_array *get_object_cache_func(zend_object *object)
{
    object_cache *cache;
    cache = get_object_cache(object);
    if (cache->func == NULL) {
        ALLOC_HASHTABLE(cache->func);
        zend_hash_init(cache->func, 16, NULL, free_pointcut_cache , 0);
    }
    return cache->func;;
}

static zend_array *get_cache_func(zend_execute_data *ex) /*{{{*/
{
    zend_array *pointcut_table = NULL;
    zend_array *ht_object_cache = NULL;
    zend_object *object = NULL;
    zend_class_entry *ce;
    zend_string *cache_key;
    zval *cache = NULL;
    zval pointcut_cache_value;
    pointcut_cache *_pointcut_cache = NULL;

    object = Z_OBJ(ex->This);
    //1.search cache
    if (object == NULL) { //function or static method
        ht_object_cache = AOP_G(function_cache);
        if (ex->func->common.fn_flags & ZEND_ACC_STATIC) {
            ce = ex->called_scope;
            cache_key = zend_string_init("", ZSTR_LEN(ex->func->common.function_name) + ZSTR_LEN(ce->name) + 2, 0);
            sprintf((char *)ZSTR_VAL(cache_key), "%s::%s", ZSTR_VAL(ce->name), ZSTR_VAL(ex->func->common.function_name));
        } else {
            cache_key = zend_string_copy(ex->func->common.function_name);
        }
    } else { //method
        ce = ex->called_scope;
        cache_key = zend_string_copy(ex->func->common.function_name);
        ht_object_cache = get_object_cache_func(object);
    }    

    cache = zend_hash_find(ht_object_cache, cache_key);

    if (cache != NULL) {
        _pointcut_cache = (pointcut_cache *)Z_PTR_P(cache);
        if (_pointcut_cache->version != AOP_G(pointcut_version) || (object != NULL && _pointcut_cache->ce != ce)) {
            //cache lost
            _pointcut_cache = NULL;
            zend_hash_del(ht_object_cache, cache_key);
        }
    }

    //2.calculate function hit pointcut
    if (_pointcut_cache == NULL) {
        _pointcut_cache = (pointcut_cache *)emalloc(sizeof(pointcut_cache));
        _pointcut_cache->ht = calculate_function_pointcuts(ex);
        _pointcut_cache->version = AOP_G(pointcut_version);

        if (object == NULL) {
            _pointcut_cache->ce = NULL;
        } else {
            _pointcut_cache->ce = ce;
        }
        ZVAL_PTR(&pointcut_cache_value, _pointcut_cache);

        zend_hash_add(ht_object_cache, cache_key, &pointcut_cache_value);
    }
    zend_string_release(cache_key);
    return _pointcut_cache->ht;
}
/*}}}*/

static void execute_pointcut(pointcut *pc, zval *arg, zend_execute_data *call, zval *retval)
{
    zval params[1];

    if (EG(current_execute_data) == call){
        //dely to execute call function, execute pointcut first
        EG(current_execute_data) = call->prev_execute_data;
    }

    ZVAL_COPY_VALUE(&params[0], arg);

    pc->fci.retval = retval;
    pc->fci.param_count = 1;
    pc->fci.params = params;

    if (zend_call_function(&pc->fci, &pc->fci_cache) == FAILURE) {
        zend_error(E_ERROR, "Problem in AOP Callback");
    }

    if (EG(current_execute_data) == call->prev_execute_data){
        //EG(current_execute_data) = call;
    }
}

static void execute_context(zend_execute_data *ex, int args_overloaded, zval *args)
{
    zend_class_entry *current_scope = NULL;

    if (EG(exception)) {
        return ;
    }

    if (EG(scope) != ex->called_scope) {
        current_scope = EG(scope);
        EG(scope) = ex->called_scope;
    }

    //overload arguments
    if (args_overloaded) {
        uint32_t i, max_num_args, num_args = 0;
        zend_ulong args_index;
        zval *original_args_value;
        zval *overload_args_value;

        max_num_args = ex->func->common.num_args;

        ZEND_HASH_FOREACH_NUM_KEY_VAL(Z_ARR_P(args), args_index, overload_args_value) {
            if (args_index > max_num_args - 1) {
                zval_ptr_dtor(overload_args_value);
                continue;
            }
            original_args_value = ZEND_CALL_VAR_NUM(ex, args_index);
            Z_TRY_DELREF_P(original_args_value);

            ZVAL_COPY(original_args_value, overload_args_value);
            num_args++;
        } ZEND_HASH_FOREACH_END(); 
        
        ZEND_CALL_NUM_ARGS(ex) = num_args;
    }
    
    EG(current_execute_data) = ex;

    if (ex->func->common.type == ZEND_USER_FUNCTION) {
        original_zend_execute_ex(ex);
    } else if (ex->func->common.type == ZEND_INTERNAL_FUNCTION) {
        //zval return_value;
        if (original_zend_execute_internal) {
            original_zend_execute_internal(ex, ex->return_value);
        }else{
            execute_internal(ex, ex->return_value);
        }
    } else { /* ZEND_OVERLOADED_FUNCTION */
        //TODO:
    }

    if (current_scope != NULL) {
        EG(scope) = current_scope;
    }

    if (args_overloaded) {
        zval_ptr_dtor(args);
    }
}

void func_pointcut_and_execute(HashPosition pos, zend_array *pointcut_table, zend_execute_data *ex, int args_overloaded, zval *args) 
{
    pointcut *current_pc = NULL;
    zval *current_pc_value = NULL;
    zval aop_object;
    AopJoinpoint_object *joinpoint;
    zend_object *exception = NULL;
    zval pointcut_ret;

    if (pointcut_table == NULL) {
        //find pointcut of current call function
        pointcut_table = get_cache_func (ex);
        if (pointcut_table == NULL) {
            AOP_G(overloaded) = 0;
            execute_context(ex, args_overloaded, args);
            AOP_G(overloaded) = 1;
            return;
        }
        zend_hash_internal_pointer_reset_ex(pointcut_table, &pos);
    } else {
        zend_hash_move_forward_ex(pointcut_table, &pos);
    }

    current_pc_value = zend_hash_get_current_data_ex(pointcut_table, &pos);
    if (current_pc_value == NULL) {
        AOP_G(overloaded) = 0;
        execute_context(ex, args_overloaded, args);
        AOP_G(overloaded) = 1;
        return;
    }
    current_pc = (pointcut *)Z_PTR_P(current_pc_value);
    
    //TODO:make JoinPoint object
    object_init_ex(&aop_object, aop_joinpoint_ce);
    joinpoint = (AopJoinpoint_object *)(Z_OBJ(aop_object));
    joinpoint->ex = ex;
    joinpoint->current_pointcut = current_pc;
    joinpoint->pos = pos;
    joinpoint->advice = pointcut_table;
    joinpoint->kind_of_advice = current_pc->kind_of_advice;
    joinpoint->exception = NULL;

    if (args_overloaded) {
        Z_TRY_ADDREF_P(args);
        joinpoint->args = args;
    }
    joinpoint->args_overloaded = args_overloaded;

    if (current_pc->kind_of_advice & AOP_KIND_BEFORE) {
        if (!EG(exception)) {
            execute_pointcut(current_pc, &aop_object, ex, &pointcut_ret);
            if (&pointcut_ret != NULL) {
                zval_ptr_dtor(&pointcut_ret);
            }
        }
    }
    if (current_pc->kind_of_advice & AOP_KIND_AROUND) {
        if (!EG(exception)) {
            execute_pointcut(current_pc, &aop_object, ex, &pointcut_ret);
            if (&pointcut_ret != NULL && !Z_ISNULL(pointcut_ret)) {
                if (ex->return_value != NULL) {
                    zval_ptr_dtor(ex->return_value);
                    ZVAL_COPY_VALUE(ex->return_value, &pointcut_ret);
                } else {
                    zval_ptr_dtor(&pointcut_ret);
                }
            }
        }
    } else {
        func_pointcut_and_execute(pos, pointcut_table, ex, joinpoint->args_overloaded, joinpoint->args);
    }
    //AOP_KIND_AFTER
    if (current_pc->kind_of_advice & AOP_KIND_AFTER) {
        if (current_pc->kind_of_advice & AOP_KIND_CATCH && EG(exception)) {
            exception = EG(exception);
            joinpoint->exception = exception;
            EG(exception) = NULL;
            execute_pointcut(current_pc, &aop_object, ex, &pointcut_ret);
            EG(exception) = exception;

            if (&pointcut_ret != NULL && !Z_ISNULL(pointcut_ret)) {
                if (ex->return_value != NULL) {
                    zval_ptr_dtor(ex->return_value);
                    ZVAL_COPY_VALUE(ex->return_value, &pointcut_ret);
                } else {
                    zval_ptr_dtor(&pointcut_ret);
                }
            }

        } else if (current_pc->kind_of_advice & AOP_KIND_RETURN && !EG(exception)) {
            execute_pointcut(current_pc, &aop_object, ex, &pointcut_ret);
            
            if (&pointcut_ret != NULL && !Z_ISNULL(pointcut_ret)) {
                if (ex->return_value != NULL) {
                    zval_ptr_dtor(ex->return_value);
                    ZVAL_COPY_VALUE(ex->return_value, &pointcut_ret);
                } else {
                    zval_ptr_dtor(&pointcut_ret);
                }
            }

        }
    }

    //Z_DELREF(aop_object);
    zval_ptr_dtor(&aop_object);
    return;
}

//execute_ex overload
ZEND_API void aop_execute_ex(zend_execute_data *ex)
{
    zend_function *fbc = NULL;
    HashPosition pos = 0;
    
    fbc = ex->func;

    if (!AOP_G(aop_enable) || fbc == NULL || AOP_G(overloaded) || EG(exception) || fbc->common.function_name == NULL || fbc->common.type == ZEND_EVAL_CODE || fbc->common.fn_flags & ZEND_ACC_CLOSURE) {
	    return original_zend_execute_ex(ex);
    }

    AOP_G(overloaded) = 1;
    func_pointcut_and_execute(pos, NULL, ex, 0, NULL);
    AOP_G(overloaded) = 0;
}

ZEND_API void aop_execute_internal(zend_execute_data *ex, zval *return_value)
{
    zend_function *fbc = NULL;
    HashPosition pos = 0;
    
    fbc = ex->func;

    if (!AOP_G(aop_enable) || fbc == NULL || AOP_G(overloaded) || EG(exception) || fbc->common.function_name == NULL) {
        if (original_zend_execute_internal) {
            return original_zend_execute_internal(ex, return_value);
        }else{
            return execute_internal(ex, return_value);
        }
    }

    ex->return_value = return_value;

    AOP_G(overloaded) = 1;
    func_pointcut_and_execute(pos, NULL, ex, 0, NULL);
    AOP_G(overloaded) = 0;
}

