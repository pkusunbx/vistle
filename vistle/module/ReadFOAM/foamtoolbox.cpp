/**************************************************************************\
 **                                                           (C)2013 RUS  **
 **                                                                        **
 ** Description: Read FOAM data format                                     **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 **                                                                        **
 ** History:                                                               **
 ** May   13	    C.Kopf  	    V1.0                                   **
 *\**************************************************************************/


//Includes copied from vistle ReadFOAM.cpp
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <set>
#include <map>
#include <cctype>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/qi_no_skip.hpp>
#include <boost/spirit/include/support_istream_iterator.hpp>
#include <boost/spirit/include/support_multi_pass.hpp>
#include <boost/spirit/include/classic_position_iterator.hpp>

#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_object.hpp>

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/io.hpp>
#include <boost/fusion/include/vector.hpp>
#include <boost/fusion/iterator.hpp>
#include <boost/fusion/include/iterator.hpp>
#include <boost/fusion/container/vector/vector_fwd.hpp>
#include <boost/fusion/include/vector_fwd.hpp>
#include <boost/fusion/include/std_pair.hpp>

#include <boost/filesystem.hpp>

#include "foamtoolbox.h"

const size_t MaxHeaderLines = 1000;

namespace bi = boost::iostreams;
namespace bs = boost::spirit;
namespace bf = boost::filesystem;
namespace classic = boost::spirit::classic;

template<typename Alloc = std::allocator<char> >
struct basic_gzip_decompressor;
typedef basic_gzip_decompressor<> gzip_decompressor;

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

BOOST_FUSION_ADAPT_STRUCT(
    HeaderInfo,
    (std::string, version)
    (std::string, format)
    (std::string, fieldclass)
    (std::string, location)
    (std::string, object)
    (std::string, dimensions)
    (std::string, internalField)
    (index_t, lines)
)


class FilteringStreamDeleter {
 public:
   FilteringStreamDeleter(bi::filtering_istream *f, std::ifstream *s)
      : filtered(f)
      , stream(s)
      {}

   void operator()(std::istream *f) {
      assert(static_cast<bi::filtering_istream *>(f) == filtered);
      delete filtered;
      delete stream;
   }

   bi::filtering_istream *filtered;
   std::ifstream *stream;
};

boost::shared_ptr<std::istream> getStreamForFile(const std::string &filename) {

   std::ifstream *s = new std::ifstream(filename, std::ios_base::in | std::ios_base::binary);
   if (!s->is_open()) {
      std::cerr << "failed to open " << filename << std::endl;
      return boost::shared_ptr<std::istream>();
   }

   bi::filtering_istream *fi = new bi::filtering_istream;
   bf::path p(filename);
   if (p.extension().string() == ".gz") {
      fi->push(bi::gzip_decompressor());
   }
   fi->push(*s);
   return boost::shared_ptr<std::istream>(fi, FilteringStreamDeleter(fi, s));
}

boost::shared_ptr<std::istream> getStreamForFile(const std::string &dir, const std::string &basename) {

   bf::path zipped(dir + "/" + basename + ".gz");
   if (bf::exists(zipped) && !bf::is_directory(zipped))
      return getStreamForFile(zipped.string());
   else
      return getStreamForFile(dir + "/" + basename);
}

bool isTimeDir(const std::string &dir) {

   //std::cerr << "checking timedir: " << dir << std::endl;

   if (dir == ".")
      return false;
#if 1
   if (dir == "0")
      return false;
#endif

   int numdots = 0;
   for (size_t i=0; i<dir.length(); ++i) {
      if (dir[i] == '.') {
         ++numdots;
         if (numdots > 1)
            return false;
      } else if (!isdigit(dir[i]))
         return false;
   }

   return true;
}

bool isProcessorDir(const std::string &dir) {

   if (dir.find("processor") != 0) {

      return false;
   }

   for (size_t i=strlen("processor"); i<dir.length(); ++i) {

      if (!isdigit(dir[i]))
         return false;
   }

   return true;
}

