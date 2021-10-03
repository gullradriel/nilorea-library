/**\file n_enum.h
 * Macro to build enums and their tostring counterparts, a reduced version of https://github.com/FredBienvenu/N_ENUM
 *\author Castagnier Mickael, based on original FredBienvenu N_ENUM entry
 *\version 1.0
 *\date 28/09/2021
 */

#include <string.h> // for strcmp
#include <stdbool.h>

#define $(class, method) class##_##method

/* UTILS MACROS */
#define __N_ENUM_MACRO_ENTRY_CASE(element_name) case element_name:
#define __N_ENUM_MACRO_ENTRY_UNKNOWN_VALUE(enum_name) __##enum_name##_UNKNOWN_VALUE__

/* MACROS FOR N_ENUMS MACRO ENTRIES (TWO PARAMETERS) PREFIX : __N_ENUM_MACRO_ENTRY_ */
#define __N_ENUM_MACRO_ENTRY_TO_ENUM_ELEMENT(element_name, element_value)\
	element_name = element_value,

#define __N_ENUM_MACRO_ENTRY_TO_ISVALID_CASE(element_name, element_value)\
	__N_ENUM_MACRO_ENTRY_CASE(element_name) return true;

#define __N_ENUM_MACRO_ENTRY_TO_TOSTRING_CASE(element_name, element_value)\
	__N_ENUM_MACRO_ENTRY_CASE(element_name) return #element_name;
/* MACROS FOR N_ENUMS MACRO ENTRIES (END) */

/* MACRO FOR ENUM DECLARATION */
#define __N_ENUM_DECLARE_ENUM(MACRO_DEFINITION, enum_name)\
	typedef enum enum_name\
{\
	MACRO_DEFINITION(__N_ENUM_MACRO_ENTRY_TO_ENUM_ELEMENT)\
	__N_ENUM_MACRO_ENTRY_UNKNOWN_VALUE(enum_name)\
}\
enum_name;
/* MACRO FOR ENUM DECLARATION (END) */

/* MACRO FOR N_ENUMS FUNCTIONS * PREFIX : __N_ENUM_FUNCTION_ */
#define __N_ENUM_DECLARE_FUNCTION(__N_ENUM_FUNCTION, enum_name) __N_ENUM_FUNCTION(enum_name); // MACRO for function declaration (just add ; after function)

#define __N_ENUM_FUNCTION_ISVALID(enum_name)\
	bool $(enum_name, isValid)(enum_name value)

#define __N_ENUM_FUNCTION_TOSTRING(enum_name)\
	const char* $(enum_name, toString)(enum_name value)
/* MACRO FOR N_ENUMS FUNCTIONS (END) */

/* MACRO FOR N_ENUMS FUNCTIONS DEFINITIONS * PREFIX : __N_ENUM_DEFINE_FUNCTION_ */
#define __N_ENUM_DEFINE_FUNCTION_ISVALID(MACRO_DEFINITION, enum_name)\
	__N_ENUM_FUNCTION_ISVALID(enum_name)\
{\
	switch(value)\
	{\
		MACRO_DEFINITION(__N_ENUM_MACRO_ENTRY_TO_ISVALID_CASE)\
		default: return false;\
	}\
}

#define __N_ENUM_DEFINE_FUNCTION_TOSTRING(MACRO_DEFINITION, enum_name)\
	__N_ENUM_FUNCTION_TOSTRING(enum_name)\
{\
	switch(value)\
	{\
		MACRO_DEFINITION(__N_ENUM_MACRO_ENTRY_TO_TOSTRING_CASE)\
		default: return 0;\
	}\
}
/* MACRO FOR N_ENUMS FUNCTIONS DEFINITIONS (END) */ 

/* N_ENUM DECLARATION */
#define N_ENUM_DECLARE(MACRO_DEFINITION, enum_name)\
	__N_ENUM_DECLARE_ENUM(MACRO_DEFINITION, enum_name)\
	__N_ENUM_DECLARE_FUNCTION(__N_ENUM_FUNCTION_ISVALID, enum_name)\
	__N_ENUM_DECLARE_FUNCTION(__N_ENUM_FUNCTION_TOSTRING, enum_name)
/* N_ENUM DECLARATION (END) */

/* N_ENUM DEFINITION */
#define N_ENUM_DEFINE(MACRO_DEFINITION, enum_name)\
	__N_ENUM_DEFINE_FUNCTION_ISVALID(MACRO_DEFINITION, enum_name)\
	__N_ENUM_DEFINE_FUNCTION_TOSTRING(MACRO_DEFINITION, enum_name)
/* N_ENUM DEFINITION (END) */

