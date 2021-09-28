/**\file n_enum.h
 * Macro to build enums and their tostring counterparts, a reduced version of https://github.com/FredBienvenu/SmartEnum
 *\author Reduced by Castagnier Mickael, based on original FredBienvenu SmartEnum entry
 *\version 1.0
 *\date 28/09/2021
 */

#include <string.h> // for strcmp
#include <stdbool.h>

#define $(class, method) class##_##method

/*
	UTILS MACROS
*/
#define __SMARTENUM_MACRO_ENTRY_CASE(element_name) case element_name:
#define __SMARTENUM_MACRO_ENTRY_COMPARE(element_name) if(!strcmp(#element_name, str_value))

#define __SMARTENUM_MACRO_ENTRY_UNKNOWN_VALUE(enum_name) __##enum_name##_UNKNOWN_VALUE__
/* 
	MACROS FOR SMARTENUMS MACRO ENTRIES (TWO PARAMETERS) 
	PREFIX : __SMARTENUM_MACRO_ENTRY_
*/
#define __SMARTENUM_MACRO_ENTRY_TO_ENUM_ELEMENT(element_name, element_value)\
	element_name = element_value,
	
#define __SMARTENUM_MACRO_ENTRY_TO_ISVALID_CASE(element_name, element_value)\
	__SMARTENUM_MACRO_ENTRY_CASE(element_name) return true;

#define __SMARTENUM_MACRO_ENTRY_TO_TOSTRING_CASE(element_name, element_value)\
	__SMARTENUM_MACRO_ENTRY_CASE(element_name) return #element_name;
	
#define __SMARTENUM_MACRO_ENTRY_TO_ISSTRINGVALID_COMPARE(element_name, element_value)\
	 __SMARTENUM_MACRO_ENTRY_COMPARE(element_name) return true;

#define __SMARTENUM_MACRO_ENTRY_TO_FROMSTRING_COMPARE(element_name, element_value)\
	 __SMARTENUM_MACRO_ENTRY_COMPARE(element_name) return element_name;
	 
/* MACROS FOR SMARTENUMS MACRO ENTRIES (END) */

/* MACRO FOR ENUM DECLARATION */
#define __SMARTENUM_DECLARE_ENUM(MACRO_DEFINITION, enum_name)\
typedef enum enum_name\
{\
	MACRO_DEFINITION(__SMARTENUM_MACRO_ENTRY_TO_ENUM_ELEMENT)\
	__SMARTENUM_MACRO_ENTRY_UNKNOWN_VALUE(enum_name)\
}\
enum_name;
/* MACRO FOR ENUM DECLARATION (END) */

/* 
	MACRO FOR SMARTENUMS FUNCTIONS 
	PREFIX : __SMARTENUM_FUNCTION_
*/
// MACRO for function declaration (just add ; after function)
#define __SMARTENUM_DECLARE_FUNCTION(__SMARTENUM_FUNCTION, enum_name) __SMARTENUM_FUNCTION(enum_name);

#define __SMARTENUM_FUNCTION_ISVALID(enum_name)\
bool $(enum_name, isValid)(enum_name value)

#define __SMARTENUM_FUNCTION_TOSTRING(enum_name)\
const char* $(enum_name, toString)(enum_name value)

#define __SMARTENUM_FUNCTION_ISSTRINGVALID(enum_name)\
bool $(enum_name, isStringValid)(const char* str_value)

#define __SMARTENUM_FUNCTION_FROMSTRING(enum_name)\
enum_name $(enum_name, fromString)(const char* str_value)

/* MACRO FOR SMARTENUMS FUNCTIONS (END) */

/* 
	MACRO FOR SMARTENUMS FUNCTIONS DEFINITIONS 
	PREFIX : __SMARTENUM_DEFINE_FUNCTION_
*/
#define __SMARTENUM_DEFINE_FUNCTION_ISVALID(MACRO_DEFINITION, enum_name)\
__SMARTENUM_FUNCTION_ISVALID(enum_name)\
{\
	switch(value)\
	{\
		MACRO_DEFINITION(__SMARTENUM_MACRO_ENTRY_TO_ISVALID_CASE)\
		default: return false;\
	}\
}

#define __SMARTENUM_DEFINE_FUNCTION_TOSTRING(MACRO_DEFINITION, enum_name)\
__SMARTENUM_FUNCTION_TOSTRING(enum_name)\
{\
	switch(value)\
	{\
		MACRO_DEFINITION(__SMARTENUM_MACRO_ENTRY_TO_TOSTRING_CASE)\
		default: return 0;\
	}\
}

#define __SMARTENUM_DEFINE_FUNCTION_ISSTRINGVALID(MACRO_DEFINITION, enum_name)\
__SMARTENUM_FUNCTION_ISSTRINGVALID(enum_name)\
{\
	MACRO_DEFINITION(__SMARTENUM_MACRO_ENTRY_TO_ISSTRINGVALID_COMPARE)\
	return false;\
}

#define __SMARTENUM_DEFINE_FUNCTION_FROMSTRING(MACRO_DEFINITION, enum_name)\
__SMARTENUM_FUNCTION_FROMSTRING(enum_name)\
{\
	MACRO_DEFINITION(__SMARTENUM_MACRO_ENTRY_TO_FROMSTRING_COMPARE)\
	return __SMARTENUM_MACRO_ENTRY_UNKNOWN_VALUE(enum_name);\
}

/* MACRO FOR SMARTENUMS FUNCTIONS DEFINITIONS (END) */


/* SMART ENUM DECLARATION */
#define SMARTENUM_DECLARE(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DECLARE_ENUM(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DECLARE_FUNCTION(__SMARTENUM_FUNCTION_ISVALID, enum_name)\
__SMARTENUM_DECLARE_FUNCTION(__SMARTENUM_FUNCTION_TOSTRING, enum_name)\
__SMARTENUM_DECLARE_FUNCTION(__SMARTENUM_FUNCTION_ISSTRINGVALID, enum_name)\
__SMARTENUM_DECLARE_FUNCTION(__SMARTENUM_FUNCTION_FROMSTRING, enum_name)
/* SMART ENUM DECLARATION (END) */

/* SMART ENUM DEFINITION */
#define SMARTENUM_DEFINE(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DEFINE_FUNCTION_ISVALID(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DEFINE_FUNCTION_TOSTRING(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DEFINE_FUNCTION_ISSTRINGVALID(MACRO_DEFINITION, enum_name)\
__SMARTENUM_DEFINE_FUNCTION_FROMSTRING(MACRO_DEFINITION, enum_name)\
/* SMART ENUM DEFINITION (END) */