bool checkMeshDirectory(CaseInfo &info, const std::string &meshdir, bool time) {

   //std::cerr << "checking meshdir " << meshdir << std::endl;
   bf::path p(meshdir);
   if (!bf::is_directory(p)) {
      std::cerr << meshdir << " is not a directory" << std::endl;
      return false;
   }

   bool havePoints = false;
   std::map<std::string, std::string> meshfiles;
   for (auto it = bf::directory_iterator(p);
         it != bf::directory_iterator();
         ++it) {
      bf::path ent(*it);
      std::string stem = ent.stem().string();
      std::string ext = ent.extension().string();
      if (stem == "points" || stem == "faces" || stem == "owner" || stem == "neighbour") {
         if (bf::is_directory(*it) || (!ext.empty() && ext != ".gz")) {
            std::cerr << "ignoring " << *it << std::endl;
         } else {
            meshfiles[stem] = bf::path(*it).string();
            if (stem == "points")
               havePoints = true;
         }
      }
   }

   if (meshfiles.size() == 4) {
      if (time) {
         info.varyingGrid = true;
      }
      return true;
   }
   if (meshfiles.size() == 1 && time && havePoints) {
      info.varyingCoords = true;
      return true;
   }

   std::cerr << "did not find all of points, faces, owner and neighbour files" << std::endl;
   return false;
}

bool checkSubDirectory(CaseInfo &info, const std::string &timedir, bool time) {

   bf::path dir(timedir);
   if (!bf::exists(dir)) {
      std::cerr << "timestep directory " << timedir << " does not exist" << std::endl;
      return false;
   }
   if (!bf::is_directory(timedir)) {
      std::cerr << "timestep directory " << timedir << " is not a directory" << std::endl;
      return false;
   }

   for (auto it = bf::directory_iterator(dir);
         it != bf::directory_iterator();
         ++it) {
      bf::path p(*it);
      if (bf::is_directory(*it)) {
         std::string name = p.filename().string();
         if (name == "polyMesh") {
            if (!checkMeshDirectory(info, p.string(), time))
               return false;
         }
      } else {
         std::string stem = p.stem().string();
         if (time)
            ++info.varyingFields[stem];
         else
            ++info.constantFields[stem];
      }
   }

   return true;
}

bool checkCaseDirectory(CaseInfo &info, const std::string &casedir, bool compare, double mintime, double maxtime) {

   std::cerr << "reading casedir: " << casedir << std::endl;

   bf::path dir(casedir);
   if (!bf::exists(dir)) {
      std::cerr << "case directory " << casedir << " does not exist" << std::endl;
      return false;
   }
   if (!bf::is_directory(casedir)) {
      std::cerr << "case directory " << casedir << " is not a directory" << std::endl;
      return false;
   }

   int num_processors = 0;
   for (auto it = bf::directory_iterator(dir);
         it != bf::directory_iterator();
         ++it) {
      if (bf::is_directory(*it)) {
         if (isProcessorDir(bf::basename(it->path())))
            ++num_processors;
      }
   }

   if (!compare && num_processors>0) {
      info.numblocks = num_processors;
   }

   if (compare && num_processors > 0) {
      std::cerr << "found processor subdirectory in processor directory" << std::endl;
      return false;
   }
   
   if (num_processors > 0) {
      bool result = checkCaseDirectory(info, casedir+"/processor0", false, mintime, maxtime);
      if (!result) {
         std::cerr << "failed to read case directory for processor 0" << std::endl;
         return false;
      }

      for (int i=1; i<num_processors; ++i) {
         std::stringstream s;
         s << casedir << "/processor" << i;
         bool result = checkCaseDirectory(info, s.str(), true, mintime, maxtime);
         if (!result)
            return false;
      }

      return true;
   }

   int num_timesteps = 0;
   for (auto it = bf::directory_iterator(dir);
         it != bf::directory_iterator();
         ++it) {
      if (bf::is_directory(*it)) {
         std::string bn = it->path().filename().string();
         if (isTimeDir(bn)) {
            double t = atof(bn.c_str());
            if (t >= mintime && t <= maxtime) {
               ++num_timesteps;
               if (compare) {
                  if (info.timedirs.find(t) == info.timedirs.end()) {
                     std::cerr << "timestep " << bn << " not available on all processors" << std::endl;
                     return false;
                  }
               } else {
                  info.timedirs[t]=bn;
               }
            }
         }
      }
   }

   bool first = true;
   for (auto it = bf::directory_iterator(dir);
         it != bf::directory_iterator();
         ++it) {
      if (bf::is_directory(*it)) {
         std::string bn = it->path().filename().string();
         if (isTimeDir(bn)) {
            bool result = checkSubDirectory(info, it->path().string(), true);
            if (!result)
               return false;
            first = false;
         } else if (bn == "constant") {
            bool result = checkSubDirectory(info, it->path().string(), false);
            if (!result)
               return false;
         }
      }
   }

   if (compare && num_timesteps != info.timedirs.size()) {
      std::cerr << "not all timesteps available on all processors" << std::endl;
      return false;
   }

   return true;
}

