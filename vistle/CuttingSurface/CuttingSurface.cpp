#include <sstream>
#include <iomanip>

#include <omp.h>
#include <google/profiler.h>

#include "object.h"
#include "tables.h"

#include "CuttingSurface.h"

MODULE_MAIN(CuttingSurface)


CuttingSurface::CuttingSurface(int rank, int size, int moduleID)
   : Module("CuttingSurface", rank, size, moduleID) {

   createInputPort("grid_in");
   createInputPort("data_in");

   createOutputPort("grid_out");
   createOutputPort("data_out");

   omp_set_num_threads(4);
}

CuttingSurface::~CuttingSurface() {

}

#define lerp(a, b, t) ( a + t * (b - a) )

typedef struct {
   float x, y, z;
} float3;

const float EPSILON = 1.0e-10f;

inline float3 lerp3(const float3 & a, const float3 & b, const float t) {

   float3 res;
   res.x = lerp(a.x, b.x, t);
   res.y = lerp(a.y, b.y, t);
   res.z = lerp(a.z, b.z, t);
   return res;
}

inline float3 interp(float value, const float3 & p0, const float3 & p1,
                     const float f0, const float f1,
                     const float v0, const float v1, float & v) {

   float diff = (f1 - f0);

   if (fabs(diff) < EPSILON) {
      v = v0;
      return p0;
   }

   if (fabs(value - f0) < EPSILON) {
      v = v1;
      return p0;
   }

   if (fabs(value - f1) < EPSILON) {
      v = v0;
      return p1;
   }

   float t = (value - f0) / diff;
   v = v0 + t * (v1 - v0);

   return lerp3(p0, p1, t);
}

