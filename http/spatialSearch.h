#ifndef SPSRCH
#define SPSRCH

#include <FFJSON.h>
#include <string>
#include <map>
#include <list>
#include <thread>
#include <queue>
#include <math.h>

enum SLOG {
   SL = 1<<13,
   SLL = 1<<14,
   SLLL = 1<<18
};
struct QuadNode;
struct Circle {
   float x;
   float y;
   float r;
   FFJSON* nf;
   bool grabIfNearest (FFJSON& f);
};
union QuadHldr;
struct WholeQuadNode;
struct ParentQuadHldr {
   ParentQuadHldr (
      deque<WholeQuadNode>* pqcqh=nullptr, QuadNode* node=nullptr,
      ParentQuadHldr* pQH = nullptr, float x = 0, float y = 0) 
      : pqcqh(pqcqh), node(node), pQH(pQH), x(x), y(y)
      {}
   QuadNode* node;
   ParentQuadHldr* pQH;
   float x,y;
   deque<WholeQuadNode>* pqcqh;
   int8_t ind;
};
struct WholeQuadNode {
   QuadNode* qn;
   float x,y,dx,dy;
   char xsign,ysign,rxsign,rysign;
   QuadNode* pQN;
   int8_t ind;
   QuadHldr* qh;
};
struct Direction {
   int8_t x;
   int8_t y;
   int8_t abs ();
};
struct NdNPrn {
   QuadHldr* qh = nullptr;
   QuadNode* prn = nullptr;
   float dx = 0.0;
   float ds = 0.0;
   Direction d = {0};
   uint8_t ind = 0;
   void print () const;
};
typedef unsigned uchar;
enum OP {
   OR, AND
};
struct Pts {
   Circle c = {0};
   vector<NdNPrn> pts;
   vector<uint> ina;
   NdNPrn cnd;
   int ni=-1;
   int nni=-1;
   int pni=0;
   OP op=AND;
   uint minPts=20;
};
struct CompareByDistanceToCenter;
struct QHMut_ {
   void* vp;
   mutex m;
};
struct CntMut_ {
   mutex m;
   int count=0;
};
struct FFQuad_ {
   FFJSON& rF;
   vector<uint>& ina;
   float lx = 0;
   float ly = 0;
   bool deleteLeaf = false;
	bool isS= false;
   FFQuad_ (FFJSON& rF, vector<uint>& ina, float lx = 0, float ly = 0,
            bool deleteLeaf = false) : rF(rF), ina(ina), lx(lx), ly(ly),
                                       deleteLeaf(deleteLeaf) {}
};
union QuadHldr {
   QuadHldr () {
      qp=nullptr;
   };
	~QuadHldr ();
	void destroy (QuadNode* pQN= nullptr, QuadNode* tQN= nullptr);
	struct InitInsArgs_ {
      FFJSON& rF;
      const vector<uint>& ina;
      float lx;
      float ly;
      InitInsArgs_ (FFJSON& rF, const vector<uint>& ina, float lx, float ly):
         rF(rF), ina(ina), lx(lx), ly(ly) {};
   };
   //QHMut_ mtx;
   QuadNode* qp;
   FFJSON* fp;
   set<FFJSON*>* sp;
   QuadNode* qn ();
   uint insert (
      FFQuad_& fq, float x = 0.0, float y = 0.0, uint level = 0,
      QuadNode* tQN = nullptr, int8_t tind=0, QuadNode* pQN = nullptr,
      int8_t ind=0, int8_t sn = 0
   );
   uint getPointsFromQuad (
      Pts& pts, uint level=0, float x=0, float y=0,
      QuadNode* tQN=nullptr, int8_t tind=0, QuadNode* pQN=nullptr, int8_t ind=0
   );
   // uint getPointsFromRadius (
   //    set<FFJSON*, CompareByDistanceToCenter>& pts, Circle& c, uint minPts=30,
   //    uint level=0, float x=0, float y=0, QuadNode* pQN = nullptr
   // );
   // uint addAllLeavesInRadius (set<FFJSON*,CompareByDistanceToCenter>& pts,
   //                            QuadNode* pQN);
   uint findNeighbours (Pts& pts, QuadNode* tQN=nullptr, uint8_t tind=0,
                        QuadNode* pQN=nullptr, uint8_t ind=0, float dx =0.0,
                        float ds=0.0, Direction d = {0},
                        bool notChild = false);
   uint addChildrenOnEdge (Pts& pts, Direction d, QuadNode* pQN,
                           uint8_t ind, float dx, float ds, bool noChk=false);
   int addThis (Pts& pts, Direction d, float dx, float ds,
                 QuadNode* pQN, uint8_t ind = 0, int noChk = 0);
   void print (Circle& c, uint level = 0, QuadNode* tQN = nullptr,
               uint8_t tind = 0, QuadNode* pQN = nullptr, uint8_t ind = 0);
   vector<uint> getIntNames (QuadNode* tQN=nullptr, uint8_t tind=0,
                             QuadNode* pQN=nullptr, uint8_t ind=0);
   CntMut_& lock (int l);
   void unlock (CntMut_* p);
   // void del (QuadNode* tQN = nullptr, int8_t tind = 0,
   //           QuadNode* pQN = nullptr, int8_t ind = 0);
};