CaseInfo getCaseInfo(const std::string &casedir, double mintime, double maxtime) {

   CaseInfo info;
   info.valid = checkCaseDirectory(info, casedir, false, mintime, maxtime);

   std::cerr << " " << "casedir: " << casedir << " " << std::endl
      << "Number of processors: " << info.numblocks << std::endl
      << "Number of time directories found: " << info.timedirs.size()  << std::endl
      << "Number of fields: " << info.constantFields.size()+info.varyingFields.size() << std::endl;

   int np = info.numblocks > 0 ? info.numblocks : 1;
   std::cerr << "  constant:";
   for (auto field: info.constantFields) {
      std::cerr << "  " << field.first << ": " << field.second;
      if (field.second != np) {
         std::cerr << " (ignored)";
         info.constantFields.erase(field.first);
      }
   }
   std::cerr << std::endl;

   std::cerr << "  varying: ";
   for (auto field: info.varyingFields) {
      std::cerr << "  " << field.first << ": " << field.second;
      if (field.second != np*info.timedirs.size()) {
         std::cerr << " (ignored)";
         info.varyingFields.erase(field.first);
      }
   }
   std::cerr << std::endl;

   return info;
}

//Boost Spirit parser definitions

std::string getFoamHeader(std::istream &stream)
{
   std::string header;
   for (size_t i=0; i<MaxHeaderLines; ++i) {
      std::string line;
      std::getline(stream, line);
      if (line == "(")
         break;
      header.append(line);
      header.append("\n");
   }
   return header;
}

//Skipper for skipping FOAM Headers and unimportant data
template <typename Iterator>
struct skipper: qi::grammar<Iterator> {

   skipper(): skipper::base_type(start) {

      start =
         ascii::space
         | "/*" >> *(ascii::char_ - "*/") >> "*/"
         | "//" >> *(ascii::char_ - qi::eol) >> qi::eol
         | "FoamFile" >> *ascii::space >> '{' >> *(ascii::char_ - '}') >> '}'
         | "dimensions" >> *(ascii::char_ - qi::eol) >> qi::eol
         | "internalField" >> *(ascii::char_ - qi::eol) >> qi::eol
         ;
   }

   qi::rule<Iterator> start;
};

//Skipper for skipping FOAM Headers and unimportant data
template <typename Iterator>
struct headerSkipper: qi::grammar<Iterator> {

