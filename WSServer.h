/* 
 * File:   WSServer.h
 * Author: Gowtham
 *
 * Created on December 12, 2013, 6:34 PM
 */

#ifndef WSSERVER_H
#define WSSERVER_H

#define MAX_ECHO_PAYLOAD 1024
#define LWS_NO_CLIENT

#include "FerryStream.h"
#include "mongoose.h"
#include <FFJSON.h>
#include <string>
#include <map>
#include <list>
#include <thread>
#include <queue>
#include <math.h>

using namespace std;
using namespace placeholders;

class WSServer {
public:
   WSServer(
      const char* pcHostName,
      int iDebugLevel=15,
      int iPort=8080,
      int iSecurePort=0,
      const char* pcSSLCertFilePath="",
      const char* pcSSLPrivKeyFilePath="",
      const char* pcSSLCAFilePath="",
      bool bDaemonize=false,
      int iRateUs=0,
      const char* pcInterface="",
      const char* pcClient="",
      int iOpts=0,
      int iSysLogOptions=0
   );
   virtual ~WSServer();
private:
/*
   void fn (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   void fn_tls (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   void tls_ntls_common (struct mg_connection* c, int ev, void* ev_data,
                         void* fn_data);
   void mailfn (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   struct mg_mgr mgr, mail_mgr;

   thread_local static WSServer* toHttpListen;
   static void gfn (struct mg_connection *c, int ev, void *ev_data,
                    void *fn_data);
   static void gfn_tls (struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data);
   static void gmailfn (struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data);
   const char* server = "tcp://ferryfair.com:25";
   const char* user = "Necktwi";
   const char* pass = "tornshoes";
   char* to = "gowtham.kudupudi@gmail.com";

   const char* from = "FerryFair";
   const char* subj = "Test email from Mongoose library!";
   const char* mesg = "Hi!\nThis is a test message.\nBye.";

   bool s_quit = false;
*/
};

struct QuadNode;
struct Circle {
   float x;
   float y;
   float r;
   Txj* nf;
   bool grabIfNearest (Txj& f);
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
struct Pts {
   Circle c = {0};
   vector<NdNPrn> pts;
   vector<uint> ina;
   int ni=-1;
   int nni=-1;
   uint minPts=-1;
};
struct CompareByDistanceToCenter;
union QuadHldr {
   QuadHldr () {
      qp=nullptr;
   };
   QuadNode* qp;
   Txj* fp;
   set<Txj*>* sp;
   QuadNode* qn ();
   uint insert (
      Txj& rF, vector<uint>& ina, bool deleteLeaf = false,
      float lx= 0.0, float ly=0.0, float x = 0.0, float y = 0.0,
      uint level = 0, QuadNode* tQN = nullptr, int8_t tind=0,
      QuadNode* pQN = nullptr, int8_t ind=0, int8_t sn = 0
   );
   uint getPointsFromQuad (
      Pts& pts, uint level=0, float x=0, float y=0,
      QuadNode* tQN=nullptr, int8_t tind=0, QuadNode* pQN=nullptr, int8_t ind=0
   );
   // uint getPointsFromRadius (
   //    set<Txj*, CompareByDistanceToCenter>& pts, Circle& c, uint minPts=30,
   //    uint level=0, float x=0, float y=0, QuadNode* pQN = nullptr
   // );
   // uint addAllLeavesInRadius (set<Txj*,CompareByDistanceToCenter>& pts,
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
   // void del (QuadNode* tQN = nullptr, int8_t tind = 0,
   //           QuadNode* pQN = nullptr, int8_t ind = 0);
};

struct CompareByDistanceToCenter {
   bool operator () (Txj* pf1, Txj* pf2) const {
      Txj& f1 = *pf1;
      Txj& f2 = *pf2;
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
      Txj& rF, vector<uint>& ina, float lx, float ly, float x = 0.0,
      float y = 0.0, uint level = 0, QuadNode* pQN = nullptr, int8_t ind=0,
      bool deleteLeaf = false, int8_t sn = 0
   );
   void del (QuadNode* tQN = nullptr, int8_t tind = 0,
             QuadNode* pQN = nullptr, int8_t ind = 0);
   void seti (vector<uint>& ina);
   bool copyi (vector<uint>& ina);
   uint hasName (vector<uint>& ina,
                 vector<map<QuadNode*,uint>::iterator> vit);
   bool updateIntNames (QuadNode* tQN = nullptr, uint8_t tind = 0,
                        QuadNode* pQN = nullptr, uint8_t ind = 0);
   ~QuadNode ();
};

struct Qn2 
{
   QuadNode* p1;
   QuadNode* p2;
   QuadNode* p3;
   QuadNode* p4;
};
void printpts (const vector<NdNPrn>& pts);
#endif /* WSSERVER_H */
