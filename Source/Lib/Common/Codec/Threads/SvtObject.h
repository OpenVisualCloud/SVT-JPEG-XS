/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/
#ifndef _SVT_OBJECT_H_
#define _SVT_OBJECT_H_
#include <stdlib.h>

#include "SvtMalloc.h"

/*typical usage like this.

  typedef A {
      DctorCall dctor;
      //...
  };

  a_dctor(void* p)
  {
      A* a = (A*)p;
      //free everything allocated by A;
  }

  a_ctor(A* a)
  {
      //this is only needed if destructor is called.
      a->dctor = a_dctor;
      //construct everything
  }

  A* o;
  SVT_NEW(o, a_ctor);
  //...
  SVT_RELEASE(o);

}
*/

typedef void (*DctorCall)(void* pobj);

#define SVT_DELETE_UNCHECKED(pobj) \
    do {                           \
        if ((pobj)->dctor)         \
            (pobj)->dctor(pobj);   \
        SVT_FREE((pobj));          \
    } while (0)

//trick: to support zero param constructor
#define SVT_VA_ARGS(...) , ##__VA_ARGS__

#define SVT_NO_THROW_NEW(pobj, ctor, ...)                            \
    do {                                                             \
        SvtJxsErrorType_t svt_new_local_err;                         \
        size_t svt_new_local_size = sizeof(*pobj);                   \
        SVT_NO_THROW_CALLOC(pobj, 1, svt_new_local_size);            \
        if (pobj) {                                                  \
            svt_new_local_err = ctor(pobj SVT_VA_ARGS(__VA_ARGS__)); \
            if (svt_new_local_err != SvtJxsErrorNone)                \
                SVT_DELETE_UNCHECKED(pobj);                          \
        }                                                            \
    } while (0)

#define SVT_NEW(pobj, ctor, ...)                                 \
    do {                                                         \
        SvtJxsErrorType_t svt_new_local_err;                     \
        size_t svt_new_local_size = sizeof(*pobj);               \
        SVT_CALLOC(pobj, 1, svt_new_local_size);                 \
        svt_new_local_err = ctor(pobj SVT_VA_ARGS(__VA_ARGS__)); \
        if (svt_new_local_err != SvtJxsErrorNone) {              \
            SVT_DELETE_UNCHECKED(pobj);                          \
            return svt_new_local_err;                            \
        }                                                        \
    } while (0)

#define SVT_DELETE(pobj)                \
    do {                                \
        if (pobj)                       \
            SVT_DELETE_UNCHECKED(pobj); \
    } while (0)

#define SVT_DELETE_PTR_ARRAY(pa, count)   \
    do {                                  \
        uint32_t i;                       \
        if (pa) {                         \
            for (i = 0; i < count; i++) { \
                SVT_DELETE(pa[i]);        \
            }                             \
            SVT_FREE(pa);                 \
        }                                 \
    } while (0)

#undef SVT_DELETE_UNCHECK //do not use this outside
//#undef SVT_VA_ARGS

#endif /*_SVT_OBJECT_H_*/