    headerSkipper(): headerSkipper::base_type(start) {

        start =
            ascii::space
            | "/*" >> *(ascii::char_ - "*/") >> "*/"
            | "//" >> *(ascii::char_ - qi::eol) >> qi::eol
            | "FoamFile" >> *ascii::space >> '{' >> *(ascii::char_ - qi::eol) >> qi::eol
            | "note" >> *(ascii::char_ - qi::eol) >> qi::eol
            | '}'
            ;
    }

qi::rule<Iterator> start;
};

template <typename Iterator>
struct headerParser: qi::grammar<Iterator, HeaderInfo(), headerSkipper<Iterator> > {

    headerParser(): headerParser::base_type(start) {

        start =
            version
            >> format
            >> fieldclass
            >> location
            >> object
            >> -dimensions
            >> -internalField
            >> lines
        ;

        version = "version" >> +(ascii::char_ - ';') >> ';'
        ;
        format = "format" >> +(ascii::char_ - ';') >> ';'
        ;
        fieldclass = "class" >> +(ascii::char_ - ';') >> ';'
        ;
        location = "location" >> qi::lit('"') >> +(ascii::char_ - '"') >> '"' >> ';'
        ;
        object = "object" >> +(ascii::char_ - ';') >> ';'
        ;
        dimensions = "dimensions" >> qi::lexeme['[' >> +(ascii::char_ - ']') >>']' >> ';']
        ;
        internalField ="internalField" >> qi::lexeme[+(ascii::char_ - qi::eol) >> qi::eol]
        ;
        lines = qi::int_
        ;
    }

    qi::rule<Iterator, HeaderInfo(), headerSkipper<Iterator> > start;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > version;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > format;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > fieldclass;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > location;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > object;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > dimensions;
    qi::rule<Iterator, std::string(), headerSkipper<Iterator> > internalField;
    qi::rule<Iterator, int(), headerSkipper<Iterator> > lines;
};

HeaderInfo readFoamHeader(std::istream &stream)
{
    struct headerParser<std::string::iterator> headerParser;
    struct headerSkipper<std::string::iterator> headerSkipper;

    std::string header = getFoamHeader(stream);
    HeaderInfo info;

    info.valid = qi::phrase_parse(header.begin(),header.end(),
                             headerParser, headerSkipper, info);

    if (!info.valid) {
       std::cerr << "parsing FOAM header failed" << std::endl;

       std::cerr << "================================================" << std::endl;
       std::cerr << header << std::endl;
       std::cerr << "================================================" << std::endl;
    }

    return info;
}


//Skipper for skipping everything but the dimensions in the owners file Header
template <typename Iterator>
struct dimSkipper: qi::grammar<Iterator> {

   dimSkipper(): dimSkipper::base_type(start) {

      start =
         ascii::space
         | "/*" >> *(ascii::char_ - "*/") >> "*/"
         | "//" >> *(ascii::char_ - qi::eol) >> qi::eol
         | "version" >> *(ascii::char_ - qi::eol) >> qi::eol
         | "location" >> *(ascii::char_)
         | ~ascii::char_("0-9")
         ;
   }

   qi::rule<Iterator> start;
};

//Parser that Reads the size of the domain (nCells, nPoints, nFaces, nInternalFaces) from the owners header (use together with dimSkipper)
template <typename Iterator>
struct dimParser: qi::grammar<Iterator, std::vector<index_t>(),
                               dimSkipper<Iterator> > {

   dimParser(): dimParser::base_type(start) {

       start = term >> term >> term >> term
         ;

      term = qi::int_
         ;
   }

   qi::rule<Iterator, std::vector<index_t>(), dimSkipper<Iterator> > start;
   qi::rule<Iterator, index_t(),dimSkipper<Iterator> > term;
};

