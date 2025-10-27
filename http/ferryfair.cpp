#include <atomic>
#include <string>
#include <chrono>
#include <filesystem>
#include <condition_variable>
#include <mutex>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <unistd.h>
// #include <string.h>
// #include <cctype>
// #include <cstring>
#include <myconverters.h>
#include <mystdlib.h>
#include "https.h"
#include "ferryfair.h"
#include "spatialSearch.h"
#include "cap.h"
#include "smtpClient.h"

using namespace std;

bool valgrind_test = false;
int valgrind_count = 1;
mutex qhModMtx;
unique_lock<mutex> modLk(qhModMtx);
condition_variable cvMod, cvSrch;
atomic<int> searchCv{0};
atomic<int> modQhCv{0};

struct CompThingNameMatch {
   bool operator () (const tuple<FFJSON*,int8_t>& t1,
                     const tuple<FFJSON*,int8_t>& t2) const {
      return (get<1>(t1) < get<1>(t2));
   }
};
int getIdChildInd (FFJSON& arr, int id) {
   int last = arr.size;
   last = id<last?id:last;
   for (int i=last-1;i>=0;++i) {
      if ((int)arr[i]["id"]==id) {
         return i;
      }
   }
   return -1;
}

struct BidThings_ {
   set<FFJSON*> mdts;
   Pts all;
   Pts search;
};
map<FFJSON*, BidThings_> bidThings;
thread_local ccp to = nullptr;
thread_local char subj[64];
thread_local char mesg[128];
int addSmtgsToReply (FFJSON& users, FFJSON& user, FFJSON& r,
                     set<FFJSON*>& mdts, bool usr = true) {
   if (usr) {
      FFJSON q("{things:!}");//all all keys of user except things to r;
      user.answerObject(&q, nullptr, FerryTimeStamp(), &r);
   }
   FFJSON& rts = r["things"];
   FFJSON& uts = user["things"];
   int k=rts.size;
   int ik=k;
   for (uint i = 0; i<uts.size; ++i) {
      FFJSON* f = &uts[i];
      if (to) {
         if (strcmp((*f)["user"]["name"], to))
            continue;
      }
      set<FFJSON*>::iterator it = mdts.find(f);
      if (it==mdts.end()) {
         rts[k]=f;
         ++k;
         mdts.insert(f);
      }
   }
   FFJSON::Iterator stit = user.find("smsgs");
   if (stit!=user.end()) {
      FFJSON& smsgs = *stit;
      FFJSON& rsmsgs = r["smsgs"];
      for (int i=0; i<smsgs.size; ++i) {
         FFJSON& s = smsgs[i];
         if (!s[0].size)
            continue;
         FFJSON& uts = users[(ccp)s[0]]["things"];
         int tind = getIdChildInd(uts, (int)s[1]);
         FFJSON* f = &uts[tind];
         set<FFJSON*>::iterator it = mdts.find(f);
         if (it==mdts.end()) {
            rts[k]=f;
            ++k;
         }
      }
   }
   return k-ik;
}

void addSearchNoDups (Pts& pts, FFJSON& reply, set<FFJSON*>& mdts, bool dupLnks = true) {
   CompThingNameMatch cTNM;
   multiset<tuple<FFJSON*, int8_t>, CompThingNameMatch> score(cTNM);
   int k=0;
   for (int i=0;i<pts.pni;++i) {
      NdNPrn& nd = pts.pts[i];
      FFJSON* f;
      if (nd.prn==(QuadNode*)-1) {
         f = (FFJSON*)nd.qh;
      } else {
         auto aa = getNode(nd);
         f = (FFJSON*)get<0>(aa);
      }
      score.insert({f,nd.d.x});
   }
   multiset<tuple<FFJSON*, int8_t>, CompThingNameMatch>::iterator it=
      score.begin();
   while (it!=score.end()) {
      FFJSON& f = *get<0>(*it);
      bool thingIsWithUser = mdts.find(&f)!=mdts.end();
      if (dupLnks && thingIsWithUser) {
         FFJSON& rt = reply["things"][k];
         rt["id"]=f["id"];
         rt["user"]=&f["user"]["name"];
      } else {
         reply["things"][k]=&f;
      }
      ++k;++it;
   }  
}

bool isValidEmail (FFJSON& tname) {
   if (tname.isType(FFJSON::STRING) && tname.size<48) {
      ccp ctname = tname;
      for (uint i=0; i<tname.size; ++i) {
         char c = ctname[i];
         if (!((c>='a' && c<='z') || (c>='0' && c<='9') ||
               c=='@' || c=='_' || c=='.')) {
            return false;
         }
      }
      if (strstr(ctname, "<script")) {
         return false;
      }
   }
   return true;
}
bool isValidThingName (FFJSON& tname) {
   if (tname.isType(FFJSON::STRING) && tname.size>0 && tname.size<=64) {
      ccp ctname = tname;
      for (uint i=0; i<tname.size; ++i) {
         char c = ctname[i];
         if (c==' ') {
            if (i+1<tname.size) {
               c = ctname[i+1];
               if (c==' ') {
                  return false;
               }
            }
         } else if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
                      (c>='0' && c<='9') || (c=='-' || c=='.'))) {
            return false;
         }
      }
   }
   return true;
}

