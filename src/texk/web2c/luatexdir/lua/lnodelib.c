/* $Id$ */

#include "luatex-api.h"
#include <ptexlib.h>

#include "nodes.h"

/* This routine finds the numerical value of a string (or number) at
   lua stack index |n|. If it is not a valid node type, raises a lua
   error. */

static 
int do_get_node_type_id (lua_State *L, int n, node_info *data) {
  int j;
  char *s = NULL;
  int i=-1;
  if (lua_type(L,n)==LUA_TSTRING) {
    s = (char *)lua_tostring(L,n);
  } else if (lua_isnumber(L,n)) {
    i = lua_tointeger(L,n);
  } else {
    return -1;
  }
  for (j=0;data[j].id!=-1;j++) {
    if ((i>=0 && data[j].id==i) || 
	(s!=NULL && strcmp(s,data[j].name)==0)) {
      break;
    }
  }
  if (data[j].id==-1) {
    return -1;
  } else {
    return j;
  }
}

#define get_node_type_id(L,n)    do_get_node_type_id(L,n,node_data)
#define get_node_subtype_id(L,n) do_get_node_type_id(L,n,whatsit_node_data)

static 
int get_valid_node_type_id (lua_State *L, int n) {
  int i = get_node_type_id(L,n);
  if (i==-1) {
    if (lua_type(L,n)==LUA_TSTRING) {
      lua_pushfstring(L, "Invalid node type id: %s",(char *)lua_tostring(L,n));
    } else {
      lua_pushfstring(L, "Invalid node type id: %d",(int)lua_tonumber(L,n));
    }
    return lua_error(L);
  }
  return i;
}

static 
int get_valid_node_subtype_id (lua_State *L, int n) {
  int i = get_node_subtype_id(L,n);
  if (i==-1) {
    if (lua_type(L,n)==LUA_TSTRING) {
      lua_pushfstring(L, "Invalid whatsit node id: %s",(char *)lua_tostring(L,n));
    } else {
      lua_pushfstring(L, "Invalid whatsit node id: %d",(int)lua_tonumber(L,n));
    }
    return lua_error(L);
  }
  return i;
}



/* Creates a userdata object for a number found at the stack top, 
  if it is representing a node (i.e. an pointer into |varmem|). 
  It replaces the stack entry with the new userdata, or pushes
  |nil| if the number is |null|, or if the index is definately out of
  range. This test could be improved.
*/

void
lua_nodelib_push(lua_State *L) {
  halfword n;
  halfword *a;
  n = -1;
  if (lua_isnumber(L,-1)) {
    n = lua_tointeger(L,-1);
  }
  lua_pop(L,1);
  if ((n==null) || (n<0) || (n>var_mem_max)) {
    lua_pushnil(L);
  } else {
    a = lua_newuserdata(L, sizeof(halfword));
    *a = n;
    luaL_getmetatable(L,NODE_METATABLE);
    lua_setmetatable(L,-2);
  }
  return;
}

/* converts type strings to type ids */

static int
lua_nodelib_id (lua_State *L) {
  integer i = get_node_type_id(L,1);
  if (i>=0) {
    lua_pushnumber(L,i);
  } else {
    lua_pushnil(L);
  }
  return 1;
}