template <typename Iterator>
struct BoundaryParser
: qi::grammar<Iterator, std::map<std::string, std::map<std::string, std::string> >(),
   skipper<Iterator> >
{
   BoundaryParser()
      : BoundaryParser::base_type(start)
      {
         start = qi::omit[qi::int_] >> '(' >> +mapmap >> ')';
         mapmap = +(qi::char_ - '{') >> '{' >> entrymap >> '}';
         entrymap = +pair;
         pair  = qi::lexeme[+qi::char_("a-zA-Z")] >> +(qi::char_ - ';') >> ';';
      }

   qi::rule<Iterator, std::map<std::string, std::map<std::string, std::string> >(),
      skipper<Iterator> > start;

   qi::rule<Iterator, std::pair<std::string, std::map<std::string, std::string> >(),
      skipper<Iterator> > mapmap;

   qi::rule<Iterator, std::map<std::string, std::string>(),
      skipper<Iterator> > entrymap;

   qi::rule<Iterator, std::pair<std::string, std::string>(),
      skipper<Iterator> > pair;
};



Boundaries loadBoundary(const std::string &meshdir) {

   auto stream = getStreamForFile(meshdir, "boundary");

   typedef std::istreambuf_iterator<char> base_iterator_type;
   typedef bs::multi_pass<base_iterator_type> forward_iterator_type;
   typedef classic::position_iterator2<forward_iterator_type> pos_iterator_type;
   forward_iterator_type fwd_begin =
      bs::make_default_multi_pass(base_iterator_type(*stream));
   forward_iterator_type fwd_end;
   pos_iterator_type pos_begin(fwd_begin, fwd_end, meshdir + "/boundary");
   pos_iterator_type pos_end;

   struct skipper<pos_iterator_type> skipper;

   BoundaryParser<pos_iterator_type> p;
   std::map<std::string, std::map<std::string, std::string> > boundaries;

   bool r = qi::phrase_parse(pos_begin, pos_end,
         p, skipper, boundaries);

   //std::cout << "r: " << r << std::endl;

   Boundaries bounds;

   std::map<std::string, std::map<std::string, std::string> >::iterator top;
   for (top = boundaries.begin(); top != boundaries.end(); top ++) {

      std::string name = top->first;
#if 0
      std::cout << name << ":" << std::endl;
      std::map<std::string, std::string>::iterator i;
      for (i = top->second.begin(); i != top->second.end(); i ++) {
         std::cout << "    " << i->first << " => " << i->second << std::endl;
      }
#endif
      const auto &cur = top->second;
      auto nFaces = cur.find("nFaces");
      auto startFace = cur.find("startFace");
      auto type = cur.find("type");
      if (nFaces != cur.end() && startFace != cur.end() && type != cur.end()) {
         std::string t = type->second;
         index_t n = atol(nFaces->second.c_str());
         index_t s = atol(startFace->second.c_str());
         bounds.addBoundary(Boundary(name, s, n, t));
      }
   }

   return bounds;
}

template<typename T>
std::istream &operator>>(std::istream &stream, std::vector<T> &vec) {

   size_t n;
   stream >> n;
   stream.ignore(std::numeric_limits<std::streamsize>::max(),'(');
   vec.reserve(n);
   for (size_t i=0; i<n; ++i) {
      T val;
      stream >> val;
      vec.push_back(val);
   }
   stream.ignore(std::numeric_limits<std::streamsize>::max(),')');
   return stream;
}

template<typename T>
bool readVectorArray(std::istream &stream, T *x, T *y, T *z, const size_t lines) {

   for (size_t i=0; i<lines; ++i) {
      stream.ignore(std::numeric_limits<std::streamsize>::max(), '(');
      stream >> x[i] >> y[i] >> z[i];
      stream.ignore(std::numeric_limits<std::streamsize>::max(), ')');
   }

   return true;
}

template<typename T>
bool readArray(std::istream &stream, T *p, const size_t lines) {

   for (size_t i=0; i<lines; ++i) {
      stream >> p[i];
   }

   return true;
}

bool readIndexArray(std::istream &stream, index_t *p, const size_t lines) {
   return readArray<index_t>(stream, p, lines);
}

