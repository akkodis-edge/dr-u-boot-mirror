#ifndef AKE_NVRAM_H__
#define AKE_NVRAM_H__

/**
 * nvram_get() - Look up the value of an nvram variable
 *
 * @varname:	Variable to look up
 * @return value of variable, or NULL if not found
 */
char *nvram_get(const char* varname);

/**
 * nvram_get_ulong() - Look up the value of an nvram variable
 *
 * @varname:    Variable to look up
 * @base:       Base to use (e.g. 10 for base 10, 2 for binary)
 * @default_val: Default value to return if no value is found
 * @return the value found, or @default_val if none
 */
ulong nvram_get_ulong(const char* varname, int base, ulong default_value);

/**
 * nvram_set() - set an nvram variable
 *
 * This sets or deletes the value of an nvram variable. For setting the
 * value the variable is created if it does not already exist.
 *
 * @varname: Variable to adjust
 * @value: Value to set for the variable, or NULL or "" to delete the variable
 * @return 0 if OK, 1 on error
 */
int nvram_set(const char* varname, const char* value);

/**
 * nvram_set_ulong() - set an nvram variable to an integer
 *
 * @varname: Variable to adjust
 * @value: Value to set for the variable (will be converted to a string)
 * @return 0 if OK, 1 on error
 */
int nvram_set_ulong(const char* varname, ulong value);

/**
 * nvram_set_env() - set an nvram variable to env
 *
 * If varname is empty, return error
 *
 * @varname: Variable to copy
 * @envname: Env variable to write to
 * @return 0 if OK, 1 on error
 */
int nvram_set_env(const char* varname, const char* envname);

/**
 * nvram_commit() - commit nvram variables to flash
 *
 * @return 0 if ok, -errno on error
 */
int nvram_commit(void);

/**
 * nvram_get_list() - Get nvram_list
 * @return NULL if not OK
 */
struct libnvram_list* const nvram_get_list(void);

#endif // AKE_NVRAM_H__
