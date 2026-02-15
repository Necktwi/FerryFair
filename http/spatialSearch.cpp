#include "spatialSearch.h"
#include <myconverters.h>
#include <metaphone3.h>
#include <memory>
#include <mutex>

const uint thnsPrSrch = 25;
QuadHldr thnsTree;
thread_local Metaphone3Encoder m3e;
vector<map<QuadNode*, uint>> qpmapvec;
map<set<FFJSON*>*, vector<uint>> mapffset;
map<string, FFJSON*>* nameints;
FFJSON* fnameints;
map<const string*, uint> mitpos;
mutex mitposMtx;
map<QuadHldr*, CntMut_> qhModMutMap;
mutex qhMapMut;

void ptswap (vector<NdNPrn>& pts, uint one, uint two) {
   NdNPrn temp = pts[one];
   pts[one]=pts[two];
   pts[two]=temp;
}
void quickSort (vector<NdNPrn>& pts, int start, int end) {
   if (end-start<=0) {
      return;
   } else if (end-start==1) {
      if (pts[start].ds>pts[end].ds) {
         ptswap(pts, start, end);
      }
   } else {
      float cmp = pts[start].ds;
      int lowind=start;
      int picker=start+1;
      while (picker<=end) {
         if (pts[picker].ds<cmp) {
            ptswap(pts, picker, lowind);
            ++lowind;
         }
         ++picker;
      }
      if (lowind-start >= 2) {
         quickSort(pts, start, lowind-1);
      }
      if (lowind==start) {
         ++lowind;
      }
      if (end-lowind>=1) {
         quickSort(pts, lowind, end);
      }
   }
}

vector<string> metaname (string name) {
   vector<string> r = explode(name);
   for (int k=0;k<r.size();++k) {
      r[k]= m3e.encode(r[k]).first;   
   }
   return r;
}
// returns vector of size >= size of qpmapvec
vector<uint> nametouint (vector<string>& mstr) {
   uint bitCode=0;
   vector<uint> r;
   for (int k=0;k<mstr.size();++k) {
      map<string,FFJSON*>::iterator it = nameints->lower_bound(mstr[k]);
      int d=0;
      if (it==nameints->end()) {
         d=nameints->size();
      } else {
         d = (int)mitpos[&it->first];
      }
      div_t bi = div(d,(8*sizeof(uint)));
      for (int i=r.size();i<=bi.quot;++i) {
         if (i>=qpmapvec.size()) {
            qpmapvec.push_back(map<QuadNode*, uint>());
         }
         r.push_back(0);
      }
      bitCode=1<<bi.rem;
      r[bi.quot]|=bitCode;
   }
   return r;
}
void xorina (vector<uint>& dina, vector<uint>& sina) {
   for (int i=0; i<sina.size(); ++i) {
      if (i==dina.size()) {
         dina.push_back(sina[i]);
      } else {
         dina[i] |= sina[i];
      }
   }
}
void xorinaname (vector<uint>& dina, ccp str) {
   vector<string> mstr = metaname(str);
   vector<uint> nina = nametouint(mstr);
   xorina(dina, nina);
}
template<typename T, typename U>
void* fpxor (T* a, U* b, int8_t ind) {
   size_t t = (size_t)(((size_t)a xor (size_t)b));
   int8_t* c = (int8_t*)&t;
   if (c[7]!=0) {
      printf("error: msbs are not equal\n");
   }
   c[7]=ind;
   return (void*)t;
}

template<typename T, typename U>
tuple<void*, int8_t> bpxor (T* a, U* b) {
   int8_t n = ((int8_t*)&a)[7];
   ((int8_t*)&a)[7]=0;
   void* m = (void*)(((size_t)a) xor (size_t)b);
   return tuple<void*, int8_t>{m, n};
}

tuple<void*,int8_t> getNode (NdNPrn n) {
   return bpxor(n.qh->qp, n.prn);
}

struct UintName {
   vector<uint> vu;
   vector<string> mwd;
};
bool CompNameWt::operator () (const map<string, FFJSON*>::iterator it1,
                              const map<string, FFJSON*>::iterator it2) const {
      return it1->second->val.number > it2->second->val.number;
}
CompNameWt cmpNmWt;