std::pair<vistle::Object *, vistle::Object *>
CuttingSurface::generateCuttingSurface(const vistle::Object * grid_object,
                                       const vistle::Object * data_object,
                                       const vistle::util::Vector & normal,
                                       const float distance) {

   const vistle::UnstructuredGrid *grid = NULL;
   const vistle::Vec<float> *data = NULL;

   if (!grid_object || !data_object)
      return std::make_pair((vistle::Object *) NULL, (vistle::Object *) NULL);

   if (grid_object->getType() == vistle::Object::SET &&
       data_object->getType() == vistle::Object::SET) {

      const vistle::Set *gset = static_cast<const vistle::Set *>(grid_object);
      const vistle::Set *dset = static_cast<const vistle::Set *>(data_object);

      if (gset->getNumElements() != dset->getNumElements())
         return std::make_pair((vistle::Object *) NULL,
                               (vistle::Object *) NULL);

      vistle::Set *outGSet = vistle::Set::create(gset->getNumElements());
      vistle::Set *outDSet = vistle::Set::create(dset->getNumElements());
      for (size_t index = 0; index < gset->getNumElements(); index ++) {
         std::pair<vistle::Object *, vistle::Object *> result =
            generateCuttingSurface(gset->getElement(index),
                                   dset->getElement(index), normal, distance);
         (*outGSet->elements)[index] = result.first;
         (*outDSet->elements)[index] = result.second;
      }

      return std::make_pair(outGSet, outDSet);
   }

   if (grid_object->getType() == vistle::Object::UNSTRUCTUREDGRID &&
       data_object->getType() == vistle::Object::VECFLOAT) {
      grid = static_cast<const vistle::UnstructuredGrid *>(grid_object);
      data = static_cast<const vistle::Vec<float> *>(data_object);
   }

   const size_t *el = &((*grid->el)[0]);
   const size_t *tl = &((*grid->tl)[0]);
   const size_t *cl = &((*grid->cl)[0]);
   const float *x = &((*grid->x)[0]);
   const float *y = &((*grid->y)[0]);
   const float *z = &((*grid->z)[0]);

   const float *d = &((*data->x)[0]);

   size_t numElem = grid->getNumElements();
   vistle::Triangles *triangles = vistle::Triangles::create();
   vistle::Vec<float> *outData = vistle::Vec<float>::create();

   size_t numVertices = 0;

#pragma omp parallel for
   for (size_t elem = 0; elem < numElem; elem ++) {

      switch (tl[elem]) {

         case vistle::UnstructuredGrid::HEXAHEDRON: {

            float3 vertlist[12];
            float maplist[12];
            float3 v[8];
            size_t index[8];
            float field[8];
            float mapping[8];
            size_t p = el[elem];

            index[0] = cl[p + 5];
            index[1] = cl[p + 6];
            index[2] = cl[p + 2];
            index[3] = cl[p + 1];
            index[4] = cl[p + 4];
            index[5] = cl[p + 7];
            index[6] = cl[p + 3];
            index[7] = cl[p];

            for (int idx = 0; idx < 8; idx ++)
               mapping[idx] = d[index[idx]];

            uint tableIndex = 0;
            for (int idx = 0; idx < 8; idx ++) {
               v[idx].x = x[index[idx]];
               v[idx].y = y[index[idx]];
               v[idx].z = z[index[idx]];
               field[idx] = (normal * vistle::util::Vector(v[idx].x, v[idx].y, v[idx].z) - distance);
            }

            for (int idx = 0; idx < 8; idx ++)
               tableIndex += (((int) (field[idx] < 0.0)) << idx);

            int numVerts = hexaNumVertsTable[tableIndex];
            if (numVerts) {
               numVertices += numVerts;
               vertlist[0] = interp(0.0, v[0], v[1], field[0], field[1],
                                    mapping[0], mapping[1], maplist[0]);
               vertlist[1] = interp(0.0, v[1], v[2], field[1], field[2],
                                    mapping[1], mapping[2], maplist[1]);
               vertlist[2] = interp(0.0, v[2], v[3], field[2], field[3],
                                    mapping[2], mapping[3], maplist[2]);
               vertlist[3] = interp(0.0, v[3], v[0], field[3], field[0],
                                    mapping[3], mapping[0], maplist[3]);

               vertlist[4] = interp(0.0, v[4], v[5], field[4], field[5],
                                    mapping[4], mapping[5], maplist[4]);
               vertlist[5] = interp(0.0, v[5], v[6], field[5], field[6],
                                    mapping[5], mapping[6], maplist[5]);
               vertlist[6] = interp(0.0, v[6], v[7], field[6], field[7],
                                    mapping[6], mapping[7], maplist[6]);
               vertlist[7] = interp(0.0, v[7], v[4], field[7], field[4],
                                    mapping[7], mapping[4], maplist[7]);

               vertlist[8] = interp(0.0, v[0], v[4], field[0], field[4],
                                    mapping[0], mapping[4], maplist[8]);
               vertlist[9] = interp(0.0, v[1], v[5], field[1], field[5],
                                    mapping[1], mapping[5], maplist[9]);
               vertlist[10] = interp(0.0, v[2], v[6], field[2], field[6],
                                     mapping[2], mapping[6], maplist[10]);
               vertlist[11] = interp(0.0, v[3], v[7], field[3], field[7],
                                     mapping[3], mapping[7], maplist[11]);

               for (int idx = 0; idx < numVerts; idx += 3) {

                  int edge[3];
                  float3 *v[3];
                  edge[0] = hexaTriTable[tableIndex][idx];
                  v[0] = &vertlist[edge[0]];
                  edge[1] = hexaTriTable[tableIndex][idx + 1];
                  v[1] = &vertlist[edge[1]];
                  edge[2] = hexaTriTable[tableIndex][idx + 2];
                  v[2] = &vertlist[edge[2]];
#pragma omp critical
                  {
                     triangles->cl->push_back(triangles->x->size());
                     triangles->cl->push_back(triangles->x->size() + 1);
                     triangles->cl->push_back(triangles->x->size() + 2);

                     triangles->x->push_back(v[0]->x);
                     triangles->x->push_back(v[1]->x);
                     triangles->x->push_back(v[2]->x);

                     triangles->y->push_back(v[0]->y);
                     triangles->y->push_back(v[1]->y);
                     triangles->y->push_back(v[2]->y);

                     triangles->z->push_back(v[0]->z);
                     triangles->z->push_back(v[1]->z);
                     triangles->z->push_back(v[2]->z);

                     outData->x->push_back(maplist[edge[0]]);
                     outData->x->push_back(maplist[edge[1]]);
                     outData->x->push_back(maplist[edge[2]]);
                  }
               }
            }
            break;
         }

         default:
            break;
      }
   }

   return std::make_pair(triangles, outData);
}


bool CuttingSurface::compute() {

   std::list<vistle::Object *> gridObjects = getObjects("grid_in");
   std::cout << "CuttingSurface: " << gridObjects.size() << " grid objects"
             << std::endl;

   std::list<vistle::Object *> dataObjects = getObjects("data_in");
   std::cout << "CuttingSurface: " << dataObjects.size() << " data objects"
             << std::endl;

   const vistle::util::Vector normal(1.0, 0.0, 0.0);
   const float distance = 0.0;

   while (gridObjects.size() > 0 && dataObjects.size() > 0) {

      struct timeval t0, t1;
      gettimeofday(&t0, NULL);

      ProfilerStart("/tmp/cut.prof");
      std::pair<vistle::Object *, vistle::Object *> object =
         generateCuttingSurface(gridObjects.front(), dataObjects.front(),
                                normal, distance);
      ProfilerStop();

      gettimeofday(&t1, NULL);
      long long usec = t1.tv_sec - t0.tv_sec;
      usec *= 1000000;
      usec += (t1.tv_usec - t0.tv_usec);

      std::cout << "CuttingSurface: " << usec << " usec" << std::endl;

      if (object.first)
         addObject("grid_out", object.first);

      if (object.second)
         addObject("data_out", object.second);

      removeObject("grid_in", gridObjects.front());
      removeObject("data_in", dataObjects.front());

      gridObjects = getObjects("grid_in");
      dataObjects = getObjects("data_in");
   }

   return true;
}
