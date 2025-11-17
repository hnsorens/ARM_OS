#ifndef MODULE_H
#define MODULE_H


#define vtable(type) struct type ___table;
#define start(function) \
void function(typeof(___table) *table);   \
typeof(___table)* _entry()                \
{                                         \
  function(&___table);                    \
  return &___table;                       \
}

#endif
