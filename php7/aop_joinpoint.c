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
  | Author:                                                              |
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

zend_class_entry *aop_joinpoint_ce;

zend_object_handlers AopJoinpoint_object_handlers;

ZEND_BEGIN_ARG_INFO_EX(arginfo_aop_args_returnbyref, 0, ZEND_RETURN_REFERENCE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_aop_args_setArguments, 0)
    ZEND_ARG_ARRAY_INFO(0, arguments, 0)
ZEND_END_ARG_INFO()

zend_function_entry aop_joinpoint_methods[] = {
    PHP_ME(AopJoinpoint, getArguments, arginfo_aop_args_returnbyref, 0)
    PHP_ME(AopJoinpoint, setArguments, arginfo_aop_args_setArguments, 0)
    PHP_ME(AopJoinpoint, getException, NULL, 0)
    PHP_ME(AopJoinpoint, getPointcut, NULL, 0)
    PHP_ME(AopJoinpoint, process, NULL, 0)
    PHP_ME(AopJoinpoint, getKindOfAdvice, NULL, 0)
    PHP_ME(AopJoinpoint, getObject, NULL, 0)
    PHP_ME(AopJoinpoint, getReturnedValue, arginfo_aop_args_returnbyref, 0)
    PHP_ME(AopJoinpoint, getClassName, NULL, 0)
    PHP_ME(AopJoinpoint, getMethodName, NULL, 0)
    PHP_ME(AopJoinpoint, getFunctionName, NULL, 0)

    PHP_FE_END
};

void aop_free_JoinPoint(zend_object *object)
{
    AopJoinpoint_object *obj = (AopJoinpoint_object *)object;

    if (obj->args != NULL) {
        //zval_ptr_dtor(obj->args);
        Z_TRY_DELREF_P(obj->args);
        efree(obj->args);
    }
    zend_object_std_dtor(object);
}
//new AopJoinPoint()
zend_object *aop_create_handler_JoinPoint(zend_class_entry *ce) /*{{{*/
{
    AopJoinpoint_object *obj = (AopJoinpoint_object *)emalloc(sizeof(AopJoinpoint_object));
    
    zend_object_std_init(&obj->std, ce);
    obj->std.handlers = &AopJoinpoint_object_handlers;

    return &obj->std;
}
/*}}}*/

void register_class_AopJoinPoint(void) /*{{{*/
{
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "AopJoinpoint", aop_joinpoint_methods);
    aop_joinpoint_ce = zend_register_internal_class(&ce);

    aop_joinpoint_ce->create_object = aop_create_handler_JoinPoint;
    memcpy(&AopJoinpoint_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    AopJoinpoint_object_handlers.clone_obj = NULL;
    AopJoinpoint_object_handlers.free_obj = aop_free_JoinPoint;
}
/*}}}*/

/*{{{ proto AopJoinpoint::getArguments()
 */
PHP_METHOD(AopJoinpoint, getArguments)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());

    if (object->args == NULL) {
        uint32_t num_args, i;
        zval *arg;
        zval *ret = emalloc(sizeof(zval));
        
        array_init(ret);
        num_args = ZEND_CALL_NUM_ARGS(object->ex);
        for (i = 0; i < num_args; i++){
            arg = ZEND_CALL_VAR_NUM(object->ex, i);
            if (Z_ISUNDEF_P(arg)) {
                continue;
            }
            Z_TRY_ADDREF_P(arg);
            zend_hash_next_index_insert(Z_ARR_P(ret), arg);
        }
        object->args = ret;
    }
    RETURN_ZVAL(object->args, 1, 0);
}
/*}}}*/

/*{{{ proto AopJoinpoint::setArguments()
 */
PHP_METHOD(AopJoinpoint, setArguments)
{
    zval *params;
    zval *args;
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY) {
        zend_error(E_ERROR, "setArguments is only available when the JoinPoint is a function or ia method call");
    }

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

    if (object->args != NULL) {
        zval_ptr_dtor(object->args);
        efree(object->args);
    }
    args = emalloc(sizeof(zval));
    ZVAL_COPY(args, params);
    object->args = args;
    object->args_overloaded = 1;

    RETURN_NULL();
}
/*}}}*/

/*{{{ proto AopJoinpoint::getException()
 */