void QuadNode::seti (vector<uint>& ina) {
	for (int i=0; i<ina.size();++i) {
		if (ina[i]!=0) {
			uint& lni = qpmapvec[i][this];
			lni |= ina[i];
			// if (i==0 && lni&0x80) {
			//    printf("apple set\n");
         // }
      }
   }
}
bool QuadNode::copyi (vector<uint>& ina) {
   bool changed = false;
   for (int i=0; i<ina.size();++i) {
      map<QuadNode*, uint>::iterator it = qpmapvec[i].find(this);
      if (it != qpmapvec[i].end()) {
         if (ina[i]) {
            uint& lni = it->second;
            if (lni!=ina[i]) {
               lni = ina[i];
               changed=true;
            }            
         } else {
            qpmapvec[i].erase(this);
         }
      } else {
         if (ina[i]) {
            uint& lni = qpmapvec[i][this];
            lni = ina[i];
            changed=true;
         }         
      }
   }
   return changed;
}
vector<map<QuadNode*,uint>::iterator> qpfind (QuadNode* qp) {
   vector<map<QuadNode*, uint>::iterator> vit;
   map<QuadNode*, uint>::iterator it;
   for (int i=0; i<qpmapvec.size();++i) {
      it = qpmapvec[i].find(qp);
      if (it!=qpmapvec[i].end()) {
         for (int j=vit.size(); j<i;++j) {
            vit.push_back(qpmapvec[j].end());
         }
         vit.push_back(it);
      }
   }
   return vit;
}

bool isQuad (map<QuadNode*,uint>::iterator it) {
   return qpmapvec[0].end()!=it;
}
QuadNode* QuadHldr::qn () {
   QuadNode* r=nullptr;
   for (int i=0; i<qpmapvec.size(); ++i) {
      r = qpmapvec[i].upper_bound((QuadNode*)(this-4))->first;
      uint dist = (uint)(this-(QuadHldr*)r);
      if (dist <4) {
         return r;
      }
   }
   return r;
}
uint QuadNode::hasName (vector<uint>& ina,
                        vector<map<QuadNode*,uint>::iterator> vit, bool allIna
) {
   if (!ina.size()) {
      return -1;
   }
   uint count=vit.size();
   uint size = ina.size();
   if (size>count) {
      if (allIna)
         return 0;
      size=count;
   }
   count = 0;
   for (int i=0;i<size;++i) {
      uint mchBits = ina[i] & vit[i]->second;
      if (!allIna || (mchBits==ina[i]))
         count += countSetBits(mchBits);
      else
         return 0;
   }
   return count;
}

vector<uint> qpIna (vector<map<QuadNode*,uint>::iterator> vit) {
   vector<uint> ina;
   for (int i=0;i<vit.size();++i) {
      ina.push_back(vit[i]->second);
   }
   return ina;
}

int8_t ffHasName (FFJSON& ff, vector<uint>& ina, bool allIna) {
   if (!ina.size()) {
      return -1;
   }
   string tname((ccp)ff["name"]);
   tname += " ";
   tname += (ccp)ff["user"]["name"];
   vector<string> mstr = metaname(tname);
   vector<uint> nina = nametouint(mstr);
   int8_t count=0;
   int smallest=ina.size();
   if(smallest>nina.size()) {
      if (allIna)
         return 0;
      smallest=nina.size();
   }
   for (int i=0;i<smallest;++i) {
      uint mchBits = ina[i] & nina[i];
      if (!allIna || (mchBits == ina[i]))
         count += countSetBits(mchBits);
      else
         return 0;
   }
   return count;
}

vector<uint> QuadHldr::getIntNames (QuadNode* tQN, uint8_t tind,
                                    QuadNode* pQN, uint8_t ind) {
   vector<uint> r;
   if (!fp) {
      return r;
   }
   QuadNode* resfp = (QuadNode*)get<0>(bpxor(fp, pQN));
   uint a=0;
   for (uint i=0; i<qpmapvec.size(); ++i) {
      map<QuadNode*, uint>::iterator it = qpmapvec[i].find(resfp);
      if (it!=qpmapvec[i].end()) {
         r.push_back(it->second);
         a|=it->second;
      } else {
         r.push_back(0);
      }
   }
   if (!a) {
      set<FFJSON*>* ressfp = (set<FFJSON*>*)resfp;
      map<set<FFJSON*>*, vector<uint>>::iterator sit = mapffset.find(ressfp);
      bool isS=sit != mapffset.end();
      if (isS) {
         r = sit->second;
      } else {
         FFJSON& tfp = *(FFJSON*)resfp;
         vector<string> mstr = metaname((ccp)tfp["name"]);
         r=nametouint(mstr);
      }
   }
   return r;
}