static int
lua_nodelib_subtype (lua_State *L) {
  integer i = get_node_subtype_id(L,1);
  if (i>=0) {
    lua_pushnumber(L,i);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

/* converts id numbers to type names */

static int
lua_nodelib_type (lua_State *L) {
  integer i = get_node_type_id(L,1);
  if (i>=0) {
    lua_pushstring(L,node_data[i].name);
  } else {
    lua_pushnil(L);
  }
  return 1;
}


/* allocate a new node */

static int
lua_nodelib_new(lua_State *L) {
  int i,j;
  halfword n  = null;
  i = get_valid_node_type_id(L,1);

  if (i==whatsit_node) {
    j = -1;
    if (lua_gettop(L)>1) {  j = get_valid_node_subtype_id(L,2);  }
    if (j<0) {
      lua_pushstring(L, "Creating a whatsit requires the subtype number as a second argument");
      lua_error(L);
    }
  } else {
    j = 0;
    if (lua_gettop(L)>1) { j = lua_tointeger(L,2); }
  }
  n = new_node(i,j);
  lua_pushnumber(L,n);
  lua_nodelib_push(L);
  return 1;
}


/* Free a node.
   This function returns the 'next' node, because that may be helpful */

static int
lua_nodelib_free(lua_State *L) {
  halfword *n;
  halfword p;
  if (lua_gettop(L)<1) {
    lua_pushnil(L);
    return 1;
  } else if(lua_isnil(L,1)) {
    return 1; /* the nil itself */
  }
  n = check_isnode(L,1);
  p = vlink(*n); 
  flush_node(*n); 
  lua_pushnumber(L,p);
  lua_nodelib_push(L);
  return 1;
}

/* Free a node list */

static int
lua_nodelib_flush_list(lua_State *L) {
  halfword *n_ptr;
  if ((lua_gettop(L)<1) || lua_isnil(L,1))
    return 0;
  n_ptr = check_isnode(L,1);
  flush_node_list(*n_ptr); 
  return 0;
}

/* find prev, and fix backlinks */

#define set_t_to_prev(head,current)		\
  t = head;					\
  while (vlink(t)!=current && t != null) {	\
    if (vlink(t)!=null)				\
      alink(vlink(t)) = t;			\
    t = vlink(t);				\
  }

/* remove a node from a list */


static int
lua_nodelib_remove (lua_State *L) {
  halfword head, current, t;
  if (lua_gettop(L)<2) {
    lua_pushstring(L,"Not enough arguments for node.remove()");
    lua_error(L);
  }
  head = *(check_isnode(L,1));
  if (lua_isnil(L,2)) {
    return 2; /* the arguments, as they are */
  }
  current = *(check_isnode(L,2));

  if (head == current) {
    if (alink(head)!=null && vlink(current)!=null) {
      alink(vlink(current)) = alink(head);
    }
    head  = vlink(current);
    current = head;
  } else {  /* head != current */
    t = alink(current);
    if (t==null || vlink(t)!=current) {
      set_t_to_prev(head,current);
      if (t==null) { /* error! */
	lua_pushstring(L,"Attempt to node.remove() a non-existing node");
	lua_error(L);
      }
    }
    /* t is now the previous node */
    vlink(t) = vlink(current);
    if (vlink(current)!=null) {
      alink(vlink(current))  = t;
    }
    current  = vlink(current);
  }
  lua_pushnumber(L,head);
  lua_nodelib_push(L);  
  lua_pushnumber(L,current);
  lua_nodelib_push(L);  
  return 2;
}

/* Insert a node in a list */

static int
lua_nodelib_insert_before (lua_State *L) {
  halfword head, current, n, t;
  if (lua_gettop(L)<3) {
    lua_pushstring(L,"Not enough arguments for node.insert_before()");
    lua_error(L);
  }
  if (lua_isnil(L,3)) {
    lua_pop(L,1);
    return 2;
  } else {
    n = *(check_isnode(L,3));
  }
  if (lua_isnil(L,1)) { /* no head */
    vlink(n) = null;
    alink(n) = null;
    lua_pushnumber(L,n);
    lua_nodelib_push(L);    
    lua_pushvalue(L,-1);
    return 2;  
  } else {
    head = *(check_isnode(L,1));
  }
  if (lua_isnil(L,2)) {
    current = tail_of_list(head);
  } else {
    current = *(check_isnode(L,2));
  }
  t = alink(current);
  if (t==null || vlink(t)!=current) {
    set_t_to_prev(head,current);
    if (t==null) { /* error! */
      lua_pushstring(L,"Attempt to node.insert_before() a non-existing node");
      lua_error(L);
    }
  }
  couple_nodes(t,n);
  couple_nodes(n,current);
  if (head==current) {
    lua_pushnumber(L,n);
  } else {
    lua_pushnumber(L,head);
  }
  lua_nodelib_push(L);
  lua_pushnumber(L,n);
  lua_nodelib_push(L);    
  return 2; 
}


static int
lua_nodelib_insert_after (lua_State *L) {
  halfword head, current, n;
  if (lua_gettop(L)<3) {
    lua_pushstring(L,"Not enough arguments for node.insert_after()");
    lua_error(L);
  }
  if (lua_isnil(L,3)) {
    lua_pop(L,1);
    return 2;
  } else {
    n = *(check_isnode(L,3));
  }
  if (lua_isnil(L,1)) { /* no head */
    vlink(n) = null;
    alink(n) = null;
    lua_pushnumber(L,n);
    lua_nodelib_push(L);    
    lua_pushvalue(L,-1);
    return 2;  
  } else {
    head = *(check_isnode(L,1));
  }
  if (lua_isnil(L,2)) {
    current = head;
    while (vlink(current)!=null)  
      current = vlink(current);
  } else {
    current = *(check_isnode(L,2));
  }
  couple_nodes(n,vlink(current));
  couple_nodes(current, n);

  lua_pop(L,2);
  lua_pushnumber(L,n);
  lua_nodelib_push(L);    
  return 2; 
}


/* Copy a node list */

static int
lua_nodelib_copy_list (lua_State *L) {
  halfword *n;
  halfword m;
  if (lua_isnil(L,1))
    return 1; /* the nil itself */
  n = check_isnode(L,1);
  m = copy_node_list(*n);
  lua_pushnumber(L,m);
  lua_nodelib_push(L);
  return 1;
}

/* (Deep) copy a node */

static int
lua_nodelib_copy(lua_State *L) {
  halfword *n;
  halfword m;
  if (lua_isnil(L,1))
    return 1; /* the nil itself */
  n = check_isnode(L,1);
  m = copy_node(*n);
  lua_pushnumber(L,m);
  lua_nodelib_push(L);
  return 1;
}


/* build a hbox */

static int
lua_nodelib_hpack(lua_State *L) {
  halfword n, p;
  char *s;
  integer w  = 0;
  int m = 1;
  n = *(check_isnode(L,1));
  if (lua_gettop(L)>1) {
    w = lua_tointeger(L,2);
    if (lua_gettop(L)>2 && lua_type(L,3)==LUA_TSTRING) {
      s = (char *)lua_tostring(L,3);
      if (strcmp(s,"additional")==0) {
	m = 1;
      } else {
	m = lua_tonumber(L,3);
      }
    }
  }
  p = hpack(n,w,m);
  lua_pushnumber(L,p);
  lua_nodelib_push(L);
  return 1;
}

/* This function is similar to |get_node_type_id|, for field
   identifiers.  It has to do some more work, because not all
   identifiers are valid for all types of nodes.
*/

/* this inlining is an optimisation trick. it would be even faster to
   compare string pointers on the lua stack, but that would require a
   lot of code reworking that I don't have time for right now.
*/

#define TEST2(a,b) (*s==a && *(s+1)==b && *(s+2) == 0)

#define TEST4(a,b,c,d)						\
  (*s==a && *(s+1)==b && *(s+2)==c && *(s+3)==d && *(s+4)==0)

static int
get_node_field_id (lua_State *L, int n, int node ) {
  int j;
  char **fields;
  char *s = NULL;
  int i = -2;
  int t = type(node);
  if (lua_type(L,n)==LUA_TSTRING) {
    s = (char *)lua_tostring(L,n);
    if      (TEST2('i','d'))                     { i = 1;  }
    else if (TEST4('n','e','x','t'))             { i = 0;  } 
    else if (t==glyph_node) {
      if      (TEST4('c','h','a','r'))           { i = 4;  } /* char */
      else if (TEST4('f','o','n','t'))           { i = 5;  } /* font */
      else if (TEST4('a','t','t','r'))           { i = 3;  } /* attr */
      else if (TEST4('p','r','e','v'))           { i = -1; } /* prev */
      else if (TEST4('l','a','n','g'))           { i = 6; }  /* lang */
      else if (strcmp(s,"subtype") == 0 )        { i = 2;  } /* subtype */
      else if (TEST4('l','e','f','t'))           { i = 7; }  /* left */
      else if (strcmp(s,"right") == 0 )          { i = 8;  } /* right */
      else if (strcmp(s,"uchyph") == 0 )         { i = 9;  } /* uchyph */
      else if (strcmp(s,"components") == 0 )     { i = 10;  } /* components */
      else if (strcmp(s,"xoffset") == 0 )        { i = 11;  } /* yoffset */
      else if (strcmp(s,"yoffset") == 0 )        { i = 12;  } /* yoffset */
    }
    else if (TEST4('p','r','e','v'))             { i = -1; }
    else if (strcmp(s,"subtype") == 0)           { i = 2;  }
    else {
      if (t==whatsit_node) {
	fields = whatsit_node_data[subtype(node)].fields;
      } else {
	fields = node_data[t].fields;
      }
      for (j=0;fields[j]!=NULL;j++) {
	if (strcmp(s,fields[j])==0) {
	  i=j+3;
	  break;
	}
      }
      if (fields[j]==NULL)
	i=-2;
    }
  } else if (lua_isnumber(L,n)) {
    /* TODO do some test here as well !*/
    i = lua_tointeger(L,n);
  }
  return i;
}


static int
get_valid_node_field_id (lua_State *L, int n, int node ) {
  char *s;
  int i = get_node_field_id(L,n,node);
  if (i==-2) {
    if (lua_type(L,n)==LUA_TSTRING) {
      s = (char *)lua_tostring(L,n); 
      lua_pushfstring(L, "Invalid field id %s for node type %s (%d)" , s, node_data[type(node)].name,subtype(node));
    } else {
      lua_pushfstring(L, "Invalid field id %d for node type %s (%d)" , i, node_data[type(node)].name,subtype(node));
    }
    lua_error(L);
  }
  return i;
}

static int
lua_nodelib_has_field (lua_State *L) {
  int i= -2;
  if (!lua_isnil(L,1)) {
    i = get_node_field_id(L,2,*(check_isnode(L,1)));
  }
  lua_pushboolean(L,(i!=-2));
  return 1;
}


/* fetch the list of valid node types */

static int
do_lua_nodelib_types (lua_State *L, node_info *data) {
  int i;
  lua_newtable(L);
  for (i=0;data[i].id!=-1;i++) {
    lua_pushstring(L,data[i].name);
    lua_rawseti(L,-2,data[i].id);
  }
  return 1;
}

static int 
lua_nodelib_types (lua_State *L) {
  return do_lua_nodelib_types (L, node_data);
}

static int 
lua_nodelib_whatsits (lua_State *L) {
  return do_lua_nodelib_types (L, whatsit_node_data);
}


/* fetch the list of valid fields */

static int
lua_nodelib_fields (lua_State *L) {
  int i = -1;
  char ** fields;
  int t = get_valid_node_type_id(L,1);
  if (t==whatsit_node ) {
    t = get_valid_node_subtype_id(L,2);
    fields = whatsit_node_data[t].fields;
  } else {
    fields = node_data[t].fields;
  }
  lua_checkstack(L,2);
  lua_newtable(L);
  lua_pushstring(L,"prev");
  lua_rawseti(L,-2,-1);
  lua_pushstring(L,"next");
  lua_rawseti(L,-2,0);
  lua_pushstring(L,"id");
  lua_rawseti(L,-2,1);
  lua_pushstring(L,"subtype");
  lua_rawseti(L,-2,2);
  for (i=0;fields[i]!=NULL;i++) {
    lua_pushstring(L,fields[i]);
    lua_rawseti(L,-2,(i+3));
  }
  return 1;
}

/* find the end of a list */

static int
lua_nodelib_tail (lua_State *L) {
  halfword *n;
  halfword t;
  if (lua_isnil(L,1))
    return 1; /* the nil itself */
  n = check_isnode(L,1);
  t=*n;
  if (t==null)
    return 1; /* the old userdata */
  alink(t) = null;
  while (vlink(t)!=null) {
    alink(vlink(t)) = t;
    t = vlink(t);
  }
  lua_pushnumber(L,t);
  lua_nodelib_push(L);
  return 1;
}

/* a few utility functions for attribute stuff */

static int
lua_nodelib_has_attribute (lua_State *L) {
  halfword *n;
  int i, val ;
  if (lua_isnil(L,1))
    return 1; /* the nil itself */
  n = check_isnode(L,1);
  i = lua_tointeger(L,2);
  val = luaL_optinteger(L,3,-1);
  if((val = has_attribute(*n,i,val))>=0) {
    lua_pushnumber(L,val);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int
lua_nodelib_set_attribute (lua_State *L) {
  halfword *n;
  int i, val;
  if (lua_gettop(L)==3) {
    i = lua_tointeger(L,2);
    val = lua_tointeger(L,3);
    n = check_isnode(L,1);
    if (val<0) {
      (void)unset_attribute(*n,i,val);
    } else {
      set_attribute(*n,i,val);
    }
  } else {
    lua_pushstring(L,"incorrect number of arguments");
    lua_error(L);
  }
  return 0;
}


static int
lua_nodelib_unset_attribute (lua_State *L) {
  halfword *n;
  int i, val, ret;
  if (lua_gettop(L)<=3) {
    i = luaL_checknumber(L,2);
    val = luaL_optnumber(L,3,-1); 
    n = check_isnode(L,1);
    ret = unset_attribute(*n,i,val);
    if(ret>=0) {
      lua_pushnumber(L,ret);
    } else {
      lua_pushnil(L);
    }
    return 1;
  } else {
    lua_pushstring(L,"incorrect number of arguments");
    return lua_error(L);
  }
}


/* iteration */

static int nodelib_aux_nil (lua_State *L) {
  lua_pushnil(L);
  return 1;
}

static int nodelib_aux_next (lua_State *L) {
  halfword *n;
  halfword t; /* traverser */
  halfword m; /* match */
  int i; /* id */
  m = lua_tointeger(L,lua_upvalueindex(1));
  i = lua_tointeger(L,lua_upvalueindex(2));
  if (lua_isnil(L,2)) { /* first call */
    n = check_isnode(L,1);
    t = *n;
  } else {
    n = check_isnode(L,2);
    if (vlink(*n)==m) {
      lua_pushnil(L);
      return 1;
    } 
    t = vlink(*n);
  }

  if (i==-1) {
    lua_pushnumber(L,t);
    lua_nodelib_push(L);
  } else {
    while (t!=null && t!=m && type(t)!=i) { t = vlink(t); }
    if (t==m) {
      lua_pushnil(L);
    } else {
      lua_pushnumber(L,t);
      lua_nodelib_push(L);    
    }
  }
  return 1;
}


static int
do_lua_nodelib_traverse (lua_State *L, halfword match, int i, halfword first) {
  /* first upvalue: match */
  lua_pushnumber(L, match);
  /* second upvalue: filter */
  lua_pushnumber(L, i); 
  lua_pushcclosure(L, nodelib_aux_next, 2);
  lua_pushnumber(L, first);
  lua_nodelib_push(L);
  lua_pushnil(L);
  return 3;
}


static int lua_nodelib_traverse (lua_State *L) {
  halfword n;
  halfword m = null;
  if (lua_isnil(L,1)) {
    lua_pushcclosure(L, nodelib_aux_nil, 0);
    return 1;
  }
  n = *(check_isnode(L,1));
  if (lua_gettop(L)==2) {
    m = *(check_isnode(L,2));
  } 
  return do_lua_nodelib_traverse(L,m,-1,n);
}


static int lua_nodelib_traverse_filtered (lua_State *L) {
  halfword n;
  halfword m = null;
  int i = -1;
  i = lua_tointeger(L,1);
  if (lua_isnil(L,2)) {
    lua_pushcclosure(L, nodelib_aux_nil, 0);
    return 1;
  }
  n = *(check_isnode(L,2));
  if (lua_gettop(L)==3)
    m = *(check_isnode(L,3));
  return do_lua_nodelib_traverse(L,m,i,n);
}



static int
do_lua_nodelib_count (lua_State *L, halfword match, int i, halfword first) {
  int count  = 0;
  int t = first;
  while (t!=match) {
    if (i<0 || type(t)==i) { count++;  }
    t = vlink(t);
  }
  lua_pushnumber(L,count);
  return 1;
}

static int lua_nodelib_length (lua_State *L) {
  halfword n;
  halfword m = null;
  if (lua_isnil(L,1)) {
    lua_pushnumber(L,0);
    return 1;
  }
  n = *(check_isnode(L,1));
  if (lua_gettop(L)==2) {
    m = *(check_isnode(L,2));
  } 
  return do_lua_nodelib_count(L,m,-1,n);
}


static int lua_nodelib_count (lua_State *L) {
  halfword n;
  halfword m = null;
  int i = -1;
  i = lua_tointeger(L,1);
  if (lua_isnil(L,2)) {
    lua_pushnumber(L,0);
    return 1;
  }
  n = *(check_isnode(L,2));
  if (lua_gettop(L)==3)
    m = *(check_isnode(L,3));
  return do_lua_nodelib_count(L,m,i,n);
}

/* fetching a field from a node */

#define nodelib_pushlist(L,n) { lua_pushnumber(L,n); lua_nodelib_push(L); }
#define nodelib_pushattr(L,n) { lua_pushnumber(L,n); lua_nodelib_push(L); }
#define nodelib_pushspec(L,n) { lua_pushnumber(L,n); lua_nodelib_push(L); }
#define nodelib_pushaction(L,n) { lua_pushnumber(L,n); lua_nodelib_push(L); }
#define nodelib_pushstring(L,n) { lua_pushstring(L,makecstring(n)); }

static void
lua_nodelib_getfield_whatsit  (lua_State *L, int n, int field) {
  if (field==2) {
    lua_pushnumber(L,subtype(n));
  } else if (field==3){
    nodelib_pushattr(L,node_attr(n));
  } else {
  switch (subtype(n)) {
  case open_node:               
    switch (field) {
    case  4: lua_pushnumber(L,write_stream(n));              break;
    case  5: nodelib_pushstring(L,open_name(n));             break;
    case  6: nodelib_pushstring(L,open_area(n));             break;
    case  7: nodelib_pushstring(L,open_ext(n));              break;
    default: lua_pushnil(L); 
    }
    break;
  case write_node:              
    switch (field) {
    case  4: lua_pushnumber(L,write_stream(n));              break;
    case  5: tokenlist_to_lua(L,write_tokens(n));            break;
    default: lua_pushnil(L); 
    }
    break;
  case close_node:              
    switch (field) {
    case  4: lua_pushnumber(L,write_stream(n));              break;
    default: lua_pushnil(L); 
    }
    break;
  case special_node:            
    switch (field) {
    case  4: tokenlist_to_luastring(L,write_tokens(n));      break;
    default: lua_pushnil(L); 
    }
    break;
  case local_par_node:          
    switch (field) {
    case  4: lua_pushnumber(L,local_pen_inter(n));           break;
    case  5: lua_pushnumber(L,local_pen_broken(n));          break;
    case  6: lua_pushnumber(L,local_par_dir(n));             break;
    case  7: nodelib_pushlist(L,local_box_left(n));          break;
    case  8: lua_pushnumber(L,local_box_left_width(n));      break;
    case  9: nodelib_pushlist(L,local_box_right(n));         break;
    case 10: lua_pushnumber(L,local_box_right_width(n));     break;
    default: lua_pushnil(L); 
    }
    break;
  case dir_node:                
    switch (field) {
    case  4: lua_pushnumber(L,dir_dir(n));                   break;
    case  5: lua_pushnumber(L,dir_level(n));                 break;
    case  6: lua_pushnumber(L,dir_dvi_ptr(n));               break;
    case  7: lua_pushnumber(L,dir_dvi_h(n));                 break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_literal_node:
    switch (field) {
    case  4: lua_pushnumber(L,pdf_literal_mode(n));          break;
    case  5: tokenlist_to_luastring(L,pdf_literal_data(n));  break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_refobj_node:         
    switch (field) {
    case  4: lua_pushnumber(L,pdf_obj_objnum(n));            break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_refxform_node:       
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_xform_objnum(n));          break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_refximage_node:      
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_ximage_objnum(n));         break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_annot_node:          
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_annot_objnum(n));          break;
    case  8: tokenlist_to_luastring(L,pdf_annot_data(n));    break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_start_link_node:     
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_link_objnum(n));           break;
    case  8: tokenlist_to_luastring(L,pdf_link_attr(n));     break;
    case  9: nodelib_pushaction(L,pdf_link_action(n));       break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_dest_node:           
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_dest_named_id(n));         break;
    case  8: if (pdf_dest_named_id(n) == 1)
	tokenlist_to_luastring(L,pdf_dest_id(n));
      else 
	lua_pushnumber(L,pdf_dest_id(n));                    break;
    case  9: lua_pushnumber(L,pdf_dest_type(n));             break;
    case 10: lua_pushnumber(L,pdf_dest_xyz_zoom(n));         break;
    case 11: lua_pushnumber(L,pdf_dest_objnum(n));           break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_thread_node:         
  case pdf_start_thread_node:   
    switch (field) {
    case  4: lua_pushnumber(L,pdf_width(n));                 break;
    case  5: lua_pushnumber(L,pdf_height(n));                break;
    case  6: lua_pushnumber(L,pdf_depth(n));                 break;
    case  7: lua_pushnumber(L,pdf_thread_named_id(n));       break;
    case  8: if (pdf_thread_named_id(n) == 1)
	tokenlist_to_luastring(L,pdf_thread_id(n));
      else 
	lua_pushnumber(L,pdf_thread_id(n));                  break;
    case  9: tokenlist_to_luastring(L,pdf_thread_attr(n));   break;
    default: lua_pushnil(L); 
    }
    break;
  case late_lua_node:           
    switch (field) {
    case  4: lua_pushnumber(L,late_lua_reg(n));              break;
    case  5: tokenlist_to_luastring(L,late_lua_data(n));     break;
    default: lua_pushnil(L); 
    }
    break;
  case close_lua_node:          
    switch (field) {
    case  4: lua_pushnumber(L,late_lua_reg(n));              break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_colorstack_node:     
    switch (field) {
    case  4: lua_pushnumber(L,pdf_colorstack_stack(n));      break;
    case  5: lua_pushnumber(L,pdf_colorstack_cmd(n));        break;
    case  6: tokenlist_to_luastring(L,pdf_colorstack_data(n)); break;
    default: lua_pushnil(L); 
    }
    break;
  case pdf_setmatrix_node:      
    switch (field) {
    case  4: tokenlist_to_luastring(L,pdf_setmatrix_data(n)); break;
    default: lua_pushnil(L); 
    }
    break;
  case user_defined_node:       
    switch (field) {
    case  4: lua_pushnumber(L,user_node_id(n));	             break;
    case  5: lua_pushnumber(L,user_node_type(n));            break;
    case  6: 
      switch (user_node_type(n)) {
      case 'd': lua_pushnumber(L,user_node_value(n));     break;
      case 'n': nodelib_pushlist(L,user_node_value(n));   break;
      case 's': nodelib_pushstring(L,user_node_value(n)); break;
      case 't': tokenlist_to_lua(L,user_node_value(n));   break;
      default: lua_pushnumber(L,user_node_value(n));      break;
      }                                                      break;
    default: lua_pushnil(L); 
    }
    break;
  default:
    lua_pushnil(L); 
    break;
  }
  }
}


static int
lua_nodelib_getfield  (lua_State *L) {
  halfword *n_ptr, n;
  int field;
  if (lua_isnil(L,1))
    return 1; /* a nil */
  n_ptr = check_isnode(L,1);
  n = *n_ptr;
  field = get_valid_node_field_id(L,2, n);

  if (field<-1)
    return 0;
  if (field==0) {
    lua_pushnumber(L,vlink(n));
    lua_nodelib_push(L);
  } else if (field==1) {
    lua_pushnumber(L,type(n));
  } else if (field==-1) {
    lua_pushnumber(L,alink(n));
    lua_nodelib_push(L);
  } else {
    switch (type(n)) {
    case hlist_node:
    case vlist_node:
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,width(n));	      break;
      case  5: lua_pushnumber(L,depth(n));	      break;
      case  6: lua_pushnumber(L,height(n));	      break;
      case  7: lua_pushnumber(L,box_dir(n));          break;
      case  8: lua_pushnumber(L,shift_amount(n));     break;
      case  9: lua_pushnumber(L,glue_order(n));       break;
      case 10: lua_pushnumber(L,glue_sign(n));        break;
      case 11: lua_pushnumber(L,(double)glue_set(n)); break;
      case 12: nodelib_pushlist(L,list_ptr(n));       break;
      default: lua_pushnil(L);
      }
      break;
    case unset_node:
      switch (field) {
      case  2: lua_pushnumber(L,0);	              break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,width(n));	      break;
      case  5: lua_pushnumber(L,depth(n));	      break;
      case  6: lua_pushnumber(L,height(n));	      break;
      case  7: lua_pushnumber(L,box_dir(n));          break;
      case  8: lua_pushnumber(L,glue_shrink(n));      break;
      case  9: lua_pushnumber(L,glue_order(n));       break;
      case 10: lua_pushnumber(L,glue_sign(n));        break;
      case 11: lua_pushnumber(L,glue_stretch(n));     break;
      case 12: lua_pushnumber(L,span_count(n));	      break;
      case 13: nodelib_pushlist(L,list_ptr(n));       break;
      default: lua_pushnil(L);
      }
      break;
    case rule_node:
      switch (field) {
      case  2: lua_pushnumber(L,0);	              break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,width(n));	      break;
      case  5: lua_pushnumber(L,depth(n));	      break;
      case  6: lua_pushnumber(L,height(n));	      break;
      case  7: lua_pushnumber(L,rule_dir(n));         break;
      default: lua_pushnil(L);
      }
      break;
    case ins_node:
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,float_cost(n));	      break;
      case  5: lua_pushnumber(L,depth(n));	      break;
      case  6: lua_pushnumber(L,height(n));	      break;
      case  7: nodelib_pushspec(L,split_top_ptr(n));  break;
      case  8: nodelib_pushlist(L,ins_ptr(n));        break;
      default: lua_pushnil(L);
      }
      break;
    case mark_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,mark_class(n));	      break;
      case  5: tokenlist_to_lua(L,mark_ptr(n));       break;
      default: lua_pushnil(L);
      }
      break;
    case adjust_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: nodelib_pushlist(L,adjust_ptr(n));     break;
      default: lua_pushnil(L);
      }
      break;
    case disc_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));             break;
      case  3: nodelib_pushattr(L,node_attr(n));         break;
      case  4: nodelib_pushlist(L,vlink(pre_break(n)));  break;
      case  5: nodelib_pushlist(L,vlink(post_break(n))); break;
      case  6: nodelib_pushlist(L,vlink(no_break(n)));   break;
      default: lua_pushnil(L);
      }
      break;
    case math_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,surround(n));	      break;
      default: lua_pushnil(L);
      }
      break;
    case glue_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: nodelib_pushspec(L,glue_ptr(n));       break;
      case  5: nodelib_pushlist(L,leader_ptr(n));     break;
      default: lua_pushnil(L);
      }
      break;
    case glue_spec_node: 
      switch (field) {
      case  2: lua_pushnumber(L,0);	              break;
      case  3: lua_pushnumber(L,width(n));	      break;
      case  4: lua_pushnumber(L,stretch(n));	      break;
      case  5: lua_pushnumber(L,shrink(n));	      break;
      case  6: lua_pushnumber(L,stretch_order(n));    break;
      case  7: lua_pushnumber(L,shrink_order(n));     break;
      case  8: lua_pushnumber(L,glue_ref_count(n));   break;
      default: lua_pushnil(L);
      }
      break;
    case kern_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,width(n));	      break;
      default: lua_pushnil(L);
      }
      break;
    case penalty_node: 
      switch (field) {
      case  2: lua_pushnumber(L,0);	              break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,penalty(n));	      break;
      default: lua_pushnil(L);
      }
      break;
    case glyph_node: 
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,character(n));	      break;
      case  5: lua_pushnumber(L,font(n));	      break;
      case  6: lua_pushnumber(L,char_lang(n));	      break;
      case  7: lua_pushnumber(L,char_lhmin(n));	      break;
      case  8: lua_pushnumber(L,char_rhmin(n));	      break;
      case  9: lua_pushnumber(L,char_uchyph(n));      break;
      case 10: nodelib_pushlist(L,lig_ptr(n));	      break;
      case 11: lua_pushnumber(L,x_displace(n));	      break;
      case 12: lua_pushnumber(L,y_displace(n));	      break;
      default: lua_pushnil(L);
      }
      break;
    case action_node:
      switch (field) {
      case  2: /* dummy subtype */                            break;
      case  3: lua_pushnumber(L,pdf_action_type(n));          break;
      case  4: lua_pushnumber(L,pdf_action_named_id(n));      break;
      case  5: if (pdf_action_named_id(n)==1) {
	  tokenlist_to_luastring(L,pdf_action_id(n));
	} else {
	  lua_pushnumber(L,pdf_action_id(n));
	}                                                     break;
      case  6: tokenlist_to_luastring(L,pdf_action_file(n));  break;
      case  7: lua_pushnumber(L,pdf_action_new_window(n));    break;
      case  8: tokenlist_to_luastring(L,pdf_action_tokens(n));break;
      case  9: lua_pushnumber(L,pdf_action_refcount(n));      break;
      default: lua_pushnil(L);
      }
      break;
    case margin_kern_node :
      switch (field) {
      case  2: lua_pushnumber(L,subtype(n));	      break;
      case  3: nodelib_pushattr(L,node_attr(n));      break;
      case  4: lua_pushnumber(L,width(n));	      break;
      case  5: nodelib_pushlist(L,margin_char(n));    break;
      default: lua_pushnil(L);
      }
      break;