PHP_METHOD(AopJoinpoint, getException)
{
    zval exception_val;
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    if (!(object->current_pointcut->kind_of_advice & AOP_KIND_CATCH)){
        zend_error(E_ERROR, "getException is only available when the advice was added with aop_add_after or aop_add_after_throwing"); 
    }

    if (object->exception != NULL) {
        ZVAL_OBJ(&exception_val, object->exception);
        RETURN_ZVAL(&exception_val, 1, 0);
    }
    RETURN_NULL();
}
/*}}}*/

/*{{{ proto AopJoinpoint::getPointcut()
 */
PHP_METHOD(AopJoinpoint, getPointcut)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    RETURN_STR(object->current_pointcut->selector);
}
/*}}}*/

PHP_METHOD(AopJoinpoint, process)
{
    zval call_ret;
    int is_ret_overloaded = 0;
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    if (!object || !object->current_pointcut || !object->current_pointcut->kind_of_advice) {
        zend_error(E_ERROR, "Error");
    }
    if (!(object->current_pointcut->kind_of_advice & AOP_KIND_AROUND)) {
        zend_error(E_ERROR, "process is only available when the advice was added with aop_add_around"); 
    }
    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY) {
        //TODO:
        if (object->kind_of_advice & AOP_KIND_WRITE) {
        } else {
        }
    } else {
        if (object->ex->return_value == NULL) {
            object->ex->return_value = &call_ret;
            is_ret_overloaded = 1;
        }
        func_pointcut_and_execute(object->pos, object->advice, object->ex, object->args_overloaded, object->args);
        if (is_ret_overloaded == 0) {
            ZVAL_COPY(return_value, object->ex->return_value);
        } else {
            ZVAL_COPY_VALUE(return_value, object->ex->return_value);
            object->ex->return_value = NULL;
        }
    }
}

/*{{{ proto AopJoinpoint::getPointcut()
 */
PHP_METHOD(AopJoinpoint, getKindOfAdvice)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    RETURN_LONG(object->kind_of_advice);
}
/*}}}*/

/*{{{ proto AopJoinpoint::getObject()
 */
PHP_METHOD(AopJoinpoint, getObject)
{
    zend_object *call_object = NULL;
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    call_object = Z_OBJ(object->ex->This);
    if (call_object != NULL) {
        RETURN_ZVAL(&object->ex->This, 1, 0);
    }
    RETURN_NULL();
}
/*}}}*/

/*{{{ proto AopJoinpoint::getReturnedValue()
 */
PHP_METHOD(AopJoinpoint, getReturnedValue)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY) {
        zend_error(E_ERROR, "getReturnedValue is not available when the JoinPoint is a property operation (read or write)"); 
    }
    if (object->current_pointcut->kind_of_advice & AOP_KIND_BEFORE) {
        zend_error(E_ERROR, "getReturnedValue is not available when the advice was added with aop_add_before");
    }

    if (object->ex->return_value != NULL) {
        RETURN_ZVAL(object->ex->return_value, 1, 0);
    }
}
/*}}}*/

PHP_METHOD(AopJoinpoint, getClassName)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());

    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY) {
        //TODO:
    } else {
        zend_class_entry *ce = NULL;
        zend_object *call_object = NULL;

        call_object = Z_OBJ(object->ex->This);
        if (call_object != NULL) {
            ce = Z_OBJCE(object->ex->This);
            RETURN_STR(ce->name);
        }

        if (ce == NULL && object->ex->func->common.fn_flags & ZEND_ACC_STATIC) {
            ce = object->ex->called_scope;
            RETURN_STR(ce->name);
        }
    }
    RETURN_NULL();
}

PHP_METHOD(AopJoinpoint, getMethodName)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY || object->current_pointcut->kind_of_advice & AOP_KIND_FUNCTION) {
        zend_error(E_ERROR, "getMethodName is only available when the JoinPoint is a method call"); 
    }
    if (object->ex == NULL) {
        RETURN_NULL();
    }
    RETURN_STR(object->ex->func->common.function_name);
}

PHP_METHOD(AopJoinpoint, getFunctionName)
{
    AopJoinpoint_object *object = (AopJoinpoint_object *)Z_OBJ_P(getThis());
    
    if (object->current_pointcut->kind_of_advice & AOP_KIND_PROPERTY || object->current_pointcut->kind_of_advice & AOP_KIND_METHOD) {
        zend_error(E_ERROR, "getMethodName is only available when the JoinPoint is a function call"); 
    }
    if (object->ex == NULL) {
        RETURN_NULL();
    }
    RETURN_STR(object->ex->func->common.function_name);
}


