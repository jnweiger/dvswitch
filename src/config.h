/* Copyright 2007 Ben Hutchings.
 * See the file "COPYING" for licence details.
 */
/* Configuration file reader */

#ifndef DVSWITCH_CONFIG_H
#define DVSWITCH_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read configuration files.  Exit if they are unreadable or invalid.
 * Call the item_handler function for each configuration item found.
 * There may be multiple items with the same name; the last should
 * take precedence.
 */
void dvswitch_read_config(void (*item_handler)(const char * name,
					       const char * value));

#ifdef __cplusplus
}
#endif

#endif /* !defined(DVSWITCH_CONFIG_H) */
