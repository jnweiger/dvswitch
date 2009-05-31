// Copyright 2005-2009 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// Geometry utilities, currently only for rectangles

#ifndef DVSWITCH_GEOMETRY_H
#define DVSWITCH_GEOMETRY_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

struct rectangle;
static inline void rectangle_extend(struct rectangle * rect,
				    const struct rectangle * other);
static inline void rectangle_clip(struct rectangle * rect,
				  const struct rectangle * other);
static inline bool rectangle_is_empty(const struct rectangle * rect);

struct rectangle
{
    int left, top;     // inclusive
    int right, bottom; // exclusive; must be >= opposite edges

#ifdef __cplusplus
    rectangle & operator|=(const rectangle & other)
    {
	rectangle_extend(this, &other);
	return *this;
    }
    rectangle & operator&=(const rectangle & other)
    {
	rectangle_clip(this, &other);
	return *this;
    }
    bool empty() const
    {
	return rectangle_is_empty(this);
    }
#endif
};

static inline void rectangle_extend(struct rectangle * rect,
				    const struct rectangle * other)
{
    if (rectangle_is_empty(other))
    {
	// use current extents unchanged
    }
    else if (rectangle_is_empty(rect))
    {
	// use other extents
	*rect = *other;
    }
    else
    {
	// find rectangle enclosing both extents
	if (other->left < rect->left)
	    rect->left = other->left;
	if (other->right > rect->right)
	    rect->right = other->right;
	if (other->top < rect->top)
	    rect->top = other->top;
	if (other->bottom > rect->bottom)
	    rect->bottom = other->bottom;
    }
}

static inline void rectangle_clip(struct rectangle * rect,
				  const struct rectangle * other)
{
    // find rectangle enclosed in both extents
    if (other->left > rect->left)
	rect->left = other->left;
    if (other->right < rect->right)
	rect->right = other->right;
    if (other->top > rect->top)
	rect->top = other->top;
    if (other->bottom < rect->bottom)
	rect->bottom = other->bottom;

    // Maintain invariant
    if (rect->left > rect->right)
	rect->left = rect->right;
    if (rect->top > rect->bottom)
	rect->top = rect->bottom;
}

static inline bool rectangle_is_empty(const struct rectangle * rect)
{
    return rect->left == rect->right || rect->bottom == rect->top;
}

#endif // !DVSWITCH_GEOMETRY_H