bool QuadNode::updateIntNames (QuadNode* tQN, uint8_t tind,
                               QuadNode* pQN, uint8_t ind) {
   QuadHldr* qh = (QuadHldr*)this;
   vector<uint> r;
   for (int8_t i=0;i<4;++i,++qh) {
      vector<uint> lr = qh->getIntNames(this, i, tQN, tind);
      for (int8_t j=0;j<lr.size();++j) {
         if (j==r.size()) {
            r.push_back(0);
         }
         uint& ui = r[j];
         ui|=lr[j];
      }
   }
   return copyi(r);
}
void QuadNode::destroy (QuadNode* pQN) {
	QuadHldr* qh= (QuadHldr*)this;
	for (int i=0; i<4; ++i) {
		qh->destroy(pQN, this);
		++qh;
	}
}
QuadNode::~QuadNode () {
   map<QuadNode*,uint>::iterator qit = qpmapvec[0].find(this);
   if (qit!=qpmapvec[0].end()) {
      qpmapvec[0].erase(qit);
   }
}
void QuadHldr::destroy (QuadNode* pQN, QuadNode* tQN) {
	if (!fp)
		return;
	QuadNode* resfp;
   set<FFJSON*>* ressfp;
	vector<map<QuadNode*,uint>::iterator> qit;
   resfp = (QuadNode*)get<0>(bpxor(fp, pQN));
   ressfp = (set<FFJSON*>*)resfp;
	
	qit = qpfind((QuadNode*)resfp);
   if (qit.size()) {
		resfp->destroy(tQN);
		delete resfp;
	} else if (mapffset.find(ressfp)!=mapffset.end()) {
		delete ressfp;
	}
	fp= nullptr;
}
QuadHldr::~QuadHldr () {
	destroy();
}
CntMut_& QuadHldr::lock (int l) {
   qhMapMut.lock();
   CntMut_& cntMut = qhModMutMap[this];
   ++cntMut.count;
   qhMapMut.unlock();
   cntMut.m.lock();
   return cntMut;
}

void QuadHldr::unlock (CntMut_* p = nullptr) {
   qhMapMut.lock();
   if (!p)
      p = &qhModMutMap[this];
   --p->count;
   qhMapMut.unlock();
   p->m.unlock();
}

uint QuadNode::insert (FFQuad_& fq, float x, float y, uint level,
                       QuadNode* pQN, int8_t ind, int8_t sn) {
   QuadHldr* qh = (QuadHldr*)this;
   CntMut_* pcm = nullptr;
   if (!sn) {
      pcm = &qh->lock(__LINE__);
      this->seti(fq.ina);
      qh->unlock(pcm);
   }
   uint returnv= 0;
   int8_t qind= fq.lx>= x ? 0: 1;
   int8_t xs= qind== 0 ? 1: -1;
   qind<<= 1;
   int8_t ys= fq.ly>= y ? 1: -1;
   qind|= ys>= 0 ? 0: 1;
   flDbg(SL, "%p, %f, %f, %d, %d, %d\n", this, x, y, xs, ys, qind);
   qh+= qind;
   float dx= 180/(pow(2, level+1));
   float dy= 90/(pow(2, level+1));
   return qh->insert(fq, x+xs*dx, y+ys*dy, level+1, this, qind, pQN, ind, sn);
}