struct CompareByDistanceToCenter {
   bool operator () (FFJSON* pf1, FFJSON* pf2) const {
      FFJSON& f1 = *pf1;
      FFJSON& f2 = *pf2;
      float x1 = f1["location"][0];
      float y1 = f1["location"][1];
      float x2 = f2["location"][0];
      float y2 = f2["location"][1];
      if (x1==x2 && y1==y2) {
         return f1 < f2;
      }
      float r1 = pow(cx-x1,2) + pow(cy-y1,2);
      float r2 = pow(cx-x2,2) + pow(cy-y2,2);
      return (r1 < r2);
   }
   CompareByDistanceToCenter (float cx, float cy):cx(cx),cy(cy) {}
   float cx,cy;
};
struct QuadNode {
   QuadHldr en;
   QuadHldr es;
   QuadHldr wn;
   QuadHldr ws;
   QuadNode ();
   uint insert (
      FFQuad_& fq, float x = 0.0, float y = 0.0, uint level = 0,
      QuadNode* pQN = nullptr, int8_t ind=0, int8_t sn = 0
   );
   void del (QuadNode* tQN = nullptr, int8_t tind = 0,
             QuadNode* pQN = nullptr, int8_t ind = 0);
   void seti (vector<uint>& ina);
   bool copyi (vector<uint>& ina);
   uint hasName (vector<uint>& ina,
                 vector<map<QuadNode*,uint>::iterator> vit,
                 bool allIna = false);
   bool updateIntNames (QuadNode* tQN = nullptr, uint8_t tind = 0,
                        QuadNode* pQN = nullptr, uint8_t ind = 0);
   ~QuadNode ();
	void destroy (QuadNode* pQN= nullptr);
};

struct Qn2 {
   QuadNode* p1;
   QuadNode* p2;
   QuadNode* p3;
   QuadNode* p4;
};
void printpts (const vector<NdNPrn>& pts);
tuple<void*,int8_t> getNode (NdNPrn n);
// template<typename T, typename U>
// tuple<void*, int8_t> bpxor (T* a, U* b);
vector<string> metaname (string name);
vector<uint> nametouint (vector<string>& mstr);
int getIdChildInd (FFJSON& arr, int id);
extern map<string, FFJSON*>* nameints;
extern FFJSON* fnameints;
extern map<const string*, uint> mitpos;
extern mutex mitposMtx;
extern QuadHldr thnsTree;
struct CompNameWt {
   bool operator () (const map<string, FFJSON*>::iterator it1,
                     const map<string, FFJSON*>::iterator it2) const;
};
extern CompNameWt cmpNmWt;
int8_t ffHasName (FFJSON& ff, vector<uint>& ina, bool allIna = false);
#endif