bool isValidThingDetails (FFJSON& tname) {
   if (tname.isType(FFJSON::STRING) && tname.size<=256) {
      ccp ctname = tname;
      for (uint i=0; i<tname.size; ++i) {
         char c = ctname[i];
         if (!((c>=' ' && c<='~') || c=='\n')) {
            return false;
         }
      }
      if (strstr(ctname, "<script")) {
         return false;
      }
   }
   return true;
}

bool isValidLocation (FFJSON& cloc) {
   if (cloc.isType(FFJSON::ARRAY) && cloc.size==2) {
      if (cloc[0].isType(FFJSON::NUMBER) && cloc[1].isType(FFJSON::NUMBER)) {
         return true;
      }
   }
   return false;
}
ccp admin, adminPass;
static ccp from = "FerryFair";
string wdir;
FFJSON* pffcfg = nullptr;
FFJSON* prbs = nullptr;
FFJSON* pusers = nullptr;
ccp mailServer = nullptr;
int mailPort = 0;

bool s_quit = false;
static ccp jsonMime = "text/json";
static ccp txtMime = "text/plain";

ccp yay = "{\"error\":\"yay\"}";

string ferryfair (FFJSON& ffHttp) {
   FFJSON reply, user, rbsid;
   static FFJSON& ffcfg = *pffcfg;
   static FFJSON& rbs = *prbs;
   static FFJSON& users = *pusers;
   ccp referer=nullptr;char proto[8]="https"; int protolen;
   ccp username = nullptr, password = nullptr, cpld = nullptr;
   ccp path;
   string bid;
   FFJSON& cookie = ffHttp["cookie"];
   FFJSON payload;
   ffl_notice(FL, "cookie[bid]: %s",(ccp)cookie["bid"]);
   if (cookie["bid"]) {
      bid = (ccp)cookie["bid"];
   }
   auto now = chrono::system_clock::now();
   auto now_ms =
      chrono::time_point_cast<chrono::milliseconds>(now);
   long lepoch = now_ms.time_since_epoch().count();
   if (!ffHttp["referer"]) goto nextproto;
   referer = ffHttp["referer"];
   username = strstr(referer,":");
   protolen = username - referer;
   if (username==nullptr || protolen<0 || protolen>=8) {
      ffl_debug(FL, "badproto");
      return mkHttpRes(ffHttp, "badproto");
   }
   sprintf(proto,"%.*s",protolen,(ccp)ffHttp["referer"]);
  nextproto:
   username=nullptr;
   ffl_debug(FL, "proto: %s, host: %s", proto, (ccp)ffHttp["host"]);
   path = ffHttp["path"];
   if (path[1]=='.' || strstr(path,"/red")) {
      return mkHttpRes(ffHttp, "NaNa!", txtMime, -1, 404);
   }
   if (!strcmp(path,"/sleep")) {
      int sd = atoi((ccp)ffHttp["query"]["time"]);
      sleep(sd);
      sprintf(mesg, "slept for %d", sd);
      return mkHttpRes(ffHttp, mesg);
   } else if (!strcmp(path, "/activate")) {
      username=ffHttp["query"]["user"];
      user=&users[username];
      if ((!user["password"] || !user["inactive"]) &&
          !user["newpassword"]) {
         return mkHttpRes(ffHttp, "{\"error\":\"wrongKey\"}", jsonMime, -1, 400);
      } else if (!strcmp(user["activationKey"],ffHttp["query"]["key"])) {
         if (user["newpassword"]) {
            user["password"]=user["newpassword"];
            user["newpassword"]=false;
         }
         user["name"]=username;
         user["inactive"]=false;
         user["things"].init("[]");
         user["smsgs"].init("[]");
         user["reps"].init("[]");
         setSavMtx.lock();
         pFSetToSave.insert(user.val.fptr);
         pFSetToSave.insert(&users);
         setSavMtx.unlock();
         sprintf(mesg, "%s activated", username);
         return mkHttpRes(ffHttp, mesg);
      } else {
         return mkHttpRes(ffHttp, "{\"error\":\"wrongKey\"}", jsonMime, -1, 400);
      }
   } else if (!strcmp(path, "/logout")) {
     logout:
      rbsid["user"]=nullFFJSON;
      setSavMtx.lock();
      pFSetToSave.insert(&rbs);
      setSavMtx.unlock();
      return mkHttpRes(ffHttp, "{\"logout\":true}", jsonMime);
   }
   
   cpld = (ccp)ffHttp["payload"];
   if (!cpld) {
      goto bidcheck2;
   }
      
   if (strstr(path, "/cookie")==path) {
      //cookie
      ffl_notice(FL, "cookie");
      if (bid.length())
         if(rbs[bid])
            goto gotbid;
     newbid:
      bid = random_alphnuma_string();
     bidcheck:
      if (rbs[bid]) {
         bid=random_alphnuma_string();
         goto bidcheck;
      }
      rbs[bid]["ip"]=(ccp)ffHttp["ip"];
     gotbid:
      if (strcmp(rbs[bid]["ip"],ffHttp["ip"])) {
         goto newbid;
      }
      rbsid = &rbs[bid];
      rbsid["ts"]=now;
      reply["bid"]=bid;
      BidThings_& bts = bidThings[rbsid.val.fptr];
      set<FFJSON*>& mdts = bts.mdts;
      Pts& pts = bts.all;
      if (!cpld)
         return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
      payload.init(cpld);
      to = payload["path"];
      if (to) {
         FFJSON& pldusr = users[to];
         reply["things"].init("[]");
         if (!pldusr) {
            reply["error"]="noUser";
            goto cookieReply;
         }
         if (pldusr != &user) {
            addSmtgsToReply(users, pldusr, reply, mdts, false);
         }
         goto cookieReply;
      }
      pts=Pts();
      if (ffHttp["query"]["user"] && ffHttp["query"]["thing"]) {
         FFJSON& uthings = users[(ccp)ffHttp["query"]["user"]]["things"];
         int tind = getIdChildInd(uthings, atoi(ffHttp["query"]["thing"]));
         FFJSON* thn = &uthings[tind];
         reply["things"][0]=thn;
         mdts.clear();
         mdts.insert(thn);
         FFJSON q("{things:!}");
         user.answerObject(&q, nullptr, FerryTimeStamp(), &reply);
         goto cookieReply;
      }
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      thnsTree.getPointsFromQuad(pts);
      mdts.clear();
      for (uint i = 0; i<pts.pni; ++i) {
         NdNPrn& nd = pts.pts[i];
         FFJSON* f;
         if (nd.prn==(QuadNode*)-1) {
            f = (FFJSON*)nd.qh;
         } else {
            auto aa = getNode(nd);
            f = (FFJSON*)get<0>(aa);
         }
         reply["things"][i]=f;
         mdts.insert(f);
      }
      username = rbsid["user"];
      if (username && (user = &users[username]) &&
          !strcmp((ccp)user["bid"],bid.c_str())) {
         rbsid["urts"]=lepoch;
         addSmtgsToReply(users, user, reply, mdts);
      }
     cookieReply:
      setSavMtx.lock();
      pFSetToSave.insert(&rbs);
      setSavMtx.unlock();
      return mkHttpRes(ffHttp, reply.stringify(true).c_str(),jsonMime);
   }
  bidcheck2:
   if (!bid.length() || !rbs[bid]) {
      return "";
   }
   rbsid = &rbs[bid];
   if (!strcmp(path, "/captcha")) {
      ffl_notice(FL, "captcha");
      string tempPath(wdir+"/tmp/"+bid+".jpg");
      string randstr = random_alphnuma_string(7);
      cap randcap(randstr, tempPath, 7, 288, 68, 40, 80, 48);
      rbsid["captcha"]=randstr;
      randcap.save();
      rbs.clearEFlag(FFJSON::FILE);
      setSavMtx.lock();
      pFSetToSave.insert(&rbs);
      setSavMtx.unlock();
      return mkHttpRes(ffHttp, "{\"cap\":\"true\"}", jsonMime);
   }
   if (!cpld) {
      if (!strcmp(path, "/upload")) {
         goto upload;
      } else if (strstr(path, "/tmp/") == path) {
         return "1";
      }
      return "";
   }
   payload.init(cpld);
   if (!strcmp(path, "/login")) {
      ffl_notice(FL, "Login");
      username=payload["username"];password=payload["password"];
      ffl_notice(FL, "\nUser: %s\nPass: %s", username, password);
      if (!users[username]) {
         return mkHttpRes(ffHttp, "{\"login\":\"false\"}", jsonMime);
      }
      user=&users[username];
      cout << "password:" << (ccp)user["password"] << endl;
      if (user["password"] && !user["inactive"] &&
          !strcmp(password,user["password"])
      ) {
         rbsid["user"]=user["name"];
         rbsid["ip"]=(ccp)ffHttp["ip"];
         user["bid"]=bid;
         rbsid["urts"]=lepoch;
         addSmtgsToReply(users, user, reply, bidThings[rbsid.val.fptr].mdts);
         setSavMtx.lock();
         pFSetToSave.insert(&rbs);
         setSavMtx.unlock();
         return mkHttpRes(
            ffHttp, reply.stringify(true).c_str(), jsonMime);
      } else {
         return mkHttpRes(ffHttp, "\{\"login\":\"false\"}", jsonMime);
      }
   } else if (!strcmp(path, "/pts")) {
      ffl_notice(FL, "pts:");
      BidThings_& bts = bidThings[&rbs[bid]];
      bool isSearch = payload["search"];
      Pts& pts = isSearch?bts.search:bts.all;
      int dir = payload["dir"];
      if (dir!=1 && dir!=-1) {
         return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
      }
      int ni = payload["ni"];
      int pni = pts.pni;
      ni = ni==-1?pni:ni;
      reply["things"].init("[]");
      if (pni<pts.minPts || pts.pts.size()<=pts.minPts) {
         return mkHttpRes(ffHttp, reply.stringify(1), jsonMime);
      }
      int tpts = ni+dir*20;
      tpts = tpts<0?0:tpts;
      if (pni>=tpts || tpts>=512)
         return mkHttpRes(ffHttp, reply.stringify(1), jsonMime);
      pts.minPts=tpts;
      NdNPrn& nd = pts.cnd;
      QuadNode* tQN=nd.qh->qn();
      uint8_t tind=nd.qh-(QuadHldr*)tQN;
      pts.cnd.ds=1;
      cvSrch.wait(modLk, []{return modQhCv.load()==0;});
      ++searchCv;
      nd.qh->findNeighbours(
         pts, tQN, tind, nd.prn, nd.ind, nd.dx);
      --searchCv;
      cvMod.notify_all();
      set<FFJSON*>& mdts = bts.mdts;
      addSearchNoDups(pts, reply, mdts, false);
      return mkHttpRes(ffHttp, reply.stringify(1), jsonMime);
   } else if (!strcmp(path, "/signup")) {
      //signup
      bool recovery=false;
      ffl_notice(FL, "Signup");
      if (!isValidEmail(payload["email"])) {
         ffl_warn(FL, "invalid email.");
         return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
      }
      if (payload["username"]) {
         username=payload["username"];
         ffl_debug(FL, "User: %s\nPass: %s\nEmail: %s",
                   username, password, (ccp)payload["email"]);
      } else if (payload["email"]) {
         recovery=true;
         std::map<string,FFJSON*>* emln = users.val.pairs;
         if (emln->find(string((ccp)payload["email"]))!=emln->end()) {
            FFJSON* ffemln = (*emln)[string((ccp)payload["email"])];
            FFJSON::Link* link =
               ffemln->getFeaturedMember(FFJSON::FM_LINK).link;
            username=(*link)[0].c_str();
            ffl_debug(FL, "username: %s", username);
         } else {
            ffl_warn(FL, "%s Email not registered.",
                     (ccp)payload["email"]);
            return mkHttpRes(ffHttp, 
               "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
               jsonMime);
         }
      }
      password=payload["password"];
      user=&users[username];
      if (!recovery && user &&
          (user["activationKey"] && !user["inactive"])) {
         ffl_warn(FL, "User already exists.");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-1,\"msg\":\"Username already taken, choose an"
            " another :|\"}", jsonMime);
      } else if (!recovery && user["inactive"] &&
                 strcmp(payload["email"],user["email"])) {
         ffl_warn(FL, "User exists; mail mismatch");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
            jsonMime);
      } else if (
         !recovery && users[(ccp)payload["email"]] && !user["email"]
      ) {
         ffl_warn(FL, "Email already registered.");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-3,\"msg\":\"Email already registered! Try"
            " resetting password\"}", jsonMime);
      } else if (!recovery && !(validUsername(string(username)))) {
         ffl_warn(FL, "Invalid password");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
            jsonMime);
      } else if (
         !(password!=nullptr && validMD5(string(password)))
      ) {
         ffl_warn(FL, "Invalid password");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
            jsonMime);
      } else if (
         !payload["captcha"] || !rbsid["captcha"] ||
         strcmp((ccp)payload["captcha"],(ccp)rbsid["captcha"])!=0
      ) {
         ffl_warn(FL, "Captcha mismatch.");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-4,\"msg\":\"Captcha mismatch, hmm!\"}",
            jsonMime);
      } else if (!payload["consent"]) {
         ffl_warn(FL, "Captcha mismatch.");
         return mkHttpRes(ffHttp, 
            "{\"actEmailSent\":-5,\"msg\":\"U didn't consent to this tool"
            " usage :/\"}", jsonMime);
      }
      if (!recovery) {
         user["email"] = payload["email"];
         user["password"] = password;
         users[(ccp)user["email"]].addLink(users,username);
         user["inactive"]=true;
         filesystem::path
            usrpth(wdir+string("/upload/")+username);
         filesystem::create_directory(usrpth);
      } else {
         user["newpassword"] = password;
      }
      string actKey = random_alphnuma_string();
      user["activationKey"]=actKey;
      ffl_notice(FL, "actKey: %s",actKey.c_str());
      to=user["email"];
      sprintf(subj, "User activation link");
      sprintf(mesg, "Open %s://%s/activate?user=%s&key=%s to activate "
              "%s", proto, (ccp)ffHttp["host"]["fqdn"], username,
              (ccp)user["activationKey"], username);
      // TODO
      // mg_connect(&mail_mgr, mail_server, mailfn, NULL);
      // while(!s_quit)
      //    mg_mgr_poll(&mail_mgr, 100);
      string sfrom(admin);
      sfrom+="@";
      sfrom+=from;
      sfrom+=".com";
      if (sendMail(mailServer, mailPort, "plain", admin, adminPass, sfrom, to,
                   subj, mesg)!=0) {
         return mkHttpRes(
            ffHttp, "{\"error\":\"sendMailFailed\"}", jsonMime);
      };
      s_quit=false;
      FFJSON::FeaturedMember fm;
      fm.m_sFileName = new char[11+strlen(username)];
      sprintf(fm.m_sFileName, "users/%s.txo", username);
      if (!recovery) {
         user.setEFlag(FFJSON::CASTFILE);
         user.setEFlag(FFJSON::FILE);
         user.insertFeaturedMember(fm, FFJSON::FM_FILE);
         setSavMtx.lock();
         pFSetToSave.insert(&users);
         setSavMtx.unlock();
      }
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      pFSetToSave.insert(&rbs);
      setSavMtx.unlock();
      return mkHttpRes(ffHttp, 
         "{\"actEmailSent\":2,\"msg\":\"Activation mail sent to ur email"
         " :D\"}", jsonMime);
   } else if (strstr(path, "/search")) {
      ccp srchStr = payload["search"];
      rbsid = &rbs[bid];
      BidThings_& bts = bidThings[rbsid.val.fptr];
      set<FFJSON*>& mdts = bts.mdts;
      Pts& pts = bts.search;
      pts=Pts();
      vector<string> mstr = metaname(srchStr);
      pts.ina = nametouint(mstr);
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      ffl_info(FL, "searching %s at %s\n",srchStr,
               payload["geoposition"].stringify().c_str());
      cvSrch.wait(modLk, []{return modQhCv.load()==0;});
      ++searchCv;
      thnsTree.getPointsFromQuad(pts);
      --searchCv;
      cvMod.notify_all();
      to = payload["path"];
      addSearchNoDups(pts, reply, mdts);
      reply["things"][0];
      return mkHttpRes(ffHttp, reply.stringify(true).c_str(), jsonMime);
   }

  upload:
   if (!rbsid["user"]) {
      return "";
   }
   username = rbsid["user"];
   user = &users[username];
   if (strcmp((ccp)user["bid"],bid.c_str())) {
      goto logout;
   }

   if (!strcmp(path, "/upload")) {
      int maxThings = (bool)user["maxThings"]?
         user["maxThings"]:ffcfg["maxThings"];
      int maxThingPics = (bool)user["maxThingsPics"]?
         user["maxThingsPics"]:ffcfg["maxThingPics"];
      int thingId = atoi((ccp)ffHttp["query"]["thingId"]);
      int picId = atoi((ccp)ffHttp["query"]["picId"]);
      int fofst = atoi((ccp)ffHttp["query"]["offset"]);
      int chnkSz= atoi((ccp)ffHttp["query"]["chunkSize"]);
      int ttlSz = atoi((ccp)ffHttp["query"]["totalSize"]);
      int thngi = -1;
      // if (fofst!=0) {
      //    FFJSON& ptgs=user["pendingThings"];
      //    if(thingId!=(int)ptgs["thingId"]){
      //       mg_http_reply(c, -1, 400, headers, "{%Q:%Q}", "error",
      //                     "noSuchThingId" );
      //       goto done;                  
      //    };
      //    picId=ptgs["picId"];
      //    thngi=ptgs["thngi"];
      //    goto gotThingId;
      // }
      FFJSON& uthings = user["things"];
      if (thingId < 0) {
         if (uthings && uthings.size>=maxThings) {
            ffl_notice (
               HL,
               "user[\"things\"].size: %d", uthings.size
            );
            return mkHttpRes(ffHttp, "{\"error\":\"thingsAreAtMax\"}", jsonMime, -1, 400);
         }
         if (uthings && uthings.size) {
            thngi=uthings.size;
            thingId=(int)uthings[thngi-1]["id"]+1;
         } else {
            thngi=0;
            thingId=1;
         }
      } else {
         thngi=getIdChildInd(uthings, thingId);
         // int tSize = user["things"].size-1;
         // for (int i=(thingId<tSize?thingId:tSize); i>=0; --i) {
         //    if ((int)user["things"][i]["id"]==thingId) {
         //       thngi=i;
         //       break;
         //    }
         // }
         if (thngi<0) {
            return mkHttpRes(ffHttp, "{\"error\":\"noSuchThingId\"}", jsonMime, -1, 400);
         }
      }
     gotThingId:
      ffl_notice (
         HL,
         "picId: %d, maxThingPics: %d, thingId: %d, thngi: %d",
         picId, maxThingPics, thingId, thngi
      );
      if (picId >= maxThingPics) {
         return mkHttpRes(ffHttp, "{\"error\":\"picsAreAtMax\"}", jsonMime, -1, 400);
      }
      string upldpth(wdir);
      upldpth += "/upload/";
      upldpth += username;
      upldpth += "/";
      upldpth += to_string(thingId);
      upldpth += ".";
      upldpth += to_string(picId);
      upldpth +=".jpg";
      ffl_notice(FL, "receiving: %s", upldpth.c_str());
      ios_base::openmode ofmode;
      if (fofst==0) {
         uthings[thngi]["id"]=thingId;
         if (!uthings[thngi]["user"]) {
            uthings[thngi]["user"].addLink(users, username);
         }
         FFJSON& ups = uthings[thngi]["pics"];
         ups[picId]["partial"] = true;
         // if (fofst+chnkSz<ttlSz) {
         //    FFJSON& ptgs=user["pendingThings"];
         //    ptgs["thingId"]=thingId;
         //    ptgs["picId"]=picId;
         //    ptgs["thngi"]=thngi;
         // }
         ofmode = std::ios::trunc;
      } else {
         ofmode = std::ios::app;
      }
      char msg[30];
      ofstream upfile(upldpth.c_str(), ofmode | std::ios::binary);
      if (!upfile.is_open()) {
         sprintf(msg, "{\"error\":\"createFailed\"}");
         return mkHttpRes(ffHttp, msg, jsonMime, -1, 400);
         
      }
      int initial_size = (int)(long long)upfile.tellp();
      if (initial_size!=fofst) {
         sprintf(msg, "{\"lastChunk\":%d}", initial_size);
         return mkHttpRes(ffHttp, msg, jsonMime, -1, 400);
      }
      int wrByteCount = ffHttp["content-length"];
      upfile.write(cpld, wrByteCount);
      // mg_http_upload(
      //    c, hm, &mg_fs_posix, upldpth.c_str(), 2999999, msg);
      if (fofst+chnkSz >= ttlSz) {
//            user.erase("pendingThings");
         uthings[thngi]["pics"][picId].erase("partial");
         uthings[thngi]["pics"][picId]["ts"]=lepoch;
//            printf("pendingThings\n");
         setSavMtx.lock();
         pFSetToSave.insert(user.val.fptr);
         setSavMtx.unlock();
      }
      sprintf(msg, "{\"thingId\":%d,\"picId\":%d}", thingId, picId);
      return mkHttpRes(ffHttp, msg, jsonMime);
   } else if (!strcmp(path, "/updateThing")) {
      FFJSON& user = users[username];
      if (strcmp((ccp)user["bid"],bid.c_str())) {
         return mkHttpRes(ffHttp, "{\"error\":\"bidmismatch\"}", jsonMime, -1, 400);
      }
      payload.init(cpld);
      if (payload["things"]) {
         FFJSON& cthings = payload["things"];
         FFJSON& uthings = user["things"];
         bool newthing=false;
         int id = 0;
         if (!(bool)uthings) {
            uthings.init("[]");
         }
         int j=0;
         for (int i=0; i<cthings.size; ++i) {
            if (!cthings[i])
               continue;
            if (i>uthings.size) {
               return mkHttpRes(ffHttp, "{\"error\":\"sizeExceeded\"}", jsonMime, -1, 400);
            }
            FFJSON& cfname = cthings[i]["name"];
            string cname((ccp)cfname);
            if (!isValidThingName(cfname)) {
               return mkHttpRes(ffHttp, "{\"error\":\"invalidThingName\"}",
                                jsonMime, -1, 400);
            }
            j=getIdChildInd(uthings, (int)cthings[i]["id"]);
            bool locChanged = false;
            bool nameChanged = false;
            cthings[i].erase("user");
            vector<string> mstr;
            vector<uint> ina;
            if (j<0) {
               j=uthings.size;
               uthings[j]["id"] = j?(int)uthings[j-1]["id"]+1:1;
               FFJSON& ln = uthings[j]["user"].addLink(users, username);
               if (!ln)
                  delete &ln;
               uthings[j]["name"]=cthings[i]["name"];
               nameChanged=true;
               FFJSON& cloc = cthings[i]["location"];
               if (!isValidLocation(cloc)) {
                  return mkHttpRes(ffHttp, 
                     "{\"error\":\"invalidLocation\"}", jsonMime, -1, 400);
               } else {
                  uthings[j]["location"]=cloc;
                  locChanged=true;
               }
            } else {
               string uname(uthings[j]["name"]?(ccp)uthings[j]["name"]:"");
               tolower(cname);
               tolower(uname);
               if (strcmp(cname.c_str(),uname.c_str())) {
                  mstr = metaname(uname);
                  for (int k=0; k<mstr.size(); ++k) {
                     map<string, FFJSON*>::iterator it =
                        nameints->find(mstr[k]);
                     if (it->second->val.number==1) {
                        mitpos.erase(mitpos.find(&it->first));
                        fnameints->erase(mstr[k]);
                     } else {
                        --(*nameints)[mstr[k]]->val.number;
                     }
                  }
                  nameChanged=true;
                  uthings[j]["name"]=cthings[i]["name"];
               }
               FFJSON& cloc = cthings[i]["location"];
               FFJSON& uloc = uthings[j]["location"];
               if (!isValidLocation(cloc)) {
                  return mkHttpRes(ffHttp, 
                     "{\"error\":\"invalidLocation\"}", jsonMime, -1, 400);
               }
               if (((double)cloc[0]!=(double)uloc[0] ||
                    (double)cloc[1]!=(double)uloc[1])) {
                  mstr = metaname(uname);
                  uloc=cloc;
                  locChanged=true;
               }
               if (nameChanged||locChanged) {
                  ina = nametouint(mstr);
                  cvMod.wait(modLk, [] {return searchCv.load()==0;});
                  ++modQhCv;
                  thnsTree.insert(uthings[j], ina, true);
                  --modQhCv;
                  cvSrch.notify_all();
               }
            }
            if (cthings[i]["details"]) {
               if (isValidThingDetails(cthings[i]["details"])) {
                  uthings[j]["details"]=cthings[i]["details"];
               } else {
                  return mkHttpRes(ffHttp, 
                     "{\"error\":\"invalidThingDetails\"}", jsonMime, -1, 400);
               }
            }
            if (nameChanged) {
               mstr=metaname(cname);
               for (int k=0; k<mstr.size(); ++k) {
                  map<string, FFJSON*>::iterator it =
                     nameints->find(mstr[k]);
                  if (it==nameints->end()) {
                     (*fnameints)[mstr[k]]=1;
                     it=nameints->find(mstr[k]);
                     mitpos[&it->first]=nameints->size()-1;
                  } else {
                     ++(*nameints)[mstr[k]]->val.number;
                  }
               }
               ina=nametouint(mstr);
            }
            if (locChanged||nameChanged) {
               thnsTree.insert(uthings[j], ina);
            }
            reply["things"][reply["things"].size]=&uthings[j];
         }
      }
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      pFSetToSave.insert(fnameints);
      setSavMtx.unlock();
      return mkHttpRes(ffHttp, reply.stringify(true).c_str(), jsonMime);
   } else if (strstr(path, "/owl")) {
      FFJSON& things = user["things"];
      FFJSON& smsgs = user["smsgs"];
      FFJSON& reps = user["reps"];
      int smind=smsgs.size;
      payload.init(cpld);
      FFJSON& fQs = payload["Qs"];
      FFJSON::Iterator it;
      long urts;
      long lmts;
      int i,j;
      if (!fQs) {
         goto rqs;
      }
      it = fQs.begin();
      while (it!=fQs.end()) {
         ccp tuser = (ccp)it;
         if (!strcmp(tuser,username)) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         FFJSON::Iterator tit;
         if (tuser) {
            tit  = users.find(tuser);
         }
         if (!tuser || tit==users.end()) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         FFJSON& tfuser = users[tuser];
         FFJSON& tfthings = tfuser["things"];
         tit = it->begin();
         while (tit!=it->end()) {
            ccp ctid = (ccp)tit;
            if (!ctid) {
               return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
            }
            int tid = atoi(ctid);
            int tind = getIdChildInd(tfthings, tid);
            if (tind<0) {
               return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
            }
            FFJSON& rmsgs = tfthings[tind]["rmsgs"];
            if (!rmsgs) {
               rmsgs.init("[]");
            }
            int rmind=rmsgs.size;
            smind=smsgs.size;
            int rmid=1;
            if (rmind) {
               rmid = (int)rmsgs[rmind-1]["id"]+1;
            }
            rmsgs[rmind]["id"]=rmid;
            rmsgs[rmind]["user"]=username;
            rmsgs[rmind]["msg"]=*tit;
            rmsgs[rmind]["ts"]=lepoch;
            rmsgs[rmind]["new"]=true;
            rmsgs[rmind]["smind"]=smind;
            smsgs[smind].init("[]");
            smsgs[smind][0]=tuser;
            smsgs[smind][1]=tid;
            smsgs[smind][2]=rmid;
            *tit=rmid;
            ++tit;
         }
         tfuser["lmts"]=lepoch;
         setSavMtx.lock();
         pFSetToSave.insert(&tfuser);
         setSavMtx.unlock();
         ++it;
      }
      payload["status"]=1;
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      setSavMtx.unlock();
     rqs://mark query as read
      FFJSON& fRs = payload["Rs"];
      if (!fRs) {
         goto rrs;
      }
      it = fRs.begin();
      while (it!=fRs.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = (int)*tit;
            mid = getIdChildInd(rmsgs, mid);
            if (mid<0) {
               return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
            }
            rmsgs[mid].erase("new");
            ++tit;
         }
         ++it;
      }
      payload["status"]=1;
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      setSavMtx.unlock();
     rrs://mark received replies as read
      FFJSON& frrs = payload["rrs"];
      if (!frrs) {
         goto news;
      }
      for (int i=0; i<frrs.size; ++i) {
         int smind = frrs[i];
         for (int j=0; j<reps.size; j+=2) {
            if ((int)reps[j]==smind) {
               reps.erase(j,j+2);
               break;
            }
         }
      }
      payload["status"]=1;
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      setSavMtx.unlock();
     news://fetch if there are new queries
      urts = (long)rbsid["urts"];
      if (!user["lmts"]) {
         goto rnews;
      }
      lmts = (long)user["lmts"];
      if (urts>lmts) {
         goto rnews;
      }
      for (int i=0; i<things.size; ++i) {
         FFJSON& rmsgs = things[i]["rmsgs"];
         for (int j=0;j<rmsgs.size;++j) {
            FFJSON& msg = rmsgs[j];
            long mts = (long)msg["ts"];
            if (mts<urts) {
               continue;
            }
            payload["news"][to_string((int)things[i]["id"])]
               [to_string(j)]=msg;
         }
      }
      payload["status"]=1;
     rnews://fetch if there are new replies
      if (!reps.size) {
         goto reps;
      }
      i=reps.size-1;
      lmts = (long)reps[i];
      if (urts>lmts) {
         goto reps;
      }
      j=0;
      do {
         --i;
         int smind = reps[i];
         FFJSON& smsg = smsgs[smind];
         FFJSON& tusrts = users[(ccp)smsg[0]]["things"];
         int tind = getIdChildInd(tusrts, (int)smsg[1]);
         FFJSON& trmsgs = tusrts[tind]["rmsgs"];
         int mind = getIdChildInd(trmsgs, (int)smsg[2]);
         FFJSON& rep=payload["rnews"][j];
         rep=smsg;
         rep[3]=trmsgs[mind]["rep"];
         rep[4]=smind;
         --i;++j;
         if (i<0) {
            break;
         }
         lmts=(long)reps[i];
      } while (urts<lmts);
     reps://post reply to target user thing
      FFJSON& fRps = payload["Reps"];
      if (!fRps) {
         goto owldone;
      }
      it = fRps.begin();
      while (it!=fRps.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = stoi((ccp)tit);
            int mind = getIdChildInd(rmsgs, mid);
            if (mind<0) {
               return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
            }
            smind=smsgs.size;
            rmsgs[mind]["rep"]=*tit;
            smsgs[smind].init("[]");
            smsgs[smind][0]="";
            smsgs[smind][1]=tid;
            smsgs[smind][2]=mid;
            FFJSON& tusr = users[(ccp)rmsgs[mind]["user"]];
            FFJSON& treps = tusr["reps"];
            if (!treps) {
               treps.init("[]");
            }
            treps[treps.size]=rmsgs[mind]["smind"];
            treps[treps.size]=lepoch;
            setSavMtx.lock();
            pFSetToSave.insert(&tusr);
            setSavMtx.unlock();
            ++tit;
         }
         ++it;
      }
      setSavMtx.lock();
      pFSetToSave.insert(user.val.fptr);
      setSavMtx.unlock();
      payload["status"]=1;
     owldone:
      payload["status"]=1;
      rbsid["urts"]=lepoch;
      return mkHttpRes(ffHttp, payload.stringify(true).c_str(), jsonMime);
   }
   if (valgrind_test && !--valgrind_count)
      g_running=false;
   return "";
}