bool readIndexListArray(std::istream &stream, std::vector<index_t> *p, const size_t lines) {
   return readArray<std::vector<index_t>>(stream, p, lines);
}

bool readFloatArray(std::istream &stream, scalar_t *p, const size_t lines) {
   return readArray<scalar_t>(stream, p, lines);
}

bool readFloatVectorArray(std::istream &stream, scalar_t *x, scalar_t *y, scalar_t *z, const size_t lines) {

   return readVectorArray(stream, x, y, z, lines);
}

DimensionInfo readDimensions(const std::string &meshdir) {

   struct dimParser<std::string::iterator> dimParser;
   struct dimSkipper<std::string::iterator> dimSkipper;
   boost::shared_ptr<std::istream> fileIn = getStreamForFile(meshdir, "owner");
   if (!fileIn) {
      std::cerr << "failed to open " << meshdir + "/polyMesh/owner for reading dimensions" << std::endl;
   }
   std::string header = getFoamHeader(*fileIn);

   std::vector<index_t> dims;
   bool r = qi::phrase_parse(header.begin(), header.end(), dimParser, dimSkipper, dims);
   //std::cerr << " " << "Dimension parsing successful: " << r << "  ## dimensions loaded: " << dims.size()<< " of 4" << std::endl;
   DimensionInfo info;
   info.points = dims[0];
   info.cells = dims[1];
   info.faces = dims[2];
   info.internalFaces = dims[3];
   return info;
}

index_t findVertexAlongEdge(const index_t & point,
      const index_t & homeface,
      const std::vector<index_t> & cellfaces,
      const std::vector<std::vector<index_t> > & faces) {

   std::vector<index_t> pointfaces;
   for (index_t i=0;i<cellfaces.size();i++) {
      if(cellfaces[i]!=homeface) {
         const std::vector<index_t> &face=faces[cellfaces[i]];
         for (index_t j=0;j<face.size(); j++) {
            if (face[j]==point) {
               pointfaces.push_back(cellfaces[i]);
               break;
            }
         }
      }
   }
   const std::vector<index_t> &a=faces[pointfaces[0]];
   const std::vector<index_t> &b=faces[pointfaces[1]];
   for (index_t i=0;i<a.size();i++) {
      for (index_t j=0;j<b.size();j++) {
         if(a[i]==b[j] && a[i]!=point) {
            return a[i];
         }
      }
   }

   return -1;
}

bool isPointingInwards(index_t &face,
      index_t &cell,
      index_t &ninternalFaces,
      std::vector<index_t> &owners,
      std::vector<index_t> &neighbors) {

   //check if the normal vector of the cell is pointing inwards
   //(in openFOAM it always points into the cell with the higher index)
   if (face>=ninternalFaces) {//if ia is bigger than the number of internal faces
      return true;                    //then a is a boundary face and normal vector goes inwards by default
   } else {
      index_t j,o,n;
      o=owners[face];
      n=neighbors[face];
      if (o==cell) {j=n;}
      else {j=o;} //now i is the index of current cell and j is index of other cell sharing the same face
      if (cell>j) {return true;} //if index of active cell is higher than index of "next door" cell
      else {return false;}    //then normal vector points inwards else outwards
   }
}

std::vector<index_t> getVerticesForCell(const std::vector<index_t> &cellfaces,
      const std::vector<std::vector<index_t>> &faces) {

   std::vector<index_t> cellvertices;
   for(index_t i=0;i<cellfaces.size();i++) {
      for(index_t j=0;j<faces[cellfaces[i]].size();j++) {
         cellvertices.push_back(faces[cellfaces[i]][j]);
      }
   }
   std::sort(cellvertices.begin(),cellvertices.end());
   cellvertices.erase(std::unique(cellvertices.begin(), cellvertices.end()), cellvertices.end());
   return cellvertices;
}
