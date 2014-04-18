/*
 * Copyright (C) 2013 Kay Sievers
 * Copyright (C) 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2013 Daniel Mack <daniel@zonque.org>
 * Copyright (C) 2013 Linux Foundation
 *
 * kdbus is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __KDBUS_UTIL_H
#define __KDBUS_UTIL_H

#include "linux/ioctl.h"
#include "kdbus.h"

/* all exported addresses are 64 bit */
#define KDBUS_PTR(addr) ((void __user *)(uintptr_t)(addr))

/* all exported sizes are 64 bit and data aligned to 64 bit */
#define KDBUS_ALIGN8(s) ALIGN((s), 8)
#define KDBUS_IS_ALIGNED8(s) (IS_ALIGNED(s, 8))

/* generic access and iterators over a stream of items */
#define KDBUS_ITEM_HEADER_SIZE offsetof(struct kdbus_item, data)
#define KDBUS_ITEM_SIZE(s) KDBUS_ALIGN8(KDBUS_ITEM_HEADER_SIZE + (s))
#define KDBUS_ITEM_NEXT(item) \
	(typeof(item))(((u8 *)item) + KDBUS_ALIGN8((item)->size))
#define KDBUS_ITEM_FOREACH(item, head, first)				\
	for (item = (head)->first;					\
	     (u8 *)(item) < (u8 *)(head) + (head)->size;		\
	     item = KDBUS_ITEM_NEXT(item))
#define KDBUS_ITEM_VALID(item, head)					\
	((item)->size > KDBUS_ITEM_HEADER_SIZE &&			\
	 (u8 *)(item) + (item)->size <= (u8 *)(head) + (head)->size)
#define KDBUS_ITEM_END(item, head)					\
	((u8 *)item == ((u8 *)(head) + KDBUS_ALIGN8((head)->size)))

/**
 * kdbus_size_get_user - read the size variable from user memory
 * @_s:			Size variable
 * @_b:			Buffer to read from
 * @_t:			Structure, "size" is a member of
 *
 * Return: the result of copy_from_user()
 */
#define kdbus_size_get_user(_s, _b, _t)						\
({										\
	u64 __user *_sz = (void __user *)(_b) + offsetof(typeof(_t), size);	\
	copy_from_user(_s, _sz, sizeof(__u64));					\
})

/**
 * kdbus_offset_set_user - write the offset variable to user memory
 * @_s:			Offset variable
 * @_b:			Buffer to write to
 * @_t:			Structure, "offset" is a member of
 *
 * Return: the result of copy_to_user()
 */
#define kdbus_offset_set_user(_s, _b, _t)				\
({									\
	u64 __user *_sz = (void __user *)(_b) + offsetof(_t, offset);	\
	copy_to_user(_sz, _s, sizeof(__u64));				\
})

/**
 * kdbus_check_strlen - check length of a string at the end a structure
 * @_p:			A pointer to a structure that has a size member and
 * 			a variable string at its end
 * @_s:			The name of the dynamically sized string member
 *
 * Return: 1 if the string's end marker is withing the struct, or 0 otherwise.
 */
#define kdbus_check_strlen(_p, _s)					\
({									\
	size_t _max = (_p)->size - offsetof(typeof(*(_p)), _s);		\
	_max == 0 || strnlen((_p)->_s, _max) != _max;			\
})

/**
 * kdbus_item_validate_nul - check the validity of an item containing a string
 * @item:		Item to check
 *
 * Validate that a string in a given item matches the given size, and the
 * string is \0 terminated.
 *
 * Return: true if the string in given item is valid
 */
static inline bool kdbus_item_validate_nul(const struct kdbus_item *item)
{
	size_t l = item->size - KDBUS_ITEM_HEADER_SIZE;
	return l > 0 && memchr(item->str, '\0', l) == item->str + l - 1;
}

/**
 * kdbus_str_hash - calculate a hash
 * @str:		String
 *
 * Return: hash value
 */
static inline unsigned int kdbus_str_hash(const char *str)
{
	return full_name_hash(str, strlen(str));
}

int kdbus_sysname_is_valid(const char *name);
#endif