#if 0
    case attribute_list_node :
      switch (field) {
      case  2: lua_pushnumber(L,0);	               break;
      default: lua_pushnil(L);
      }
      break;
    case attribute_node :
      switch (field) {
      case  2: lua_pushnumber(L,0);	              break;
      case  3: lua_pushnumber(L,attribute_id(n));     break;
      case  4: lua_pushnumber(L,attribute_value(n));  break;
      default: lua_pushnil(L);
      }
      break;
#endif
    case whatsit_node:
      lua_nodelib_getfield_whatsit(L,n,field);
      break;
    default: 
      lua_pushnil(L); 
      break;
    }
  }
  return 1;
}


static int nodelib_getlist(lua_State *L, int n) {
  halfword *m;
  if (lua_isuserdata(L,n)) {
    m = check_isnode(L,n);
    return *m;
  } else {
    return null ;
  }
}

#define nodelib_getspec        nodelib_getlist
#define nodelib_getaction      nodelib_getlist
#define nodelib_getstring(L,a) maketexstring(lua_tostring(L,a))
#define nodelib_gettoks(L,a)   tokenlist_from_lua(L)

static void nodelib_setattr (lua_State *L, int stackindex, halfword n) {
  halfword p;
  p = nodelib_getlist(L,stackindex);
  if (node_attr(n) != p) {
    if (node_attr(n)!=null)
      delete_attribute_ref(node_attr(n));
    node_attr(n) = p; 
    attr_list_ref(p)++;
  }
}