uint QuadHldr::insert (
   FFQuad_& fq, float x, float y, uint level, QuadNode* tQN, int8_t tind,
   QuadNode* pQN, int8_t ind, int8_t sn
) {
   FFJSON& rF=fq.rF;vector<uint>& ina=fq.ina;float& lx=fq.lx,ly=fq.ly;
   bool& deleteLeaf = fq.deleteLeaf;
   uint ret= level;
   map<set<FFJSON*>*, vector<uint>>::iterator sit;
   vector<map<QuadNode*,uint>::iterator> qit;
   void* resfp;
   set<FFJSON*>* ressfp;
   ffl_debug(SL, "qh: %p, x,y: %lf,%lf\n", this, x, y);
#ifdef _DEBUG
   if (&rF==(void*)0x5555556a4980) {
      flDbg(SLL, "qh: %p", this);
   }
#endif
   CntMut_* pcm= nullptr;
	pcm= &lock(__LINE__);
   if (fp==nullptr) {
      fp= (FFJSON*)fpxor(&rF, pQN, ind);
		//rF could b a set, don't access it. we are cheating by not writing
		//redundant function for set which we insert only in a new quadnode
		flDbg(SL, "rF:%p, %f, %f inserted\n", &rF, lx, ly);
      goto retn;
   }
   resfp = get<0>(bpxor(fp, pQN));
   ressfp = (set<FFJSON*>*)resfp;
   if (!sn) {
      qit = qpfind((QuadNode*)resfp);
   }
	sit = mapffset.find(ressfp);
   if (!qit.size() || sn) {
      bool isS = sit != mapffset.end();
      if (deleteLeaf) {
         if (!isS && resfp == (void*)&rF) {
            fp=nullptr;
            ret = 1;
            goto retn;
         } else if (isS) {
            set<FFJSON*>::iterator it = ressfp->find(&rF);
            if (it!=ressfp->end()) {
               ressfp->erase(it);
            }
            if (!ressfp->size()) {
               delete ressfp;
               fp=nullptr;
               ret = 1;
               goto retn;
            } else {
               ret = 0;
               goto retn;
            }
         }
         ret = 0;
         goto retn;
      }
      if (resfp == (void*)&rF) {
         ffl_debug(SL, "rF: %p inserted in %p\n", &rF, pQN);
         goto retn;
      }
      FFJSON& tmp= isS? **ressfp->begin() : *(FFJSON*)resfp;
      ffl_debug(SL, "tfp: %p,%p,%p\n", &tmp, fp, pQN);
      float llx= (float)tmp["location"][1];
      float lly= (float)tmp["location"][0];
      if (llx==lx && lly==ly) {
         if (!isS) {
            string tname((ccp)tmp["name"]);
            tname+= " ";
            tname+= (ccp)tmp["user"]["name"];
            xorinaname(ina, tname.c_str());
            sp= new set<FFJSON*>();
            mapffset[sp]= ina;
            sp->insert(&tmp);
            sp->insert(&rF);
            sp= (set<FFJSON*>*)fpxor(sp, pQN, ind);
         } else {
            xorina(sit->second, ina);
            ressfp->insert(&rF);
         }
         ffl_debug(SL, "rF: %p inserted in %p\n", &rF, pQN);
         goto retn;
      }
      qp = new QuadNode();
      if (!sn) {
         if (isS) {
            xorina(ina, sit->second);
         } else {
            string tname((ccp)tmp["name"]);
            tname += " ";
            tname += (ccp)tmp["user"]["name"];
            xorinaname(ina, tname.c_str());
         }
      }
      qp->seti(ina);
      FFQuad_ tfq(*(FFJSON*)resfp, ina, llx, lly, deleteLeaf);
		//we are not implementing a redundant function for set; we r clever
		qp->insert(tfq, x, y, level, tQN, tind, 1);	
		ret = qp->insert(fq, x, y, level, tQN, tind, 1);
      qp = (QuadNode*)fpxor(qp,pQN,ind);
      ffl_debug(SL, "rF: %p inserted in %p\n", &rF, pQN);
      goto retn;
   } else {
      QuadNode* qpres = (QuadNode*)resfp;
      if (!deleteLeaf)
         if (pcm) {
            unlock(pcm);
            pcm=nullptr;
         }
      ret = qpres->insert(fq, x, y, level, tQN, tind, sn);
      if (deleteLeaf) {
         if (ret) {
            QuadHldr* qh = (QuadHldr*)qpres;
            FFJSON* pxorrf = nullptr;
            if (ret>1) {
               int8_t qind = 0;
               int8_t xs=0;
               for (;qind<4;++qind,++qh) {
                  if (qh->fp!=nullptr) {
                     ++xs;
                     if (xs>1) {
                        ret = 1;
                        goto retn;
                     }
                     pxorrf=(FFJSON*)qh;
                  }
               }
               if (xs) {
                  qh=(QuadHldr*)pxorrf;
                  pxorrf=(FFJSON*)get<0>(bpxor(qh->fp, tQN));
                  delete qpres;
                  qp = (QuadNode*)fpxor(pxorrf, pQN,ind);
               } else {
                  delete qpres;
                  qp=nullptr;
                  ret = 2;
                  goto retn;
               }
            }
            ret = qpres->updateIntNames(tQN,tind,pQN,ind);
            goto retn;
         }
         ret = 0;
         goto retn;
      }
      ffl_debug(SL, "rF: %p,%s:%p inserted\n", &rF,
                rF["location"].stringify().c_str(), pQN);
      return ret;
   }
  retn:
   if (pcm)
      unlock(pcm);
   return ret;
}
bool Circle::grabIfNearest (FFJSON& f) {
   if (!nf) {
      nf=&f;
      return true;
   }
   float x1 = x - (double)f["location"][1];
   float y1 = y - (double)f["location"][0];
   float x2 = x - (double)(*nf)["location"][1];
   float y2 = y - (double)(*nf)["location"][0];
   if ((pow(x1,2)+pow(y1,2)) < (pow(x2,2)+pow(y2,2))) {
      nf=&f;
      return true;
   }
   return false;
}
void QuadHldr::print (Circle& c, uint level, QuadNode* tQN, uint8_t tind,
                      QuadNode* pQN, uint8_t ind) {
   if (fp==nullptr) {
      printf("%.*s%d: %p(%p)\n", level,"|||||||||||||||||||||||||||||||||||||||||",
             tind, this, nullptr);
      return;
   }
   QuadNode* resqp = (QuadNode*)get<0>(bpxor(fp,pQN));
   vector<map<QuadNode*,uint>::iterator> qit = qpfind((QuadNode*)resqp);
   if (!qit.size()) {
      set<FFJSON*>* ressfp = (set<FFJSON*>*)resqp;
      map<set<FFJSON*>*, vector<uint>>::iterator sit = mapffset.find(ressfp);
      bool isS=sit != mapffset.end();
      if (isS) {
         set<FFJSON*>& sf = *sit->first;
         resqp=(QuadNode*)*sf.begin();
      }
      FFJSON& f = *(FFJSON*)resqp;
      if (c.nf!=(FFJSON*)1)
         c.grabIfNearest(f);
      printf("%.*s%d: %p(%p(%d))%s\n",level,
             "||||||||||||||||||||||||||||||||||||||||||||||||||||||||||",
             tind, this, &f, isS, f["location"].stringify().c_str());
   } else {
      QuadHldr* qh = (QuadHldr*)resqp;
      printf("%.*s%d: %p(%p)\n", level,"|||||||||||||||||||||||||||||||||||||||||",
             tind, this, qh);
      for (int i=0;i<4;++i,++qh) {
         qh->print(c, level+1, resqp,i,tQN,tind);
      }
   }
}

