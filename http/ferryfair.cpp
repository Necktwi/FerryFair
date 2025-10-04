#include <atomic>
#include <string>
#include <chrono>
#include <filesystem>
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

using namespace std;

static atomic<bool> saveUsers{true};
static atomic<bool> saveRBS{true};
bool valgrind_test = false;
int valgrind_count = 1;

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

map<FFJSON*,set<FFJSON*>> bidThings; 

int addSmtgsToReply (FFJSON& users, FFJSON& user, FFJSON& r,
                     set<FFJSON*>& mdts) {
   FFJSON q("{things:!}");
   user.answerObject(&q, nullptr, FerryTimeStamp(), &r);
   FFJSON& rts = r["things"];
   FFJSON& uts = user["things"];
   int k=rts.size;
   int ik=k;
   for (uint i = 0; i<uts.size; ++i) {
      FFJSON* f = &uts[i];
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
const char* mail_server;
const char* admin;
const char* admin_pass;
const char* to = nullptr;
const char* from = "FerryFair";
char subj[64];
char mesg[128];

bool s_quit = false;
bool sendMail = false;

string ferryfair (FFJSON& ffHttp, FFJSON& vhost) {
   FFJSON reply, user, rbsid;
   ccp referer=nullptr;char proto[8]="https"; int protolen;
   ccp username = nullptr, password = nullptr, cpld = nullptr;
   ccp jsonMime = "text/json";
   ccp path;
   string bid;
   string vhdir((ccp)vhost["rootdir"]);
   static FFJSON& ffcfg = vhost["cfg"];
   static FFJSON& rbs = ffcfg["rbs"];
   static FFJSON& users = ffcfg["users"];
   FFJSON& cookie = ffHttp["cookie"];
   FFJSON& payload = ffHttp["payload"];
   ffl_notice(HL, "cookie[bid]: %s",(ccp)cookie["bid"]);
   if (cookie["bid"]) {
      bid = (ccp)cookie["bid"];
   }
   auto now = chrono::system_clock::now();
   auto now_ms =
      chrono::time_point_cast<chrono::milliseconds>(now);
   long lepoch = now_ms.time_since_epoch().count();
   if (vhost["redirect"]) {
      char rhed[64];
      sprintf(rhed, "Location: %s\r\n", (ccp)vhost["redirect"]);
      return mkHttpRes("", "text/plain", 308, "Permanent Redirect", rhed);
   }
   if (!ffHttp["referer"]) goto nextproto;
   referer = ffHttp["referer"];
   username = strstr(referer,":");
   protolen = username - referer;
   if (username==nullptr || protolen<0 || protolen>=8) {
      ffl_debug(HL, "badproto");
      return mkHttpRes("badproto");
   }
   sprintf(proto,"%.*s",protolen,(ccp)ffHttp["referer"]);
  nextproto:
   username=nullptr;
   ffl_debug(HL, "proto: %s",proto);
   path = ffHttp["path"];
   const char* pathStart;
   pathStart = strstr(path,"/sleep?");
   if (pathStart) {
      int sd = atoi(pathStart+7);
      sleep(sd);
      return mkHttpRes("slept for "+to_string(sd));
   } else if (strstr(path, "/activate?")) {
      username=ffHttp["query"]["user"];
      user=&users[username];
      if ((!user["password"] || !user["inactive"]) &&
          !user["newpassword"]) {
         return mkHttpRes("{\"error\":\"wrongKey\"}", jsonMime, 400);
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
         saveUsers=true;
         return mkHttpRes(string(username) + " activated.");
      } else {
         return mkHttpRes("{\"error\":\"wrongKey\"}", jsonMime, 400);
      }
   }
         
   cpld = (ccp)ffHttp["payload"];
   if (!cpld) {
      goto bidcheck2;
   }
      
   if (strstr(path, "/cookie")==path) {
      //cookie
      ffl_notice(HL, "cookie");
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
      set<FFJSON*>& mdts = bidThings[&rbsid];
      Pts pts;
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
      payload.init(cpld);
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      thnsTree.getPointsFromQuad(pts);
      mdts.clear();
      for (uint i = 0; i<pts.pts.size(); ++i) {
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
      saveRBS = true;
      return mkHttpRes(reply.stringify(true).c_str(),jsonMime);
   }
  bidcheck2:
   if (!bid.length() || !rbs[bid]) {
      goto fileserver;
   }
   rbsid = &rbs[bid];
   if (!cpld) {
      if (strstr(path, "/upload?chunkSize=")) {
         goto upload;
      }
      goto allfileserver;
   }
   if (!strcmp(path, "/captcha")) {
      ffl_notice(HL, "captcha");
      string tempPath(vhdir+"/tmp/"+bid+".jpg");
      string randstr = random_alphnuma_string(7);
      cap randcap(randstr, tempPath, 7, 288, 68, 40, 80, 48);
      rbsid["captcha"]=randstr;
      randcap.save();
      saveRBS=true;
      return mkHttpRes("{\"cap\":\"true\"}", jsonMime);
   } else if (!strcmp(path, "/login")) {
      ffl_notice(HL, "Login");
      payload.init(cpld);
      username=payload["username"];password=payload["password"];
      ffl_notice(HL, "\nUser: %s\nPass: %s", username, password);
      if (!users[username]) {
         return mkHttpRes("{\"login\":\"false\"}", jsonMime);
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
         addSmtgsToReply(users, user, reply, bidThings[&rbsid]);
         saveRBS = true;
         saveUsers = true;
         return mkHttpRes(reply.stringify(true).c_str(), jsonMime, 200);
      } else {
         return mkHttpRes("\{\"login\":\"false\"}", jsonMime);
      }
   } else if (!strcmp(path, "/signup")) {
      //signup
      payload.init(cpld);
      bool recovery=false;
      ffl_notice(HL, "Signup");
      if (!isValidEmail(payload["email"])) {
         ffl_warn(HL, "invalid email.");
         return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
      }
      if (payload["username"]) {
         username=payload["username"];
         ffl_debug(HL, "User: %s\nPass: %s\nEmail: %s",
                   username, password, (ccp)payload["email"]);
      } else if (payload["email"]) {
         recovery=true;
         std::map<string,FFJSON*>* emln = users.val.pairs;
         if (emln->find(string((ccp)payload["email"]))!=emln->end()) {
            FFJSON* ffemln = (*emln)[string((ccp)payload["email"])];
            FFJSON::Link* link =
               ffemln->getFeaturedMember(FFJSON::FM_LINK).link;
            username=(*link)[0].c_str();
            ffl_debug(HL, "username: %s", username);
         } else {
            ffl_warn(HL, "%s Email not registered.",
                     (ccp)payload["email"]);
            return mkHttpRes(
               "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
               jsonMime, 200);
         }
      }
      password=payload["password"];
      user=&users[username];
      if (!recovery && user &&
          (user["activationKey"] && !user["inactive"])) {
         ffl_warn(HL, "User already exists.");
         return mkHttpRes(
            "{\"actEmailSent\":-1,\"msg\":\"Username already taken, choose an"
            " another :|\"}", jsonMime, 200);
      } else if (!recovery && user["inactive"] &&
                 strcmp(payload["email"],user["email"])) {
         ffl_warn(HL, "User exists; mail mismatch");
         return mkHttpRes(
            "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
            jsonMime, 200);
      } else if (
         !recovery && users[(ccp)payload["email"]] && !user["email"]
      ) {
         ffl_warn(HL, "Email already registered.");
         return mkHttpRes(
            "{\"actEmailSent\":-3,\"msg\":\"Email already registered! Try"
            " resetting password\"}", jsonMime, 200);
      } else if (!recovery && !(validUsername(string(username)))) {
         ffl_warn(HL, "Invalid password");
         return mkHttpRes(
            "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
            jsonMime, 200);
      } else if (
         !(password!=nullptr && validMD5(string(password)))
      ) {
         ffl_warn(HL, "Invalid password");
         return mkHttpRes(
            "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
            jsonMime, 200);
      } else if (
         !payload["captcha"] || !rbsid["captcha"] ||
         strcmp((ccp)payload["captcha"],(ccp)rbsid["captcha"])!=0
      ) {
         ffl_warn(HL, "Captcha mismatch.");
         return mkHttpRes(
            "{\"actEmailSent\":-4,\"msg\":\"Captcha mismatch, hmm!\"}",
            jsonMime, 200);
      } else if (!payload["consent"]) {
         ffl_warn(HL, "Captcha mismatch.");
         return mkHttpRes(
            "{\"actEmailSent\":-5,\"msg\":\"U didn't consent to this tool"
            " usage :/\"}", jsonMime, 200);
      }
      if (!recovery) {
         user["email"] = payload["email"];
         user["password"] = password;
         users[(ccp)user["email"]].addLink(users,username);
         user["inactive"]=true;
         filesystem::path
            usrpth(vhdir+string("/upload/")+username);
         filesystem::create_directory(usrpth);
      } else {
         user["newpassword"] = password;
      }
      string actKey = random_alphnuma_string();
      user["activationKey"]=actKey;
      ffl_notice(HL, "actKey: %s",actKey.c_str());
      to=user["email"];
      sprintf(subj, "User activation link");
      sprintf(mesg, "Open %s://%s/activate?user=%s&key=%s to activate "
              "%s", proto, (ccp)ffHttp["host"], username,
              (ccp)user["activationKey"], username);
      mail_server = vhost["config"]["secret"]["mail_server"];
      admin = vhost["config"]["secret"]["admin"];
      admin_pass = vhost["config"]["secret"]["admin_pass"];
      // TODO
      // mg_connect(&mail_mgr, mail_server, mailfn, NULL);
      // while(!s_quit)
      //    mg_mgr_poll(&mail_mgr, 100);
      s_quit=false;
      saveRBS = true;
      saveUsers = true;
      return mkHttpRes(
         "{\"actEmailSent\":-6,\"msg\":\"Activation mail sent to ur email"
         " :D\"}", jsonMime, 200);
   } else if (strstr(path, "/search")) {
      payload.init(cpld);
      ccp srchStr = payload["search"];
      Pts pts;
      vector<string> mstr = metaname(srchStr);
      pts.ina = nametouint(mstr);
      int k=0;
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      ffl_info(HL, "searching %s at %s\n",srchStr,
               payload["geoposition"].stringify().c_str());
      CompThingNameMatch cTNM;
      multiset<tuple<FFJSON*, int8_t>, CompThingNameMatch> score(cTNM);
      thnsTree.getPointsFromQuad(pts);
      for (int i=0;i<pts.pts.size();++i) {
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
      rbsid = &rbs[bid];
      set<FFJSON*>& mdts = bidThings[&rbsid];
      while (it!=score.end()) {
         FFJSON& f = *get<0>(*it);
         bool thingIsWithUser = mdts.find(&f)!=mdts.end();
         if (thingIsWithUser) {
            FFJSON& rt = reply["things"][k];
            rt["id"]=f["id"];
            rt["user"]=&f["user"]["name"];
         } else {
            reply["things"][k]=&f;
         }
         ++k;++it;
      }
      reply["things"][0];
      return mkHttpRes(reply.stringify(true).c_str(), jsonMime, 200);
   }

  upload:
   if (!rbsid["user"]) {
      goto allfileserver;
   }
   username = rbsid["user"];
   user = &users[username];
   if (strcmp((ccp)user["bid"],bid.c_str())) {
      goto logout;
   }

   if (strstr(path, "/upload?")) {
      int maxThings = (bool)user["maxThings"]?
         user["maxThings"]:vhost["config"]["maxThings"];
      int maxThingPics = (bool)user["maxThingsPics"]?
         user["maxThingsPics"]:vhost["config"]["maxThingPics"];
      int thingId = atoi((ccp)ffHttp["query"]["thingId"]);
      int picId = atoi((ccp)ffHttp["query"]["picId"]);
      int fofst = atoi((ccp)ffHttp["query"]["offset"]);
      int chnkSz= atoi((ccp)ffHttp["query"]["chunkSize"]);
      int ttlSz = atoi((ccp)ffHttp["query"]["totalSize"]);
      int thngi = -1;
      // if (fofst!=0) {
      //    FFJSON& ptgs=user["pendingThings"];
      //    if(thingId!=(int)ptgs["thingId"]){
      //       mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
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
            return mkHttpRes("{\"error\":\"thingsAreAtMax\"}", jsonMime, 400);
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
            return mkHttpRes("{\"error\":\"noSuchThingId\"}", jsonMime, 400);
         }
      }
     gotThingId:
      ffl_notice (
         HL,
         "picId: %d, maxThingPics: %d, thingId: %d, thngi: %d",
         picId, maxThingPics, thingId, thngi
      );
      if (picId >= maxThingPics) {
         return mkHttpRes("{\"error\":\"picsAreAtMax\"}", jsonMime, 400);
      }
      string upldpth(vhdir);
      upldpth += "/upload/";
      upldpth += username;
      upldpth += "/";
      upldpth += to_string(thingId);
      upldpth += ".";
      upldpth += to_string(picId);
      upldpth +=".jpg";
      ffl_notice(HL, "receiving: %s", upldpth.c_str());
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
      }
      char msg[30];
      sprintf(msg, "{\"thingId\":%d,\"picId\":%d", thingId, picId);
      // mg_http_upload(
      //    c, hm, &mg_fs_posix, upldpth.c_str(), 2999999, msg);
      if (fofst+chnkSz >= ttlSz) {
//            user.erase("pendingThings");
         uthings[thngi]["pics"][picId].erase("partial");
         uthings[thngi]["pics"][picId]["ts"]=lepoch;
//            printf("pendingThings\n");
         users.save();
      }
   } else if (!strcmp(path, "/logout")) {
     logout:
      rbsid["user"]=nullFFJSON;
      saveRBS=true;
      return mkHttpRes("{\"logout\":true}", jsonMime);
   } else if (strstr(path, "/update")) {
      FFJSON& user = users[username];
      if (strcmp((ccp)user["bid"],bid.c_str())) {
         return mkHttpRes("{\"error\":\"bidmismatch\"}", jsonMime, 400);
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
               return mkHttpRes("{\"error\":\"sizeExceeded\"}", jsonMime, 400);
            }
            FFJSON& cfname = cthings[i]["name"];
            string cname((ccp)cfname);
            if (!isValidThingName(cfname)) {
               return mkHttpRes("{\"error\":\"invalidThingName\"}",
                                jsonMime, 400);
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
                  return mkHttpRes(
                     "{\"error\":\"invalidLocation\"}", jsonMime, 400);
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
                  return mkHttpRes(
                     "{\"error\":\"invalidLocation\"}", jsonMime, 400);
               }
               if (((double)cloc[0]!=(double)uloc[0] ||
                    (double)cloc[1]!=(double)uloc[1])) {
                  mstr = metaname(uname);
                  uloc=cloc;
                  locChanged=true;
               }
               if (nameChanged||locChanged) {
                  ina = nametouint(mstr);
                  thnsTree.insert(uthings[j], ina, true);
               }
            }
            if (cthings[i]["details"]) {
               if (isValidThingDetails(cthings[i]["details"])) {
                  uthings[j]["details"]=cthings[i]["details"];
               } else {
                  return mkHttpRes(
                     "{\"error\":\"invalidThingDetails\"}", jsonMime, 400);
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
      saveUsers=true;
      return mkHttpRes(reply.stringify(true).c_str(), jsonMime, 200);
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
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         FFJSON::Iterator tit;
         if (tuser) {
            tit  = users.find(tuser);
         }
         if (!tuser || tit==users.end()) {
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         FFJSON& tfuser = users[tuser];
         FFJSON& tfthings = tfuser["things"];
         tit = it->begin();
         while (tit!=it->end()) {
            ccp ctid = (ccp)tit;
            if (!ctid) {
               return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
            }
            int tid = atoi(ctid);
            int tind = getIdChildInd(tfthings, tid);
            if (tind<0) {
               return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
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
         ++it;
      }
      payload["status"]=1;
     rqs:
      FFJSON& fRs = payload["Rs"];
      if (!fRs) {
         goto rrs;
      }
      it = fRs.begin();
      while (it!=fRs.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = (int)*tit;
            mid = getIdChildInd(rmsgs, mid);
            if (mid<0) {
               return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
            }
            rmsgs[mid].erase("new");
            ++tit;
         }
         ++it;
      }
      payload["status"]=1;
     rrs:
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
     news:
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
     rnews:
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
     reps:
      FFJSON& fRps = payload["Reps"];
      if (!fRps) {
         goto owldone;
      }
      it = fRps.begin();
      while (it!=fRps.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = stoi((ccp)tit);
            int mind = getIdChildInd(rmsgs, mid);
            if (mind<0) {
               return mkHttpRes("{\"error\":\"yay\"}", jsonMime, 400);
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
            ++tit;
         }
         ++it;
      }
      payload["status"]=1;
     owldone:
      payload["status"]=1;
      rbsid["urts"]=lepoch;
      saveUsers=true;
      saveRBS=true;
      return mkHttpRes(payload.stringify(true).c_str(), jsonMime, 200);
   }
   goto done;
  fileserver:
   if (strstr(path, "/upload") ||
       strstr(path, "/tmp")) {
      goto done;
   }
  allfileserver:
   if (strstr(path, "/red")) {
      goto done;
   }
  done:
   if (valgrind_test && !--valgrind_count)
      g_running=false;
   return mkHttpRes("NaNa!");
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
            vector<string> mstr = metaname((ccp)(*tit)["name"]);
            vector<uint> ina = nametouint(mstr);
            float lx = (*tit)["location"][1];
            float ly = (*tit)["location"][0];
            uint level=thnsTree.insert((*tit), ina,0,lx,ly);
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

void initFerryFair (FFJSON& cfg) {
   cfg["vhosts"]["www"]["cfg"].init(
      string("file://")+(ccp)cfg["vhosts"]["www"]["rootdir"]+"/config.ffjson");
   makeThngsTree();
}
