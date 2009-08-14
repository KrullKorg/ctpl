/* 
 * 
 * Copyright (C) 2007-2009 Colomban "Ban" Wendling <ban@herbesfolles.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef H_CTPL_STACK_H
#define H_CTPL_STACK_H

#include <glib.h>


typedef struct s_CtplStack       CtplStack;
typedef struct s_CtplStackEntry  CtplStackEntry;

struct s_CtplStackEntry
{
  guint           ref_count;
  void           *data;
  CtplStackEntry *parent;
};

/**
 * s_CtplStack:
 * @name: stack name
 * @last: Last pushed element, or NULL
 * @last_free: Last item of pushed elements for freeing them later.
 * 
 * 
 */
struct s_CtplStack
{
  char           *name;
  GCompareFunc    compare_func;
  GFreeFunc       free_func;
  CtplStackEntry *last;
  CtplStackEntry *last_free;
};


CtplStack  *ctpl_stack_new      (const char  *name,
                                 GCompareFunc compare_func,
                                 GFreeFunc    free_func);
void        ctpl_stack_free     (CtplStack *stack);

void        ctpl_stack_push     (CtplStack *stack,
                                 void      *data);
gboolean    ctpl_stack_push_ref (CtplStack *stack);
void       *ctpl_stack_pop      (CtplStack *stack);
gboolean    ctpl_stack_is_empty (CtplStack *stack);


#endif /* guard */