void NdNPrn::print () const {
   auto aa = getNode(*this);
   printf ("%p((%d)%p),%d,%d-%f\n", qh, qh->fp!=nullptr, get<0>(aa), d.x,
           d.y,ds);
   fflush(stdout);
}
void printpts (const vector<NdNPrn>& pts) {
   for (int i=0; i<pts.size(); ++i) {
      const NdNPrn& nd = pts[i];
      nd.print();
   }
}

QuadNode::QuadNode () {
   en.fp=nullptr;
   es.fp=nullptr;
   wn.fp=nullptr;
   ws.fp=nullptr;
}

int QuadHldr::addThis (Pts& pts, Direction d, float dx, float ds,
                        QuadNode* pQN , uint8_t ind, int noChk) {
   NdNPrn n = {this, pQN, dx, ds, d, ind};
   bool there = false;
   if (noChk==1) {
      pts.pts.push_back(n);
      return 1;
   }
   for (int i=pts.pts.size()-1; i>pts.ni; --i) {
      if (pts.pts[i].qh == this) {
         NdNPrn nd = pts.pts[i];
         if (noChk==-1 && nd.d.x==d.x and nd.d.y==d.y) {
            return -1;
         }
         NdNPrn nd2;
         if (i>0 && pts.pts[i-1].qh == this) {
            nd2 = pts.pts[i-1];
         }
         pts.pts.erase(
            nd2.qh?pts.pts.begin()+(i-1):pts.pts.begin()+i, pts.pts.begin()+i+1);
         if ((d.x!=0 && nd.d.x==-d.x) || (d.y!=0 && nd.d.y==-d.y)) {
            pts.pts.push_back(nd);
            if (nd2.qh) {
               pts.pts.push_back(nd2);
            } else {
               pts.pts.push_back(n);   
            }
            return 1;
         } else if (nd2.qh && ((d.x!=0 && nd2.d.x==-d.x) ||
                               (d.y!=0 && nd2.d.y==-d.y))) {
            pts.pts.push_back(nd2);
            pts.pts.push_back(n);   
            return 1;
         } else {
            if (!nd2.qh && n.d.x!=0&&n.d.y!=0) {
               nd.d.x=n.d.x;
               nd.d.y=n.d.y;
            }
            if (nd2.qh) {
               pts.pts.push_back(nd2);
            }
            pts.pts.push_back(nd);
            return 1;
         }
      }
   }
   if (!there) {
     nothere:
      pts.pts.push_back(n);
   }
   return 1;
}