void makeThngsTree () {
   QuadNode q;
   fnameints =
      &cfg["vhosts"]["www"]["cfg"]["nameints"];
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
   FFJSON& users = cfg["vhosts"]["www"]["cfg"]["users"];
   FFJSON::Iterator it = users.begin();
   FFJSON::Iterator tit;
   int ic=0;
   while (it!= users.end()) {
      if (it->isType(FFJSON::LINK)) {
         ++it;
         continue;
      }
      string user = it.getIndex();
      FFJSON& uthings = (*it)["things"];
      tit = uthings.begin();
      while (tit!=uthings.end()) {
         if (!((*tit)["name"].isType(FFJSON::UNDEFINED) ||
               (*tit)["location"].isType(FFJSON::UNDEFINED))) {
            FFJSON* pF = &*tit;
            ffl_debug(FL, "inserting %d", ic);
            tpoolPtr->enqueue([pF, ic] {
               FFJSON& rF = *pF;
               vector<string> mstr = metaname((ccp)rF["name"]);
               vector<uint> ina = nametouint(mstr);
               float lx = rF["location"][1];
               float ly = rF["location"][0];
               thnsTree.insert(rF, ina, 0, lx, ly);
               ffl_debug(FL, "inserted %d", ic);
            });
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
   tpoolPtr->join();
}

void initFerryFair (FFJSON& cfg) {
   FFJSON& ffcfg = cfg["cfg"];
   wdir=(ccp)cfg["vhosts"]["www"]["rootdir"];
   ffcfg.init(string("file://")+wdir+"/config.ffjson|OBJECT");
   cfg["vhosts"]["www"]["cfg"]=pffcfg=&cfg["cfg"];
   admin = ffcfg["secret"]["admin"];
   adminPass = ffcfg["secret"]["adminPass"];
   prbs = &ffcfg["rbs"];
   pusers = &ffcfg["users"];
   mailServer = ffcfg["secret"]["mailServer"];
   mailPort = ffcfg["secret"]["mailPort"];
   makeThngsTree();
   Pts pts;
   vector<string> mstr = metaname("Touch");
   //vector<string> mstr = metaname("Indulehka Bringha Hair Oil");
   pts.ina=nametouint(mstr);
   //Circle c = {180.0, 90.0, 10.5};
   //Circle c = {0.1, 0.1, 10.5};
   //Circle c = {0.9, 0.8, 10.5};
   //pts.c = {77.7584640, 12.9826816, 10.5};
   //pts.c = {77.7645299,12.9941367, 10.5};
   //pts.c = {77.7644272, 12.9940713, 10.5};
   pts.c = {77.7644577, 12.9941273, 10.5};
   ffl_debug(FL, "c: %f,%f\n", pts.c.x, pts.c.y);
   FerryTimeStamp ftsStart;
   FerryTimeStamp ftsEnd;
   FerryTimeStamp ftsDiff;
   ftsStart.update();
   thnsTree.print(pts.c);
   //ina.push_back(0x80);
   thnsTree.getPointsFromQuad(pts);
   ftsEnd.update();
   ftsDiff = ftsEnd - ftsStart;
   cout << "%TEST_FINISHED% time=" << ftsDiff << " test21\n" << endl;
   std::vector<NdNPrn>::iterator it = pts.pts.begin();
   it = pts.pts.begin();
   auto itend = it+pts.pni;
   while (it!=itend) {
      FFJSON* fp;
      if (it->prn==(QuadNode*)-1) {
         fp = (FFJSON*)it->qh;
      } else {
         fp = (FFJSON*)get<0>(getNode(*it));
      }
      printf("%s\n",(*fp)["location"].stringify().c_str());
      ++it;
   }
}