static int nodelib_cantset (lua_State *L, int field, int n) {
  lua_pushfstring(L,"You cannot set field %d in a node of type %s",
		  field, node_data[type(n)].name);
  lua_error(L);
  return 0;
}

static int
lua_nodelib_setfield_whatsit(lua_State *L, int n, int field) {
  if (field==3) {
    nodelib_setattr(L,3,n); 
  } else {
  switch (subtype(n)) {
  case open_node:
    switch (field) {
    case  4: write_stream(n) = lua_tointeger(L,3);          break;
    case  5: open_name(n) = nodelib_getstring(L,3);         break;
    case  6: open_area(n) = nodelib_getstring(L,3);         break;
    case  7: open_ext(n)  = nodelib_getstring(L,3);         break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case write_node:              
    switch (field) {
    case  4: write_stream(n) = lua_tointeger(L,3);          break;
    case  5: write_tokens(n) = nodelib_gettoks(L,3);        break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case close_node:              
    switch (field) {
    case  4: write_stream(n) = lua_tointeger(L,3);          break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case special_node:            
    switch (field) {
    case  4: write_tokens(n) = nodelib_gettoks(L,3);        break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case local_par_node:          
    switch (field) {
    case  4: local_pen_inter(n) = lua_tointeger(L,3);       break;
    case  5: local_pen_broken(n) = lua_tointeger(L,3);      break;
    case  6: local_par_dir(n) = lua_tointeger(L,3);         break;
    case  7: local_box_left(n) = nodelib_getlist(L,3);      break;
    case  8: local_box_left_width(n) = lua_tointeger(L,3);  break;
    case  9: local_box_right(n) = nodelib_getlist(L,3);     break;
    case 10: local_box_right_width(n) = lua_tointeger(L,3); break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case dir_node:                
    switch (field) {
    case  4: dir_dir(n) = lua_tointeger(L,3);               break;
    case  5: dir_level(n) = lua_tointeger(L,3);             break;
    case  6: dir_dvi_ptr(n) = lua_tointeger(L,3);           break;
    case  7: dir_dvi_h(n) = lua_tointeger(L,3);             break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_literal_node:
    switch (field) {
    case  4: pdf_literal_mode(n) = lua_tointeger(L,3);      break;
    case  5: pdf_literal_data(n) = nodelib_gettoks(L,3);    break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_refobj_node:         
    switch (field) {
    case  4: pdf_obj_objnum(n) = lua_tointeger(L,3);        break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_refxform_node:       
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_xform_objnum(n) = lua_tointeger(L,3);      break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_refximage_node:      
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_ximage_objnum(n) = lua_tointeger(L,3);     break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_annot_node:          
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_annot_objnum(n) = lua_tointeger(L,3);      break;
    case  8: pdf_annot_data(n) = nodelib_getstring(L,3);    break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_start_link_node:     
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_link_objnum(n) = lua_tointeger(L,3);       break;
    case  8: pdf_link_attr(n) = nodelib_getstring(L,3);     break;
    case  9: pdf_link_action(n) = nodelib_getaction(L,3);   break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_end_link_node:       
    switch (field) {
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_dest_node:           
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_dest_named_id(n) = lua_tointeger(L,3);     break;
    case  8: if (pdf_dest_named_id(n)==1)
	pdf_dest_id(n) = nodelib_gettoks(L,3);
      else
	pdf_dest_id(n) = lua_tointeger(L,3);                break;
    case  9: pdf_dest_type(n) = lua_tointeger(L,3);         break;
    case 10: pdf_dest_xyz_zoom(n) = lua_tointeger(L,3);     break;
    case 11: pdf_dest_objnum(n) = lua_tointeger(L,3);       break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_thread_node:         
  case pdf_start_thread_node:   
    switch (field) {
    case  4: pdf_width(n) = lua_tointeger(L,3);             break;
    case  5: pdf_height(n) = lua_tointeger(L,3);            break;
    case  6: pdf_depth(n) = lua_tointeger(L,3);             break;
    case  7: pdf_thread_named_id(n) = lua_tointeger(L,3);   break;
    case  8: if (pdf_thread_named_id(n)==1)
	pdf_thread_id(n) = nodelib_gettoks(L,3);
      else
	pdf_thread_id(n) = lua_tointeger(L,3);              break;
    case  9: pdf_thread_attr(n) = nodelib_gettoks(L,3);     break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_end_thread_node:     
  case pdf_save_pos_node:       
    return nodelib_cantset(L,field,n);
    break;
  case late_lua_node:           
    switch (field) {
    case  4: late_lua_reg(n) = lua_tointeger(L,3);          break;
    case  5: late_lua_data(n) = nodelib_gettoks(L,3);       break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case close_lua_node:          
    switch (field) {
    case  4: late_lua_reg(n) = lua_tointeger(L,3);          break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_colorstack_node:     
    switch (field) {
    case  4: pdf_colorstack_stack(n) = lua_tointeger(L,3);  break;
    case  5: pdf_colorstack_cmd(n) = lua_tointeger(L,3);    break;
    case  6: pdf_colorstack_data(n) = nodelib_gettoks(L,3); break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_setmatrix_node:      
    switch (field) {
    case  4: pdf_setmatrix_data(n) = nodelib_gettoks(L,3);  break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  case pdf_save_node:           
  case pdf_restore_node:        
  case cancel_boundary_node:        
    return nodelib_cantset(L,field,n);
    break;
  case user_defined_node:       
    switch (field) {
    case  4: user_node_id(n) = lua_tointeger(L,3);          break;
    case  5: user_node_type(n) = lua_tointeger(L,3);        break;
    case  6: 
      switch(user_node_type(n)) {
      case 'd': user_node_value(n) = lua_tointeger(L,3); break;
      case 'n': user_node_value(n) = nodelib_getlist(L,3); break;
      case 's': user_node_value(n) = nodelib_getstring(L,3); break;
      case 't': user_node_value(n) = nodelib_gettoks(L,3); break;
      default: user_node_value(n) = lua_tointeger(L,3); break;
      }                                                     break;
    default: return nodelib_cantset(L,field,n);
    }
    break;
  default:
    /* do nothing */
    break;
  }
  }
  return 0;
}

static int
lua_nodelib_setfield  (lua_State *L) {
  halfword *n_ptr;
  halfword n;
  int field;
  n_ptr = check_isnode(L,1);
  n = *n_ptr;
  field = get_valid_node_field_id(L,2,n);
  if (field<-1)
    return 0;
  if (field==0) {
    vlink(n) = nodelib_getlist(L,3);
  } else if (field==-1) {
    alink(n) = nodelib_getlist(L,3);
  } else {
    switch (type(n)) {
    case hlist_node:
    case vlist_node:
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: width(n) = lua_tointeger(L,3);	        break;
      case  5: depth(n) = lua_tointeger(L,3);	        break;
      case  6: height(n) = lua_tointeger(L,3);	        break;
      case  7: box_dir(n) = lua_tointeger(L,3);         break;
      case  8: shift_amount(n) = lua_tointeger(L,3);    break;
      case  9: glue_order(n) = lua_tointeger(L,3);      break;
      case 10: glue_sign(n) = lua_tointeger(L,3);       break;
      case 11: glue_set(n) = (double)lua_tonumber(L,3); break;
      case 12: list_ptr(n) = nodelib_getlist(L,3);      break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case unset_node:
      switch (field) {
      case  2: /* dummy subtype */                      break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: width(n) = lua_tointeger(L,3);	        break;
      case  5: depth(n) = lua_tointeger(L,3);	        break;
      case  6: height(n) = lua_tointeger(L,3);	        break;
      case  7: box_dir(n) = lua_tointeger(L,3);         break;
      case  8: glue_shrink(n) = lua_tointeger(L,3);     break;
      case  9: glue_order(n) = lua_tointeger(L,3);      break;
      case 10: glue_sign(n) = lua_tointeger(L,3);       break;
      case 11: glue_stretch(n) = lua_tointeger(L,3);    break;
      case 12: span_count(n) = lua_tointeger(L,3);	break;
      case 13: list_ptr(n) = nodelib_getlist(L,3);      break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case rule_node:
      switch (field) {
      case  2: /* dummy subtype */                      break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: width(n) = lua_tointeger(L,3);	        break;
      case  5: depth(n) = lua_tointeger(L,3);	        break;
      case  6: height(n) = lua_tointeger(L,3);	        break;
      case  7: rule_dir(n) = lua_tointeger(L,3);        break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case ins_node:
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: float_cost(n) = lua_tointeger(L,3);	break;
      case  5: depth(n) = lua_tointeger(L,3);	        break;
      case  6: height(n) = lua_tointeger(L,3);	        break;
      case  7: split_top_ptr(n) = nodelib_getspec(L,3); break;
      case  8: ins_ptr(n) = nodelib_getlist(L,3);       break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case mark_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: mark_class(n) = lua_tointeger(L,3);	break;
      case  5: mark_ptr(n) = nodelib_gettoks(L,3);      break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case adjust_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: adjust_ptr(n) = nodelib_getlist(L,3);    break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case disc_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	            break;
      case  3: nodelib_setattr(L,3,n);                      break;
      case  4: vlink(pre_break(n)) = nodelib_getlist(L,3);  break;
      case  5: vlink(post_break(n)) = nodelib_getlist(L,3); break;
      case  6: vlink(no_break(n)) = nodelib_getlist(L,3);   break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case math_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: surround(n) = lua_tointeger(L,3);        break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case glue_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);         break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: glue_ptr(n) = nodelib_getspec(L,3);      break;
      case  5: leader_ptr(n) = nodelib_getlist(L,3);    break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case glue_spec_node: 
      switch (field) {
      case  2: /* dummy subtype */                      break;
      case  3: width(n) = lua_tointeger(L,3);	        break;
      case  4: stretch(n) = lua_tointeger(L,3);	        break;
      case  5: shrink(n) = lua_tointeger(L,3);	        break;
      case  6: stretch_order(n) = lua_tointeger(L,3);	break;
      case  7: shrink_order(n) = lua_tointeger(L,3);	break;
      case  8: glue_ref_count(n) = lua_tointeger(L,3);  break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case kern_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: width(n) = lua_tointeger(L,3);           break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case penalty_node: 
      switch (field) {
      case  2: /* dummy subtype */                      break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: penalty(n) = lua_tointeger(L,3);         break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case glyph_node: 
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: character(n) = lua_tointeger(L,3);       break;
      case  5: font(n) = lua_tointeger(L,3);	        break;
      case  6: set_char_lang(n,lua_tointeger(L,3));     break;
      case  7: set_char_lhmin(n,lua_tointeger(L,3));    break;
      case  8: set_char_rhmin(n,lua_tointeger(L,3));    break;
      case  9: set_char_uchyph(n,lua_tointeger(L,3));   break;
      case 10: lig_ptr(n) = nodelib_getlist(L,3);       break;
      case 11: x_displace(n) = lua_tointeger(L,3);      break;
      case 12: y_displace(n) = lua_tointeger(L,3);      break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case action_node:
      switch (field) {
      case  2: /* dummy subtype */                            break;
      case  3: pdf_action_type(n)     = lua_tointeger(L,3);   break;
      case  4: pdf_action_named_id(n) = lua_tointeger(L,3);   break;
      case  5: if (pdf_action_named_id(n)==1) {
	  pdf_action_id(n) = nodelib_gettoks(L,3);         
	} else {
	  pdf_action_id(n) = lua_tointeger(L,3);         
	}                                                     break;
      case  6: pdf_action_file(n) =  nodelib_gettoks(L,3);    break;
      case  7: pdf_action_new_window(n) = lua_tointeger(L,3); break;
      case  8: pdf_action_tokens(n) =  nodelib_gettoks(L,3);  break;
      case  9: pdf_action_refcount(n) = lua_tointeger(L,3);   break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case margin_kern_node:
      switch (field) {
      case  2: subtype(n) = lua_tointeger(L,3);	        break;
      case  3: nodelib_setattr(L,3,n);                  break;
      case  4: width(n) = lua_tointeger(L,3);           break;
      case  5: margin_char(n) = nodelib_getlist(L,3);   break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
#if 0
    case attribute_list_node:
      switch (field) {
      case  2: /* dummy subtype */                      break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
    case attribute_node:
      switch (field) {
      case  2: /* dummy subtype */                       break;
      case  3: attribute_id(n) = lua_tointeger(L,3);     break;
      case  4: attribute_value(n) = lua_tointeger(L,3);  break;
      default: return nodelib_cantset(L,field,n);
      }
      break;
#endif
    case whatsit_node:
      lua_nodelib_setfield_whatsit(L,n,field);
      break;
    default:
      /* do nothing */
      break;
    }
  }
  return 0;
}

static int
lua_nodelib_print  (lua_State *L) {
  char *msg;
  char a[7] = {' ',' ',' ', 'n', 'i', 'l', 0};
  char v[7] = {' ',' ',' ', 'n', 'i', 'l', 0};
  halfword *n;
  n = check_isnode(L,1);
  msg = xmalloc(256);
  if (alink(*n)!=null) 
    snprintf(a,7,"%6d",(int)alink(*n));
  if (vlink(*n)!=null) 
    snprintf(v,7,"%6d",(int)vlink(*n));
  snprintf(msg,255,"<node %s < %6d > %s : %s %d>", a, (int)*n, v, node_data[type(*n)].name, subtype(*n));
  lua_pushstring(L,msg);
  free(msg);
  return 1;
}


static int
lua_nodelib_equal  (lua_State *L) {
  halfword n, m;
  n = *(check_isnode(L,1));
  m = *(check_isnode(L,2));
  lua_pushboolean(L,(n==m));
  return 1;
}

static int 
font_tex_ligaturing (lua_State *L) {
  /* on the stack are two nodes and a direction */
  halfword tmp_head;
  halfword *h;
  halfword t =null;
  if (lua_gettop(L)<1) {
	lua_pushnil(L);
    lua_pushboolean(L,0);
    return 2;
  } 
  h = check_isnode(L,1);
  if (lua_gettop(L)>1) {
	t = *(check_isnode(L,2));
  }
  tmp_head = new_node(nesting_node,1);
  couple_nodes(tmp_head,*h);
  tlink(tmp_head)=t;
  t = handle_ligaturing(tmp_head,t);
  lua_pushnumber(L,vlink(tmp_head));
  flush_node(tmp_head);
  lua_nodelib_push(L);  
  lua_pushnumber(L,t);
  lua_nodelib_push(L);  
  lua_pushboolean(L,1);
  return 3;
}

static int 
font_tex_kerning (lua_State *L) {
  /* on the stack are two nodes and a direction */

  halfword tmp_head;
  halfword *h;
  halfword t =null;
  if (lua_gettop(L)<1) {
    lua_pushnil(L);
    lua_pushboolean(L,0);
    return 2;
  } 
  h = check_isnode(L,1);
  if (lua_gettop(L)>1) {
    t = *(check_isnode(L,2));
  }
  tmp_head = new_node(nesting_node,1);
  couple_nodes(tmp_head,*h);
  tlink(tmp_head)=t;
  t = handle_kerning(tmp_head,t);
  lua_pushnumber(L,vlink(tmp_head));
  flush_node(tmp_head);
  lua_nodelib_push(L);    
  lua_pushnumber(L,t);
  lua_nodelib_push(L);    
  lua_pushboolean(L,1);
  return 3;
}


static int 
lua_nodelib_first_character (lua_State *L) {
  /* on the stack are two nodes and a direction */
  halfword h, savetail = null, t = null;
  if (lua_gettop(L)<1) {
    lua_pushnil(L);
    lua_pushboolean(L,0);
    return 2;
  } 
  h = *(check_isnode(L,1));
  if (lua_gettop(L)>1) {
    t = *(check_isnode(L,2));
    savetail = vlink(t);
    vlink(t) = null;
  }
  while (h!=null && !is_simple_character(h)) {
    h = vlink(h);
  }
  if (savetail!=null) { vlink(t) = savetail; }
  lua_pushnumber(L,h);
  lua_nodelib_push(L);
  lua_pushboolean(L,(h==null? 0 : 1));
  return 2;
}


/* this is too simplistic, but it helps Hans to get going */

halfword
do_ligature_n  (halfword prev, halfword stop, halfword lig) {
  vlink(lig) = vlink(stop);
  vlink(stop) = null;
  lig_ptr(lig) = vlink(prev);
  vlink(prev) = lig;
  return lig;
}



/* node.do_ligature_n(node prev, node last, node lig) */
static int
lua_nodelib_do_ligature_n  (lua_State *L) {
  halfword n, m, o, p, tmp_head;
  n = *(check_isnode(L,1));
  m = *(check_isnode(L,2));
  o = *(check_isnode(L,3));
  if (alink(n)==null || vlink(alink(n))!=n) {
    tmp_head = new_node(temp_node,0);
    couple_nodes(tmp_head,n);
    p = do_ligature_n(tmp_head,m,o);
    flush_node(tmp_head);
  } else {
    p = do_ligature_n(alink(n),m,o);
  }
  lua_pushnumber(L,p);
  lua_nodelib_push(L);
  return 1;
}


static const struct luaL_reg nodelib_f [] = {
  {"id",            lua_nodelib_id},
  {"subtype",       lua_nodelib_subtype},
  {"type",          lua_nodelib_type},
  {"new",           lua_nodelib_new},
  {"length",        lua_nodelib_length},
  {"count",         lua_nodelib_count},
  {"traverse",      lua_nodelib_traverse},
  {"traverse_id",   lua_nodelib_traverse_filtered},
  {"slide",         lua_nodelib_tail},
  {"types",         lua_nodelib_types},
  {"whatsits",      lua_nodelib_whatsits},
  {"fields",        lua_nodelib_fields},
  {"has_field",     lua_nodelib_has_field},
  {"free",          lua_nodelib_free},
  {"flush_list",    lua_nodelib_flush_list},
  {"remove",        lua_nodelib_remove},
  {"insert_before", lua_nodelib_insert_before},
  {"insert_after",  lua_nodelib_insert_after},
  {"copy",          lua_nodelib_copy},
  {"copy_list",     lua_nodelib_copy_list},
  {"hpack",         lua_nodelib_hpack},
  {"has_attribute", lua_nodelib_has_attribute},
  {"set_attribute", lua_nodelib_set_attribute},
  {"unset_attribute", lua_nodelib_unset_attribute},
  {"do_ligature_n",   lua_nodelib_do_ligature_n},
  {"ligaturing",      font_tex_ligaturing},
  {"kerning",         font_tex_kerning},
  {"first_character", lua_nodelib_first_character},
  {NULL, NULL}  /* sentinel */
};

static const struct luaL_reg nodelib_m [] = {
  {"__index",    lua_nodelib_getfield},
  {"__newindex", lua_nodelib_setfield},
  {"__tostring", lua_nodelib_print},
  {"__eq",       lua_nodelib_equal},
  {NULL, NULL}  /* sentinel */
};



int 
luaopen_node (lua_State *L) 
{
  luaL_newmetatable(L,NODE_METATABLE);
  luaL_register(L, NULL, nodelib_m);
  luaL_register(L, "node", nodelib_f);
  return 1;
}

void 
nodelist_to_lua (lua_State *L, int n) { 
  lua_pushnumber(L,n);
  lua_nodelib_push(L);
}

int
nodelist_from_lua (lua_State *L) { 
  halfword *n;
  if (lua_isnil(L,-1))
    return null;
  n = check_isnode(L,-1);
  return *n;
}