uint QuadHldr::addChildrenOnEdge (
   Pts& pts, Direction d, QuadNode* pQN, uint8_t ind, float dx, float ds,
   bool noChk
) {
   if (!qp) {
      uint ncnt = addThis(pts, {(int8_t)-d.x,(int8_t)-d.y}, dx, ds,
                          pQN,ind,noChk);
      return ncnt;
   }
   QuadNode* resqp = (QuadNode*)get<0>(bpxor(fp, pQN));
   vector<map<QuadNode*,uint>::iterator> qit = qpfind((QuadNode*)resqp);
   if (!(qit.size() && resqp->hasName(pts.ina,qit,pts.op==AND))) {
      uint ncnt = addThis(pts, {(int8_t)-d.x,(int8_t)-d.y}, dx, ds,
                          pQN,ind,noChk);
      return ncnt;
   }
   uint c = 0;
   uint8_t lind=0;
   QuadHldr* qh = &resqp->en;
   QuadNode* tQN;
   tQN=qn();
   uint8_t tind = this-(QuadHldr*)tQN;
   int8_t ix;
   int8_t iy;
   vector<uint8_t> vind;
   if (d.x!=0) {
      ix=d.x==1?0:1;
      if (d.y!=0) {
         iy=d.y==1?0:1;
         lind=ix<<1|iy;
         vind.push_back(lind);
      } else {
         iy=0;
         lind=ix<<1|iy;
         vind.push_back(lind);
         iy=1;
         lind=ix<<1|iy;
         vind.push_back(lind);
      }
   } else {
      ix=0;
      iy=d.y==1?0:1;
      lind=ix<<1|iy;
      vind.push_back(lind);
      ix=1;
      lind=ix<<1|iy;
      vind.push_back(lind);
   }
   int nni = pts.nni;
   pts.nni = pts.pts.size()-1;
   for (int i=0;i<vind.size();++i) {
      lind = vind[i];
      qh = (QuadHldr*)resqp+lind;
      if (qh->fp) {
         QuadNode* resqp = (QuadNode*)get<0>(bpxor(qh->fp, tQN));
         vector<map<QuadNode*,uint>::iterator> qit =
            qpfind((QuadNode*)resqp);
         if (qit.size() && resqp->hasName(pts.ina,qit,pts.op==AND)) {
            int z = qh->addChildrenOnEdge(pts, d, tQN, tind, dx/2,
                                          ds-dx/4,-1);
            if (z==-1) {
               return -1;
            }
            c+=z;
            continue;
         }
      }
      int z = qh->addThis(pts, {(int8_t)-d.x,(int8_t)-d.y}, dx/2, ds-dx/4, tQN,
                          tind, -1);
      if (z==-1) {
         return -1;
      }
      ++c;
   }
   pts.nni = nni;
   return c;
}
uint QuadHldr::findNeighbours (Pts& pts, QuadNode* tQN, uint8_t tind,
                               QuadNode* pQN, uint8_t ind, float dx, float ds,
                               Direction d, bool notChild) {
   if (tQN==nullptr) {      
      return 0;
   }
   short sign= 1;
   short minus= -1;
   QuadNode* resqp = (QuadNode*)get<0>(bpxor(fp, pQN));
   QuadHldr* tQH = (QuadHldr*)tQN;
   uint8_t ix=0;
   int8_t lind = tind;
   ix = lind;
   uint8_t iy = 1&ix;
   ix>>=1;
   uint ncnt = 0;
   int nni;
   if (pts.cnd.ds) {
      pts.cnd.ds=0;
      goto cntnuFind;
   }
   // determine the neighbour quadrant
   if (d.x!=0 || d.y!=0) {
      int8_t rx = ix-d.x;//quadrant index is opposite to direction
      int8_t ry = iy-d.y;
      lind = rx<<1|ry;
      if (rx>1 || rx<0 || ry>1 || ry<0) {
         // goto parent
         if (pQN == nullptr) {
            return 0;
         }
         QuadHldr* pQH = &pQN->en+ind;
         auto tuppqh = bpxor(pQH->fp, tQN);
         QuadNode* ppQN = (QuadNode*)get<0>(tuppqh);
         //printf("fn:ppQN:%p\n", ppQN);
         lind=get<1>(tuppqh);
         uint ptscnt = pQH->findNeighbours(
            pts, pQN, ind, (QuadNode*)ppQN, lind, 2*dx, ds,
            {(int8_t)((rx>1||rx<0)?d.x:0),(int8_t)((ry>1||ry<0)?d.y:0)}, true);
         if (ptscnt && ptscnt!=-1) {
            NdNPrn ndprn = pts.pts.back();
            pts.pts.pop_back();
            tuppqh = getNode(ndprn);
            QuadHldr* presqp = (QuadHldr*)get<0>(tuppqh);
            int8_t iix, iiy, lind, pind;
            vector<map<QuadNode*,uint>::iterator> qit =
               qpfind((QuadNode*)presqp);
            if (!(qit.size() && ((QuadNode*)presqp)->
                  hasName(pts.ina,qit,pts.op==AND))||ndprn.qh->fp==nullptr) {
               ndprn.qh->addThis(pts, d, ndprn.dx, ndprn.ds, ndprn.prn,
                                 ndprn.ind);
               if (notChild) {
                  return -1;
               } else {
                  return 1;
               }
            }
            iix = d.x==1?1:(d.x==0?ix:0);
            iiy = d.y==1?1:(d.y==0?iy:0);
            lind = iiy|iix<<1;
            presqp+=lind;
            ppQN = ndprn.qh->qn();
            pind = ndprn.qh-(QuadHldr*)ppQN;
            if (notChild) {
               ncnt =
                  presqp->addThis(pts,d, dx, ds+dx/2, ppQN,pind, true);
            } else {
               int z =presqp->addChildrenOnEdge(
                  pts, {(int8_t)-d.x,(int8_t)-d.y}, ppQN, pind, dx, ds+dx/2);
               if (z==-1) {
                  ncnt+=0;
               }
            }
         } else if (ptscnt==-1) {
            if (d.x!=0 && d.y!=0) {
               NdNPrn& n = pts.pts[pts.pts.size()-1];
               if (pts.pts.size()>=2 && (d.x==-n.d.x || d.y==-n.d.y)) {
                  NdNPrn& n2 = pts.pts[pts.pts.size()-2];
                  if (n.qh!=n2.qh) {
                     pts.pts.push_back(n);
                     n.d=d;
                  }
               } else {
                  n.d=d;
               }
            }
            if (notChild) {
               return -1;
            } else {
               return 1;
            }
         }
      } else {
         tQH+=lind;
         if (notChild) {
            ncnt=tQH->addThis(pts,d,dx,ds+dx/2,pQN,ind, true);
         } else {
            int z = tQH->addChildrenOnEdge(pts, {(int8_t)-d.x,(int8_t)-d.y},
                                           pQN, ind, dx, ds+dx/2);
            if (z==-1) {
               ncnt+=0;
            }
         }
      }
   } else {
      pts.ni= pts.pts.size()-1;
      ix = ix==0?1:-1;
      iy = iy==0?1:-1;
      for (short i=-1; i <=1; ++i) {
         for (short j=-1; j <= 1; ++j) {
            if (i==0 && j==0)
               continue;
            d.x=i; d.y=j;
            ncnt+=findNeighbours(pts, tQN, tind, pQN, ind, dx, dx/2, d);
         }
      }
      //printpts(pts.pts);
      nni = pts.pts.size()-1;
      pts.nni = nni;
      if (nni<0)
         return 0;
      ++pts.ni;
      quickSort(pts.pts,pts.ni,pts.nni);
      //printf("---------\n");
      //printpts(pts.pts);
      //printf("---------\n");
     cntnuFind:
      while (pts.ni<pts.pts.size() && pts.pni<pts.minPts) {
         NdNPrn nd = pts.pts[pts.ni];
         flDbg(SLL, "pts.ni: %d, nd.qh: %p", pts.ni, nd.qh);
         tQN= nd.qh->qn();
         tind= nd.qh-(QuadHldr*)tQN;
         //pts.nni=pts.pts.size();
         ncnt+=nd.qh->findNeighbours(
            pts, tQN, tind, nd.prn, nd.ind, nd.dx, nd.ds, nd.d);
         if (nd.d.x!=0 && nd.d.y!=0) {
            Direction dd = nd.d;
            dd.x= 0;
            //pts.nni=pts.pts.size();
            ncnt+=nd.qh->findNeighbours(
               pts, tQN, tind, nd.prn, nd.ind, nd.dx, nd.ds, dd);
            dd.x = nd.d.x;
            dd.y=0;
            //pts.nni=pts.pts.size();
            ncnt+=nd.qh->findNeighbours(
               pts, tQN, tind, nd.prn, nd.ind, nd.dx, nd.ds, dd);
         }
         quickSort(pts.pts, pts.ni+1, pts.pts.size()-1);
         if (nd.qh->fp && pts.ni>=pts.pni) {
            QuadNode* resqp= (QuadNode*)get<0>(getNode(nd));
            vector<map<QuadNode*,uint>::iterator> qit=
               qpfind((QuadNode*)resqp);
            if (!qit.size()) {
               set<FFJSON*>* ressfp= (set<FFJSON*>*)resqp;
               map<set<FFJSON*>*, vector<uint>>::iterator sit=
                  mapffset.find(ressfp);
               bool isS= sit!=mapffset.end();
               if (isS) {
                  set<FFJSON*>& sf= *sit->first;
                  set<FFJSON*>::iterator sfit= sf.begin();
                  int moreElms= sf.size();
                  while (sfit!=sf.end()) {
                     //break;
                     --moreElms;
                     int8_t matchcount= ffHasName(**sfit, pts.ina,
                                                  pts.op==AND);
                     if (matchcount) {
                        pts.pts[pts.pni]= {(QuadHldr*)*sfit,(QuadNode*)-1,
                           nd.dx,nd.ds,{matchcount,0},0};
                        if (pts.pni+moreElms>pts.ni) {
                           pts.pts.insert(
                              pts.pts.begin()+pts.pni+1,
                              moreElms,{0});
                           pts.ni+=moreElms;
                        }
                        ++pts.pni;
                     }
                     ++sfit;
                  }
               } else {
                  uint8_t matchcount =
                     (uint8_t)ffHasName((*(FFJSON*)resqp),pts.ina,pts.op==AND);
                  if (matchcount) {
                     pts.pts[pts.pni] = pts.pts[pts.ni];
                     pts.pts[pts.pni].d.x = matchcount;
                     ++pts.pni;
                  }
               }
            }
         }
         ++pts.ni;
      }
      // for (int i=0; i< pts.pts.size()-1;++i) {
      //    for (int j=i+1; j<pts.pts.size(); ++j) {
      //       if (pts.pts[i].qh==pts.pts[j].qh &&
      //           !((pts.pts[i].d.x!=0 && pts.pts[i].d.x == -pts.pts[j].d.x)
      //            || (pts.pts[i].d.y!=0 && pts.pts[i].d.y ==
      //                -pts.pts[j].d.y))) {
      //          printf("%d:%p(%d,%d) == %d:%p(%d,%d)\n",
      //                 i, pts.pts[i].qh, pts.pts[i].d.x, pts.pts[i].d.y,
      //                 j, pts.pts[j].qh, pts.pts[j].d.x, pts.pts[j].d.y);
      //       }
      //    }
      // }
      // printpts(pts.pts);
      //pts.pts.erase(pts.pts.begin()+pni, pts.pts.end());
   }
   return ncnt;
}


