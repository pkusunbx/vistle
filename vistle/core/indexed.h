#ifndef INDEXED_H
#define INDEXED_H


#include "scalar.h"
#include "shm.h"
#include "coords.h"
#include "export.h"

namespace vistle {

class  V_COREEXPORT Indexed: public Coords {
   V_OBJECT(Indexed);

 public:
   typedef Coords Base;

   Indexed(const size_t numElements, const size_t numCorners,
         const size_t numVertices,
         const Meta &meta=Meta());

   size_t getNumElements() const;
   size_t getNumCorners() const;
   size_t getNumVertices() const;

   shm<size_t>::vector &cl() const { return *(*d()->cl)(); }
   shm<size_t>::vector &el() const { return *(*d()->el)(); }

   V_DATA_BEGIN(Indexed);
      ShmVector<size_t>::ptr el, cl;

      Data(const size_t numElements = 0, const size_t numCorners = 0,
           const size_t numVertices = 0,
            Type id = UNKNOWN, const std::string &name = "",
            const Meta &meta=Meta());
      static Data *create(Type id = UNKNOWN,
            const size_t numElements = 0, const size_t numCorners = 0,
            const size_t numVertices = 0,
            const Meta &meta=Meta());

   V_DATA_END(Indexed);
};

} // namespace vistle

#ifdef VISTLE_IMPL
#include "indexed_impl.h"
#endif

#endif
