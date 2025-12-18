#include "logger.h"
#include "FFJSON.h"
#include "spatialSearch.h"

enum TLOG {
   TL = 1<<11,
};
FF_LOG_TYPE fflAllowedType =
   (FF_LOG_TYPE) (FFL_ERR | FFL_NOTICE | FFL_DEBUG | FFL_INFO | FFL_WARN);
unsigned int fflAllowedBlks = (uint)(TL|SLL);
int child_exit_status = 0;

void makeThngsTree (Txo& cfg) {
   QuadNode q;
   fnameints = &cfg["nameints"];
   nameints = fnameints->val.pairs;
   map<string, FFJSON*>::iterator nit = nameints->begin();
   multiset<map<string, FFJSON*>::iterator, CompNameWt> namewtset(cmpNmWt);
   while (nit!=nameints->end()) {
      namewtset.insert(nit);
      ++nit;
   }
   uint i=0;
   multiset<map<string, FFJSON*>::iterator, CompNameWt>::iterator mit
      = namewtset.begin();
   while (mit!=namewtset.end()) {
      mitpos[&((*mit)->first)]=i;
      ++i;
      ++mit;
   }
   FFJSON& users = cfg["users"];
   FFJSON::Iterator it = users.begin();
   FFJSON::Iterator tit;
   int ic=0;
   vector<string> bmstr = metaname("flat gowtham");
   vector<uint> bina = nametouint(bmstr);
               
   while (it!= users.end()) {
      if (it->isType(FFJSON::LINK)) {
         ++it;
         continue;
      }
      string user = it.getIndex();
      //adds users to nameints
      //vector<string> musr = metaname(user);
      FFJSON& uthings = (*it)["things"];
      //(*fnameints)[musr[0]]=uthings.size+(int)(*fnameints)[musr[0]];
      tit = uthings.begin();
      while (tit!=uthings.end()) {
         if (!((*tit)["name"].isType(FFJSON::UNDEFINED) ||
               (*tit)["location"].isType(FFJSON::UNDEFINED))) {
            FFJSON* pF = &*tit;
            //flDbg(FL, "inserting %d", ic);
            FFJSON& rF = *pF;
            string tname((ccp)rF["name"]);
            tname += " ";
            tname += (ccp)rF["user"]["name"];
            flDbg(TL,"inserting %p: %s", pF, tname.c_str());
            vector<string> mstr = metaname(tname);
            vector<uint> ina = nametouint(mstr);
            for (int i=0; i<ina.size(); ++i)
               flDbgCntnu(TL,"%x ", ina[i]);
            flDbgCntnu(TL, "\n");
            float lx = rF["location"][1];
            float ly = rF["location"][0];
            FFQuad_ fq(rF, ina, lx, ly);
            thnsTree.insert(fq);
            //flDbg(FL, "%d inserted %d", tid, ic);
            // FFJSON& rF = *pF;
            // string tname((ccp)rF["name"]);
            // tname += (ccp)rF["user"]["name"];
            // vector<string> mstr = metaname(tname);
            // vector<uint> ina = nametouint(mstr);
            // bool found = true;
            // if (bina.size()>ina.size())
            //    goto skipFor;
            // for (int i=0; i<bina.size(); ++i) {
            //    if (bina[i]&ina[i]!=bina[i])
            //       found=false;
            // }
            // if (found) {
            //    int8_t matchCount = ffHasName(*pF, bina);
            //    flDbg(FL, "matchCount: %d", matchCount);
            //    goto insertEnd;
            // }
           skipFor:
            //uint level = thnsTree.insert(*tit, ina, 0, lx, ly);
            ++ic;
         }
         // Circle c;
         // c.nf=(FFJSON*)1;
         // printf("%s\n", (*tit)["location"].stringify().c_str());
         // thnsTree.print(c);
         ++tit;
      }
      ++it;
   }
}


int main (int argc, char** argv) {
   FFJSON cfg("file://http.ffjson|OBJECT");
   ccp wdir = cfg["rootdir"];
   Txo wcfg(string("file://")+wdir+"/config.txo|OBJECT");
   flDbg(TL, "wdir: %s", wdir);
   makeThngsTree(wcfg);
   Pts pts;
   string srchStr("flat");
   vector<string> mstr = metaname(srchStr);
   pts.ina=nametouint(mstr);
   pts.c = {77.7644869, 12.9940933, 10.5}; // flat
   flInf(TL, "c: %f,%f\n", pts.c.x, pts.c.y);
   thnsTree.print(pts.c);
   thnsTree.getPointsFromQuad(pts);
   std::vector<NdNPrn>::iterator it = pts.pts.begin();
   it = pts.pts.begin();
   auto itend = it+pts.pni;
   flNtc(TL, "pts matching %s: ", srchStr.c_str());
   while (it!=itend) {
      FFJSON* fp;
      if (it->prn==(QuadNode*)-1) {
         fp = (FFJSON*)it->qh;
      } else {
         fp = (FFJSON*)get<0>(getNode(*it));
      }
      printf("%p: %s\n", fp, (*fp)["location"].stringify().c_str());
      ++it;
   }
   return 0;
}