uint QuadHldr::getPointsFromQuad (
   Pts& pts, uint level, float x, float y, QuadNode* tQN,
   int8_t tind, QuadNode* pQN, int8_t ind
) {
   flDbg(SLL, "qh: %p", this);
   float dx= 180/(pow(2,level+1));
   if (fp==nullptr) {
      pts.cnd= {this, pQN, dx, 0, 0, ind};
      return findNeighbours(pts, tQN, tind, pQN, ind, dx);
   }
   void* resfp= get<0>(bpxor(fp,pQN));
   vector<map<QuadNode*,uint>::iterator> qit= qpfind((QuadNode*)resfp);
   if (!qit.size()) {
      set<FFJSON*>* ressfp = (set<FFJSON*>*)resfp;
      map<set<FFJSON*>*, vector<uint>>::iterator sit = mapffset.find(ressfp);
      bool isS = sit != mapffset.end();
      if (isS) {
         set<FFJSON*>& sf = *sit->first;
         set<FFJSON*>::iterator sfit = sf.begin();
         while (sfit!=sf.end()) {
            int8_t matchcount = ffHasName(**sfit, pts.ina,pts.op==AND);
            if (matchcount) {
               pts.pts.push_back(
                  {(QuadHldr*)*sfit,(QuadNode*)-1,dx,dx,{matchcount,0},0});
               ++pts.pni;
            }
            ++sfit;
         }
      } else if (ffHasName(*(FFJSON*)resfp, pts.ina,pts.op==AND)) {
         pts.pts.push_back({this,pQN});
         ++pts.pni;
      }
      pts.cnd = {this, pQN, dx, 0, 0, ind};
      return findNeighbours(pts, tQN, tind, pQN, ind, dx);
   } else {
      QuadNode* resqp = (QuadNode*)resfp;
      uint8_t matchcount = (uint8_t)resqp->hasName(pts.ina,qit,pts.op==AND);
      if (!matchcount) {
         pts.cnd = {this, pQN, dx, 0, 0, ind};
         return findNeighbours(pts, tQN, tind, pQN, ind, dx);
      }
      QuadHldr* qh = &resqp->en;
      int xsign=1;
      int ysign=1;
      int minus=-1;
      int8_t lind=0;
      float dy = 90/(pow(2,level+1));
      for (lind=0;lind<4; ++lind, ++qh, xsign*=ysign, ysign*=minus) {
         if (xsign*pts.c.x>=xsign*x && ysign*pts.c.y>=ysign*y) {
            break;
         }
      }
      float cx = x+xsign*dx;
      float cy = y+ysign*dy;
      return qh->getPointsFromQuad(pts, level+1, cx, cy,
                                   resqp, lind, tQN, tind);
   }
   return level;
}
