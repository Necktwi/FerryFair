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
#include <curl/curl.h>
#include "https.h"
#include "ferryfair.h"
#include "spatialSearch.h"
#include "cap.h"
#include "smtpClient.h"
#include "htmlParser.h"

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
/**
 * inserts thing in to reply r if not in mdts and adds other user's things
 * on which user has commented
 */
int addSmtgsToReply (FFJSON& users, FFJSON& user, FFJSON& r,
							set<FFJSON*>& mdts, bool usr = true, bool bNUts= false) {
	if (usr) {
		FFJSON q("{things:!}");//add all keys of user except things to r;
		user.answerObject(&q, nullptr, FerryTimeStamp(), &r);
	}
	FFJSON& rts= r["things"];
	FFJSON& uts= user["things"];
	int k= rts.size;
	int ik= k;
	if (bNUts) goto utsend;
	for (uint i= 0; i< uts.size; ++i) {
		FFJSON* f= &uts[i];
		if (to) {
			if (strcmp((*f)["user"], to))
				continue;
		}
		set<FFJSON*>::iterator it= mdts.find(f);
		if (it==mdts.end()) {
			rts[k]= f;
			++k;
			mdts.insert(f);
		}
	}
  utsend:
	FFJSON::Iterator stit= user.find("smsgs");
	if (stit!=user.end()) {
		FFJSON& smsgs= *stit;
		FFJSON& rsmsgs= r["smsgs"];
		for (int i= 0; i< smsgs.size; ++i) {
			FFJSON& s= smsgs[i];
			if (!s[0].size)
				continue;
			FFJSON& uts= users[(ccp)s[0]]["things"];
			int tind= getIdChildInd(uts, (int)s[1]);
			FFJSON* f= &uts[tind];
			set<FFJSON*>::iterator it= mdts.find(f);
			if (it== mdts.end()) {
				rts[k]= f;
				++k;
			}
		}
	}
	return k-ik;
}

void addSearchNoDups (Pts& pts, FFJSON& reply, set<FFJSON*>& mdts,
							 int prevni, bool dupLnks = true) {
	int k= reply["things"].size;
	for (int i=prevni;i<pts.pni;++i) {
		NdNPrn& nd = pts.pts[i];
		FFJSON* f;
		if (nd.prn==(QuadNode*)-1) {
			f = (FFJSON*)nd.qh;
		} else {
			auto aa = getNode(nd);
			f = (FFJSON*)get<0>(aa);
		}
		flDbg(FLL, "finding in mdts");
		bool thingIsWithUser = dupLnks && mdts.find(f)!=mdts.end();
		flDbg(FLL, "thingIsWithUser: %d", thingIsWithUser);
		if (dupLnks && thingIsWithUser) {
			FFJSON& rt = reply["things"][k];
			rt["id"]= (*f)["id"];
			rt["user"]= &(*f)["user"]["name"];
		} else {
			reply["things"][k]=f;
		}
		++k;
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

static HTML_ thingsNoJsHtml;
static HTML_* thingHPtr;
static HTML_* thingImgHPtr;
static HTML_ indexHtml;
static char* isJsHtml;
thread_local char lusrnm[MAX_UN_LENGTH];

ccp yay = "{\"error\":\"yay\"}";
static size_t onCurlResponse (void* contents, size_t size, size_t nmemb,
										string* output) {
	size_t totalSize = size * nmemb;
	flDbg(FL,"%.*s", totalSize, contents);
	output->append((char*)contents, totalSize);
	return totalSize;
}
vector<FFJSON*> usersId;
HTML_* thingToHtml (FFJSON& thn) {
	int numPics= thn["pics"].size;
	ccp username= thn["user"]["name"];
	strcpy(lusrnm, username); tolower(lusrnm);
	HTML_* pThn= thingHPtr->cloneNode();
	int id= thn[id];
	HTML_& p= pThn->getElementById("Imgs");
	for (int i= 0; i < numPics; ++i) {
		HTML_* img= thingImgHPtr->cloneNode(true);
		img->setAttribute("src", string("/upload/")+ lusrnm+ "/"+
								to_string(id)+ "."+	to_string(i)+ ".jpg");
		p.insertAdjacentElement("beforeEnd", *img);
	}
	HTML_& thnLoc= pThn->getElementById("ThingLocation");
	thnLoc.tag= "a";
	string locStr;
	locStr+= to_string((float)thn["location"][0]);
	locStr+= ",";
	locStr+= to_string((float)thn["location"][1]);
	HTML_* loc= new HTML_(locStr.c_str());
	thnLoc.insertAdjacentElement("afterBegin", *loc);
	thnLoc.setAttribute("href", "https://maps.google.com?q="+locStr);
	HTML_& thnName= pThn->getElementById("ThingName");
	HTML_* thnAName= new HTML_((ccp)thn["name"]);
	thnName.insertAdjacentElement("afterBegin", *thnAName);
	ccp tid= to_string((int)thn["id"]).c_str();
	thnName.setAttribute("href",
								string("/")+(ccp)thn["user"]["name"]+"?thing="+tid);
	HTML_& thnUsr= pThn->getElementById("ThingUsr");
	HTML_* thnAUsr= new HTML_(username);
	thnUsr.insertAdjacentElement("afterBegin", *thnAUsr);
	HTML_& thnId= pThn->getElementById("ThingId");
	HTML_* thnAId= new HTML_(tid);
	thnId.insertAdjacentElement("afterBegin", *thnAId);
	HTML_& thnLstModd= pThn->getElementById("lastModed");
	time_t ts= thn["lastModed"];
	struct tm* tm_info = gmtime(&ts);
	char buf[128];
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
	HTML_* thnALstModd= new HTML_(buf);
	thnLstModd.insertAdjacentElement("afterBegin", *thnALstModd);
	HTML_& thnDtls= pThn->getElementById("ThingDetails");
	ccp thnDtlsCcp= thn["details"];
	if (thnDtlsCcp && strlen(thnDtlsCcp)) {
		HTML_* thnADtls= new HTML_(thnDtlsCcp);
		thnDtls.insertAdjacentElement("afterBegin", *thnADtls);
	}
	return pThn;
}

int ffDefault (string& bid, Txo& rbs, Txo& ffHttp, Txo& reply,
					MkHttpArgs& mhArgs, auto& now) {
	bool bidset= false;
	if (bid.length())
		if(rbs[bid])
			goto gotbid;
  newbid:
	bid= random_alphnuma_string();
	bidset= 1;
  bidcheck:
	if (rbs[bid]) {
		bid= random_alphnuma_string();
		goto bidcheck;
	}
	rbs[bid]["ip"]= (ccp)ffHttp["ip"];
  gotbid:
	if (!rbs[bid]["ip"] || strcmp(rbs[bid]["ip"], ffHttp["ip"])) {
		goto newbid;
	}
	FFJSON& rbsid= rbs[bid];
	rbsid["ts"]= now;
	reply["bid"]= bid;
	if (bidset) {
		snprintf(mesg, sizeof(mesg), "Set-Cookie: bid=%s", bid.c_str());
		mhArgs.addlHdrs= mesg;
		flDbg(FL, mhArgs.addlHdrs);
	}
	BidThings_& bts= bidThings[rbsid.val.fptr];
  cookieReply:
	setSavMtx.lock();
	pFSetToSave.insert(&rbs);
	setSavMtx.unlock();
	/*sends refresh request if no bid*/
	// if (bidset) {
	// 	mkHttpRes(ffHttp, isJsHtml, "text/html");
	// 	return 2;
	// }
	return 0;
}
int mkHtmlThings (Txo& ffHttp, Txo& reply, Pts& pts, int numThings, int dir,
						 string srch, string loc, bool init= false) {
	HTML_* pageHtml= indexHtml.cloneNode();
	HTML_& header= pageHtml->getElementById("header");
	HTML_& locH= header.getElementById("LocationBox");
	HTML_& srchH= header.getElementById("mouth");
	locH.setAttribute("value", loc);
	srchH.setAttribute("value", srch);
	HTML_* thingsDiv= new HTML_("<div id=\"things\"></div>");
	for (int i= 0; i < numThings; ++i) {
		FFJSON* thn= &reply["things"][i];
		HTML_* thingHtml= thingToHtml(*thn);
		thingsDiv->insertAdjacentElement("beforeEnd", *thingHtml);
	}
	string srchlog= "&gp="+ loc+ "&search="+ srch;
	if ((dir>0 && numThings >= 20) || (dir<0)) {
		HTML_* ldMr= thingsNoJsHtml.getElementById("loadBottom").cloneNode();
		string href= "?req=pts&dir=1&ni=";
		href+= to_string(pts.minPts);
		href+= srchlog;
		ldMr->setAttribute("href", href);
		header.insertAdjacentElement("afterEnd", *ldMr);
	}
	if (!init && !(dir<0 && pts.minPts<=20)) {
		HTML_* ldPr= thingsNoJsHtml.getElementById("loadBottom").cloneNode();
		string href= "?req=pts&dir=-1&ni=";
		href+= to_string(pts.minPts);
		href+= srchlog;
		ldPr->setAttribute("href", href);
		ldPr->setAttribute("id", "ldPr");
		ldPr->content.children[0]->tag="<-";
		HTML_* nbsp= new HTML_("&nbsp;&nbsp;");
		header.insertAdjacentElement("afterEnd", *nbsp);
		header.insertAdjacentElement("afterEnd", *ldPr);
	}
	header.insertAdjacentElement("afterEnd", *thingsDiv);
	string htmlResponse;
	pageHtml->stringify(htmlResponse);
	delete pageHtml;
	return mkHttpRes(ffHttp, htmlResponse.c_str(), "text/html", -1, 200, "OK",
						  nullptr);
}
int ffSearch (
	FFJSON& payload, Txo& rbs, FFJSON& rbsid, FFJSON& tUsr, FFJSON& reply,
	FFJSON& ffHttp, bool noJs, long& lepoch, Txo& users, Txo& query,
	ccp username) {
	ccp qSrch= query["search"];
	if (!qSrch) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	string srchStr(qSrch);
	decodeURIComponent(srchStr);
	BidThings_& bts= bidThings[rbsid.val.fptr];
	set<FFJSON*>& mdts= bts.mdts;
	Pts& pts= (noJs||srchStr.length())?bts.search:bts.all;
	pts= Pts();
	if (payload["locked"]) { //just opened the page
		mdts.clear();
		if (username) {
			rbsid["urts"]= lepoch;
			FFJSON& user= users[lusrnm];
			addSmtgsToReply(users, user, reply, mdts, true, true);
			setSavMtx.lock();
			pFSetToSave.insert(&rbs);
			setSavMtx.unlock();
		}
	}
	if (tUsr) { //url with username
		Txo* txTName= &tUsr["name"];
		reply["name"]= txTName;
		if (query["thing"]) {
			Txo& uts= tUsr["things"];
			if (uts.size) {
				int tind= getIdChildInd(uts, atoi(query["thing"]));
				Txo& rts= reply["things"];rts.init("[]");
				rts[0]= &uts[tind];
			}
			return mkHttpRes(ffHttp, reply);
		}
		if (srchStr.length()) {
			srchStr+= " ";
		}
		ccp tname= (ccp)(*txTName);
		srchStr+= tname;
	}
	vector<string> mstr= metaname(srchStr.c_str());
	pts.ina= nametouint(mstr);
	vector<string> sharpnel;
	explode(",", string((ccp)query["gp"]), sharpnel);
	if (sharpnel.size()==2) {
		if(!rbsid["gp"])
			rbsid["gp"].init("[]");
		rbsid["gp"][0]= pts.c.y= stof(sharpnel[0]);
		rbsid["gp"][1]= pts.c.x= stof(sharpnel[1]);
	}
	int pni= pts.pni;
	flInf(FL, "searching %s at %f,%f\n", srchStr.c_str(), pts.c.x, pts.c.y);
	cvSrch.wait(modLk, []{return modQhCv.load()==0;});
	++searchCv;
	thnsTree.getPointsFromQuad(pts);
	--searchCv;
	cvMod.notify_all();
	addSearchNoDups(pts, reply, mdts, pni, !noJs);
	int numThings= reply["things"].size;
	if (!numThings) {
		reply["things"].init("[]");
	}
	if (!noJs) {
		mkHttpRes(ffHttp, reply);
	} else {
		mkHtmlThings(ffHttp, reply, pts, numThings, 1, srchStr,
						 string((ccp)query["gp"]), true);
	}
	return -1;
}

int ffPts (FFJSON& payload, FFJSON& rbsid, FFJSON& reply, FFJSON& ffHttp,
			  FFJSON& cookie, Txo& query) {
	flNtc(FL, "pts:");
	bool noJs= cookie["js"]? false : true;
	BidThings_& bts = bidThings[rbsid.val.fptr];
	set<FFJSON*>& mdts= bts.mdts;
	bool isSearch = noJs? true : payload["search"];
	Pts& pts= isSearch? bts.search : bts.all;
	int dir = noJs? atoi(query["dir"]) : payload["dir"];
	int pni= pts.pni;
	if (pni<20 || (dir!=1 && dir!=-1)) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	int ni= noJs? atoi(query["ni"]) : payload["ni"];
	if (ni==-1) ni= pni;
	reply["things"].init("[]");
	int tpts= ni+dir*20;
	if (tpts<0) tpts= 0;
	pts.minPts= tpts;
	if ((pni<pts.minPts && pni%20==0) || pts.pts.size()<=pts.minPts) {
		NdNPrn& nd= pts.cnd;
		QuadNode* tQN= nd.qh->qn();
		uint8_t tind= nd.qh-(QuadHldr*)tQN;
		nd.ds= 1;
		cvSrch.wait(modLk, []{return modQhCv.load()==0;});
		++searchCv;
		nd.qh->findNeighbours(
			pts, tQN, tind, nd.prn, nd.ind, nd.dx);
		--searchCv;
		cvMod.notify_all();
		addSearchNoDups(pts, reply, mdts, pni, false);
	} else {
		int i= tpts-20, j=0;
		if (tpts>pni) tpts= pni;
		for (; i < tpts; ++i,++j) {
			NdNPrn& nd= pts.pts[i];
			FFJSON* f;
			if (nd.prn==(QuadNode*)-1) {
				f= (FFJSON*)nd.qh;
			} else {
				auto aa= getNode(nd);
				f= (FFJSON*)get<0>(aa);
			}
			reply["things"][j]= f;
		}
   }
	int numThings= reply["things"].size;
	if (!noJs) {
		mkHttpRes(ffHttp, reply);
	} else {
		mkHtmlThings(ffHttp, reply, pts, numThings, dir,
						 string((ccp)query["search"]),
						 string((ccp)query["gp"]));
	}
	return -1;
}
int ffSignIn (FFJSON& payload, FFJSON& rbsid, FFJSON& reply, FFJSON& ffHttp,
				  FFJSON& users, string& bid, long& lepoch, Txo& rbs) {
	flNtc(FL, "SignIn");
	ccp username= payload["username"], password= payload["password"];
	if(username) {strcpy(lusrnm, username); tolower(lusrnm);}
	flNtc(FL, "\nUser: %s\nPass: %s", username, password);
	ccp gid= payload["gid"];
	FFJSON fres;
	if (!password) {
		if (!gid)
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		CURL* curl = curl_easy_init();
		if (!curl) return mkHttpRes(ffHttp, "{\"error\":3}", jsonMime);
		string readBuffer;
		string url("https://oauth2.googleapis.com/tokeninfo?id_token=");
		url += gid;
		flDbg(FL, "gurl: %s", url.c_str());
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onCurlResponse);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
		curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

		CURLcode res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);

		if (res != CURLE_OK)
			return mkHttpRes(ffHttp, "{\"error\":4}", jsonMime);
		flDbg(FL,"gglBuf: %s", readBuffer.c_str());
		fres.init(readBuffer);
		if (!fres["aud"])
			return mkHttpRes(ffHttp, "{\"signin\":\"false\"}", jsonMime);
		strcpy(lusrnm, (ccp)fres["email"]);
	}
	flNtcCntnu(FL, "lusrnm: %s\n", lusrnm);
	FFJSON& user= users[(ccp)lusrnm];
	if (!user) {
		return mkHttpRes(ffHttp, "{\"signin\":\"false\"}", jsonMime);
	}
	if ((gid || (user["password"] && !strcmp(password,user["password"])))
		 && !user["inactive"]) {
		strcpy(lusrnm, (ccp)user["name"]);tolower(lusrnm);
		rbsid["user"]= (ccp)lusrnm;
		rbsid["ip"]= (ccp)ffHttp["ip"];
		user["bid"]= bid;
		rbsid["urts"]= lepoch;
		addSmtgsToReply(users, user, reply, bidThings[rbsid.val.fptr].mdts);
		setSavMtx.lock();
		pFSetToSave.insert(&rbs);
		setSavMtx.unlock();
		return mkHttpRes(ffHttp, reply);
	} else {
		return mkHttpRes(ffHttp, "\{\"signin\":\"false\"}", jsonMime);
	}
}
int ffOwl (Txo& user, Txo& payload, ccp username, Txo& ffHttp,
			  Txo& users, long& lepoch, Txo& rbsid) {
	FFJSON& things= user["things"];
	FFJSON& smsgs= user["smsgs"];
	FFJSON& reps= user["reps"];
	int smind= smsgs.size;
	FFJSON& fQs= payload["Qs"];
	FFJSON::Iterator it;
	long urts;
	long lmts;
	int i,j;
	if (!fQs) {
		goto rqs;
	}
	it= fQs.begin();
	while (it!=fQs.end()) {
		ccp tuser= (ccp)it;
		if (!strcmp(tuser, lusrnm)) {
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
			rmsgs[rmind]["id"]= rmid;
			rmsgs[rmind]["user"]= (ccp)lusrnm;
			rmsgs[rmind]["msg"]= *tit;
			rmsgs[rmind]["ts"]= lepoch;
			rmsgs[rmind]["new"]= true;
			rmsgs[rmind]["smind"]= smind;
			smsgs[smind].init("[]");
			smsgs[smind][0]= tuser;
			smsgs[smind][1]= tid;
			smsgs[smind][2]= rmid;
			*tit= rmid;
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
	pFSetToSave.insert(&user);
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
	pFSetToSave.insert(&user);
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
	pFSetToSave.insert(&user);
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
		int smind= reps[i];
		FFJSON& smsg= smsgs[smind];
		FFJSON& tusrts= users[(ccp)smsg[0]]["things"];
		int tind= getIdChildInd(tusrts, (int)smsg[1]);
		FFJSON& trmsgs= tusrts[tind]["rmsgs"];
		int mind= getIdChildInd(trmsgs, (int)smsg[2]);
		FFJSON& rep= payload["rnews"][j];
		rep= smsg;
		rep[3]= trmsgs[mind]["rep"];
		rep[4]= smind;
		--i;++j;
		if (i<0) {
			break;
		}
		lmts= (long)reps[i];
	} while (urts<lmts);
  reps://post reply to target user thing
	FFJSON& fRps= payload["Reps"];
	if (!fRps) {
		goto owldone;
	}
	it= fRps.begin();
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
			smind= smsgs.size;
			rmsgs[mind]["rep"]= *tit;
			smsgs[smind].init("[]");
			smsgs[smind][0]= "";
			smsgs[smind][1]= tid;
			smsgs[smind][2]= mid;
			FFJSON& tusr= users[(ccp)rmsgs[mind]["user"]];
			FFJSON& treps= tusr["reps"];
			if (!treps) {
				treps.init("[]");
			}
			treps[treps.size]= rmsgs[mind]["smind"];
			treps[treps.size]= lepoch;
			setSavMtx.lock();
			pFSetToSave.insert(&tusr);
			setSavMtx.unlock();
			++tit;
		}
		++it;
	}
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	setSavMtx.unlock();
	payload["status"]=1;
  owldone:
	payload["status"]=1;
	rbsid["urts"]=lepoch;
	return mkHttpRes(ffHttp, payload);
}
int ffSleep (Txo& ffHttp, Txo& query) {
	int sd= atoi((ccp)query["time"]);
	sleep(sd);
	sprintf(mesg, "slept for %d", sd);
	return mkHttpRes(ffHttp, mesg);
}
int ffActivate (Txo& ffHttp, Txo& query, Txo& users, long& lepoch,
					 ccp username) {
	username= query["user"];
	if (!username) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	strcpy(lusrnm, username); tolower(lusrnm);
	FFJSON& user= users[(ccp)lusrnm];
	if ((!user["password"] || !user["inactive"]) && !user["newpassword"]) {
		return mkHttpRes(ffHttp, "{\"error\":\"wrongKey\"}", jsonMime, -1, 400);
	} else if (!strcmp(user["activationKey"],query["key"])) {
		if (user["newpassword"]) {
			user["password"]= user["newpassword"];
			user["newpassword"]= false;
		}
		user["name"]= username;
		user["inactive"]= false;
		if (!user["things"]) {
			user["id"]= users.size;
			usersId.push_back(&user);
			user["things"].init("[]");
			user["smsgs"].init("[]");
			user["reps"].init("[]");
			user["lmts"]= lepoch;
		}
		setSavMtx.lock();
		pFSetToSave.insert(&user);
		pFSetToSave.insert(&users);
		setSavMtx.unlock();
		sprintf(mesg, "%s activated", username);
		return mkHttpRes(ffHttp, mesg);
	} 
	return mkHttpRes(ffHttp, "{\"error\":\"wrongKey\"}", jsonMime, -1, 400);
}
int ffSendUsrThings (Txo& ffHttp, Txo& reply, Txo& query, Txo& tUsr) {
	Txo& rthns= reply["things"];
	Txo& uthns= tUsr["things"];
	if (query["thing"]) {
		rthns.init("[]");
		rthns[0]= &uthns[getIdChildInd(uthns, atoi(query["thing"]))];
	} else {
		rthns= &uthns;
	}
	reply["name"]= &tUsr["name"];
	Pts pts; return mkHtmlThings(
		ffHttp, reply, pts, rthns.size, 0, "", "0,0", true);
}
int ffUpdThn (Txo& ffHttp, Txo& reply, Txo& user, Txo& payload,
				  long& lepoch, Txo& users) {
	if (payload["things"]) {
		FFJSON& cthings= payload["things"];
		FFJSON& uthings= user["things"];
		bool newthing= false;
		int id= 0;
		if (!(bool)uthings) {
			uthings.init("[]");
		}
		int j= 0;
		for (int i= 0; i<cthings.size; ++i) {
			if (!cthings[i])
				continue;
			if (i>uthings.size) {
				return mkHttpRes(ffHttp, "{\"error\":\"sizeExceeded\"}", jsonMime,
									  -1, 400);
			}
			FFJSON& fcthing= cthings[i];
			FFJSON& fcname= fcthing["name"];
			string cname((ccp)fcname);
			if (!isValidThingName(fcname)) {
				return mkHttpRes(ffHttp, "{\"error\":\"invalidThingName\"}",
									  jsonMime, -1, 400);
			}
			j= getIdChildInd(uthings, (int)fcthing["id"]);
			bool locChanged= false;
			bool nameChanged= false;
			cthings[i].erase("user");
			vector<string> mstr;
			vector<uint> ina;
			FFJSON& fcloc= fcthing["location"];
			FFJSON& futhing= j<0?uthings[uthings.size]:uthings[j];
			FFJSON& funame= futhing["name"];
			bool moded= false;
			if (j<0) {
				j= uthings.size;
				futhing["id"]= j>1?(int)uthings[j-2]["id"]+1:1;
				FFJSON& ln= futhing["user"].addLink(users, lusrnm);
				if (!ln)
					delete &ln;
				funame= fcname;
				nameChanged= true;
				if (!isValidLocation(fcloc)) {
					return mkHttpRes(ffHttp, 
										  "{\"error\":\"invalidLocation\"}", jsonMime, -1, 400);
				} else {
					futhing["location"]= fcloc;
					locChanged= true;
				}
				moded= true;
			} else {
				string uname(funame?(ccp)funame:"");
				FFJSON::trimWhites(cname);
				FFJSON::trimWhites(uname);
				tolower(cname);
				tolower(uname);
				if (!uname.length()) {
					nameChanged= true;
					funame= fcname;
					goto updateLoc;
				}
				mstr= metaname(uname+" "+lusrnm);
				if (cname!=uname) {
					for (int k=0; k<mstr.size(); ++k) {
						map<string, FFJSON*>::iterator it =
							nameints->find(mstr[k]);
						if (it->second->val.number==1) {
							mitposMtx.lock();
							mitpos.erase(mitpos.find(&it->first));
							fnameints->erase(mstr[k]);
							mitposMtx.unlock();
						} else {
							mitposMtx.lock();
							--(*nameints)[mstr[k]]->val.number;
							mitposMtx.unlock();
						}
					}
					nameChanged=true;
					funame=fcname;
				}
			  updateLoc:
				FFJSON& fuloc = futhing["location"];
				if (!isValidLocation(fcloc)) {
					return mkHttpRes(ffHttp, 
										  "{\"error\":\"invalidLocation\"}", jsonMime, -1, 400);
				}
				if (((double)fcloc[0]!=(double)fuloc[0] ||
					  (double)fcloc[1]!=(double)fuloc[1])) {
					fuloc=fcloc;
					locChanged=true;
				}
				if (nameChanged||locChanged) {
					moded=true;
					ina = nametouint(mstr);
					cvMod.wait(modLk, [] {return searchCv.load()==0;});
					++modQhCv;
					FFQuad_ fq(uthings[j], ina, 0, 0, true);
					thnsTree.insert(fq);
					--modQhCv;
					cvSrch.notify_all();
				}
			}
			FFJSON& fcthnDtls = fcthing["details"];
			if (fcthnDtls) {
				if (isValidThingDetails(fcthnDtls)) {
					futhing["details"]=fcthnDtls;
					moded=true;
				} else {
					return mkHttpRes(ffHttp, 
										  "{\"error\":\"invalidThingDetails\"}", jsonMime, -1, 400);
				}
			}
			if (nameChanged) {
				mstr=metaname(cname+" "+lusrnm);
				for (int k=0; k<mstr.size(); ++k) {
					mitposMtx.lock();
					map<string, FFJSON*>::iterator it =
						nameints->find(mstr[k]);
					if (it==nameints->end()) {
						(*fnameints)[mstr[k]]=1;
						it=nameints->find(mstr[k]);
						mitpos[&it->first]=nameints->size()-1;
					} else {
						++(*nameints)[mstr[k]]->val.number;
					}
					mitposMtx.unlock();
				}
				ina=nametouint(mstr);
			}
			if (locChanged||nameChanged) {
				FFQuad_ fq(futhing, ina);
				thnsTree.insert(fq);
			}
			if (moded) {
				futhing["lastModed"]=lepoch;
			}
			reply["things"][reply["things"].size]=&futhing;
		}
	}
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	pFSetToSave.insert(fnameints);
	setSavMtx.unlock();
	return mkHttpRes(ffHttp, reply);
}
int ffupload (Txo& ffHttp, Txo& reply, Txo& user, long& lepoch,
				  Txo& users, Txo& query, int& cfgMaxThings,
				  int& cfgMaxThingPics, ccp cpld) {
	int uMaxThings = user["maxThings"];
	int uMaxThingPics = user["maxThingPics"];
	int maxThings = uMaxThings?uMaxThings:cfgMaxThings;
	int maxThingPics = uMaxThingPics?uMaxThingPics:cfgMaxThingPics;
	int thingId = atoi(query["thingId"]);
	int picId = atoi(query["picId"]);
	int fofst = atoi(query["offset"]);
	int chnkSz= atoi(query["chunkSize"]);
	int ttlSz = atoi(query["totalSize"]);
	int thngi = -1;
	FFJSON& uthings = user["things"];
	if (thingId < 0) {
		if (uthings && uthings.size>=maxThings) {
			flNtc (
				HL,
				"user[\"things\"].size: %d", uthings.size
			);
			return mkHttpRes(ffHttp, "{\"error\":\"thingsAreAtMax\"}",
								  jsonMime, -1, 400);
		}
		if (uthings && uthings.size) {
			thngi= uthings.size;
			thingId= (int)uthings[thngi-1]["id"]+1;
		} else {
			thngi= 0;
			thingId= 1;
		}
	} else {
		thngi=getIdChildInd(uthings, thingId);
		if (thngi<0) {
			return mkHttpRes(ffHttp, "{\"error\":\"noSuchThingId\"}",
								  jsonMime, -1, 400);
		}
	}
  gotThingId:
	flNtc (HL, "picId: %d, maxThingPics: %d, thingId: %d, thngi: %d", picId,
			 maxThingPics, thingId, thngi);
	if (picId >= maxThingPics) {
		return mkHttpRes(ffHttp, "{\"error\":\"picsAreAtMax\"}", jsonMime,
							  -1, 400);
	}
	string upldpth(wdir);
	upldpth+= "/upload/";
	upldpth+= lusrnm;
	upldpth+= "/";
	upldpth+= to_string(thingId);
	upldpth+= ".";
	upldpth+= to_string(picId);
	upldpth+= ".jpg";
	flNtc(FL, "receiving: %s", upldpth.c_str());
	ios_base::openmode ofmode;
	if (fofst== 0) {
		uthings[thngi]["id"]= thingId;
		if (!uthings[thngi]["user"]) {
			uthings[thngi]["user"].addLink(users, lusrnm);
		}
		FFJSON& ups= uthings[thngi]["pics"];
		ups[picId]["partial"]= true;
		ofmode= std::ios::trunc;
	} else {
		ofmode= ios::in|ios::out|ios::ate;
	}
	ofstream upfile(upldpth.c_str(), ofmode | std::ios::binary);
	if (!upfile.is_open()) {
		reply["error"]= "createFailed";
		return mkHttpRes(ffHttp, reply.stringify(1).c_str(), jsonMime, -1,
							  400);
	}
	int initial_size= (int)(long long)upfile.tellp();
	if (initial_size!= fofst) {
		reply["lastChunk"]= initial_size;
		return mkHttpRes(ffHttp, reply.stringify(1).c_str(), jsonMime, -1,
							  400);
	}
	int wrByteCount= ffHttp["content-length"];
	upfile.write(cpld, wrByteCount);
	if (fofst+chnkSz>= ttlSz) {
		uthings[thngi]["pics"][picId].erase("partial");
		uthings[thngi]["pics"][picId]["ts"]= lepoch;
		setSavMtx.lock();
		pFSetToSave.insert(&user);
		setSavMtx.unlock();
	}
	upfile.close();
	reply["thingId"]= thingId;
	reply["picId"]= picId;
	return mkHttpRes(ffHttp, reply);
}
int ffSignUp (FFJSON& payload, FFJSON& rbsid, FFJSON& reply, FFJSON& ffHttp,
				  FFJSON& users, string& bid, long& lepoch, Txo& rbs, ccp username, ccp password, ccp proto, auto& now) {
	//signup
	bool recovery=false;
	flNtc(FL, "Signup");
	username= payload["username"];
	strcpy(lusrnm, username);tolower(lusrnm);
	ccp email= payload["email"];
	ccp gid= payload["gid"];
	if (!gid && !isValidEmail(payload["email"])) {
		flWrn(FL, "invalid email.");
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	} else if (!payload["consent"]) {
		flWrn(FL, "no consent");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-5,\"msg\":\"U didn't consent to this tool"
							  " usage :/\"}", jsonMime);
	} else if (
		!gid && (!payload["captcha"] || !rbsid["captcha"] ||
					strcmp(payload["captcha"], rbsid["captcha"]))!=0
	) {
		flWrn(FL, "Captcha mismatch.");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-4,\"msg\":\"Captcha mismatch, hmm!\"}",
							  jsonMime);
	}
	FFJSON& user= username?users[(ccp)lusrnm]:email?users[email]:nullFFJSON;
	if (email) {
		recovery=true;
		if (!user) {
			flWrn(FL, "%s Email not registered.",email);
			return mkHttpRes(ffHttp, 
								  "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
								  jsonMime);
		}
	}
	password= payload["password"];
	if (!password) {
		if (!gid)
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		CURL* curl= curl_easy_init();
		if (!curl) return mkHttpRes(ffHttp, "{\"error\":3}", jsonMime);
		string readBuffer;
		string url("https://oauth2.googleapis.com/tokeninfo?id_token=");
		url+= gid;
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onCurlResponse);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

		CURLcode res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);

		if (res != CURLE_OK) return mkHttpRes(ffHttp, "{\"error\":4}", jsonMime);
		FFJSON fres(readBuffer);
		if (!fres["aud"])
			return mkHttpRes(ffHttp, "{\"signin\":\"false\"}", jsonMime);
		email= fres["email"];
		strcpy(mesg, email);
		email= mesg;
	}
	if (!recovery && user && user["name"]) {
		flWrn(FL, "User already exists.");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-1,\"msg\":\"Username already taken, choose an"
							  " another :|\"}", jsonMime);
	} else if (!recovery && user["inactive"] &&
				  strcmp(email,user["email"])) {
		flWrn(FL, "User exists; mail mismatch");
		return mkHttpRes(
			ffHttp, "{\"actEmailSent\":-5,\"msg\":\"Email not registered!\"}",
			jsonMime);
	} else if (!recovery && users[email] && !user["email"]) {
		flWrn(FL, "Email already registered.");
		return mkHttpRes(
			ffHttp, "{\"actEmailSent\":-3,\"msg\":\"Email already registered! "
			"Try resetting password\"}", jsonMime);
	} else if (!recovery && !(validUsername(username))) {
		flWrn(FL, "Invalid password");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
							  jsonMime);
	} else if (!gid && !(password!=nullptr && validMD5(password))) {
		flWrn(FL, "Invalid password");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-6,\"msg\":\"Invalid password X|\"}",
							  jsonMime);
	}
	if (!recovery) {
		user["email"]= email;
		if (!gid) {
			user["password"]= password;
			user["inactive"] = true;
		} else {
			user["inactive"] = false;
		}
		users[email].addLink(users,lusrnm);
		filesystem::path
			usrpth(wdir+"/upload/"+lusrnm);
		filesystem::create_directory(usrpth);
	} else {
		user["newpassword"]= password;
		username= user["name"];
		strcpy(lusrnm, username); tolower(lusrnm);
	}
	if (!gid) {
		string actKey= random_alphnuma_string();
		user["activationKey"]= actKey;
		flNtc(FL, "actKey: %s",actKey.c_str());
		to= email;
		sprintf(subj, "User activation link");
		sprintf(
			mesg, "Open %s://%s/activate?user=%s&key=%s to activate "
			"%s", proto, (ccp)ffHttp["host"]["fqdn"], username,
			(ccp)user["activationKey"], username);
		string sfrom(admin);
		sfrom+= "@";
		sfrom+= from;
		sfrom+= ".com";
		if (sendMail(mailServer, mailPort, "plain", admin, adminPass,
						 sfrom, to, subj, mesg)!=0) {
			return mkHttpRes(
				ffHttp, "{\"error\":\"sendMailFailed\"}", jsonMime);
		}
	}
		
	if (!recovery) {
		FFJSON::FeaturedMember fm,pfm;
		pfm = users.getFeaturedMember(FFJSON::FM_FILE);
		int pfnl=strlen(pfm.m_sFileName)-1;
		while (pfm.m_sFileName[pfnl]!='/' && pfnl>=0) {
			--pfnl;
		}
		fm.m_sFileName = new char[pfnl+14+strlen(lusrnm)];
		sprintf(fm.m_sFileName, "%.*s/./users/%s.txo", pfnl, pfm.m_sFileName,
				  lusrnm);
		flDbg(FL, "userFile: %s", fm.m_sFileName);
		if (gid) {
			user["name"]= username;
			user["inactive"]= false;
			user["things"].init("[]");
			user["smsgs"].init("[]");
			user["reps"].init("[]");
			user["lmts"]= lepoch;
			user["bid"]= bid;
			rbsid["ts"]= now;
			rbsid["user"]= (ccp)lusrnm;
			rbsid["ip"]= (ccp)ffHttp["ip"];
			rbsid["urts"]= lepoch;
		}
		(*user).setEFlag(FFJSON::CASTFILE);
		(*user).setEFlag(FFJSON::FILE);
		(*user).insertFeaturedMember(fm, FFJSON::FM_FILE);
		setSavMtx.lock();
		pFSetToSave.insert(&users);
		setSavMtx.unlock();
	}
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	pFSetToSave.insert(&rbs);
	setSavMtx.unlock();
	if (gid) {
		reply= *user;
		reply["actEmailSent"]= 2;
		reply["msg"]= "Welcome to FerryFair!";
		return mkHttpRes(ffHttp, reply);
	}
	return mkHttpRes(
		ffHttp, 
		"{\"actEmailSent\":2,\"msg\":\"Activation mail sent to ur email :D\"}", jsonMime);
}

int ferryfair (FFJSON& ffHttp) {
	MkHttpArgs& mhArgs= ffHttp["resArgs"];
	FFJSON reply;
	static FFJSON& ffcfg= *pffcfg;
	static FFJSON& rbs= *prbs;
	static FFJSON& users= *pusers;
	static int cfgMaxThings= ffcfg["maxThings"];
	static int cfgMaxThingPics= ffcfg["maxThingPics"];
	ccp referer= nullptr;char proto[8]= "https"; int protolen;
	ccp username= nullptr, password= nullptr, cpld= nullptr;
	ccp path;
	string bid;
	FFJSON& cookie= ffHttp["cookie"];
	FFJSON payload;
	size_t req= 0;
	flNtc(FL, "cookie[bid]: %s", (ccp)cookie["bid"]);
	if (cookie["bid"]) {
		bid = (ccp)cookie["bid"];
	}
	auto now= chrono::system_clock::now();
	auto now_ms= chrono::time_point_cast<chrono::milliseconds>(now);
	long lepoch= now_ms.time_since_epoch().count();
	if (!ffHttp["referer"]) goto nextproto;
	referer= ffHttp["referer"];
	username= strstr(referer,":");
	protolen= username - referer;
	if (username== nullptr || protolen< 0 || protolen>= 8) {
		flDbg(FL, "badproto");
		return mkHttpRes(ffHttp, "badproto");
	}
	sprintf(proto,"%.*s",protolen,(ccp)ffHttp["referer"]);
  nextproto:
	username= nullptr;
	flDbg(FL, "proto: %s", proto);
	path= ffHttp["path"];
	if (path[0]=='/') {
		++path;
	}
	if ((path[0]== '.' && strstr(path, ".well-known/")!= path) ||
		 strstr(path,"red")) {
		flDbg(FL, "path: %s", path);
		return 2;
	}
	FFJSON& query = ffHttp["query"];
	if ((ccp)query["req"]) {
		req = fnv1a((ccp)query["req"]);
		flDbg(FL, "req: %zu", req);
	}
	FFJSON& tUsr= (path && path[0]!='\0' && strcpy(lusrnm, path) &&
						tolower(lusrnm))? users[(ccp)lusrnm] : nullFFJSON;
	cpld= ffHttp["payload"];
	ccp ctype= ffHttp["content-type"];
	if (cpld && ctype && strstr(ctype, "json")) {
		payload.init(cpld);
	}
	int defRet= ffDefault(bid, rbs, ffHttp, reply, mhArgs, now);
	if (defRet>1) {
		return defRet;
	}
	switch (req) {
	case "sleep"_hash:
		return ffSleep(ffHttp, query);
	case "activate"_hash:
		return ffActivate(ffHttp, query, users, lepoch, username);
	default:
		break;
	}

	FFJSON& rbsid= rbs[bid];
	bool noJs= cookie["js"]? false : true; 
	if (!rbsid) {
		if (tUsr) {
			if (noJs) return ffSendUsrThings(ffHttp, reply, query, tUsr);
			return 1;
		} else {
			return 0;
		}
	}
	switch (req) {
	case "signOut"_hash: {
	  signOut:
		rbsid["user"]= nullFFJSON;
		setSavMtx.lock();
		pFSetToSave.insert(&rbs);
		setSavMtx.unlock();
		return mkHttpRes(ffHttp, "{\"signOut\":true}", jsonMime);		
	}
	case "captcha"_hash: {
		flNtc(FL, "captcha");
		string tempPath(wdir+"/tmp/"+bid+".jpg");
		string randstr = random_alphnuma_string(7);
		cap randcap(randstr, tempPath, 7, 288, 68, 40, 80, 48);
		rbsid["captcha"]= randstr;
		randcap.save();
		rbs.clearEFlag(FFJSON::FILE);
		setSavMtx.lock();
		pFSetToSave.insert(&rbs);
		setSavMtx.unlock();
		return mkHttpRes(ffHttp, "{\"cap\":\"true\"}", jsonMime);
	}
	case "signIn"_hash: {
		return ffSignIn(payload, rbsid, reply, ffHttp, users, bid, lepoch, rbs);
	}
	case "pts"_hash: {
		return ffPts(payload, rbsid, reply, ffHttp, cookie, query);
	}
	case "signUp"_hash: {
		return ffSignUp(payload, rbsid, reply, ffHttp, users, bid, lepoch, rbs,
							 username, password, proto, now);
	}
	}
	username= rbsid["user"];
	if (!username) {
		if (tUsr) {
			if (noJs) return ffSendUsrThings(ffHttp, reply, query, tUsr);
		}
	} else {
		strcpy(lusrnm, username);
	}
	if (req == "search"_hash) {
		return ffSearch(payload, rbs, rbsid, tUsr, reply, ffHttp, noJs, lepoch,
							 users, query, username);
	}
	
	if (!username)
		if (tUsr)
			if(req)return 0;
			else return 1;
		else return 0;
	
	FFJSON& user= users[lusrnm];

	switch (req) {
	case "updateThing"_hash: {
		return ffUpdThn(ffHttp, reply, user, payload, lepoch, users);
	}
	case "upload"_hash: {
		return ffupload(ffHttp, reply, user, lepoch, users, query,
							 cfgMaxThings, cfgMaxThingPics, cpld);
	}
	case "owl"_hash: {
		return ffOwl(user, payload, username, ffHttp, users, lepoch,
						 rbsid);
	}
	}
	if (tUsr)
		return 1;
	return 0;
}

void makeThngsTree (Txo& cfg) {
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
	int ic= 0;
	vector<string> bmstr = metaname("flat gowtham");
	vector<uint> bina = nametouint(bmstr);
					
	while (it!= users.end()) {
		if (it->isType(FFJSON::LINK)) {
			++it;
			continue;
		}
		string user = it.getIndex();
		int id = (*it)["id"];
		while (usersId.size()<=id) {
			usersId.push_back(nullptr);
		}
		usersId[id]= &(*it);
		//adds users to nameints
		//vector<string> musr = metaname(user);
		FFJSON& uthings= (*it)["things"];
		//(*fnameints)[musr[0]]=uthings.size+(int)(*fnameints)[musr[0]];
		tit= uthings.begin();
		while (tit!=uthings.end()) {
			if (!((*tit)["name"].isType(FFJSON::UNDEFINED) ||
					(*tit)["location"].isType(FFJSON::UNDEFINED))) {
				FFJSON* pF= &*tit;
				//flDbg(FL, "inserting %d", ic);
				tpoolPtr->enqueue([pF, ic] (int tid) {
					FFJSON& rF= *pF;
					string tname((ccp)rF["name"]);
					tname+= " ";
					tname+= (ccp)rF["user"]["name"];
					vector<string> mstr = metaname(tname);
					vector<uint> ina = nametouint(mstr);
					for (int i=0; i<ina.size(); ++i)
						flDbgCntnu(FLL,"%x ", ina[i]);
					flDbgCntnu(FLL, "\n");
					float lx= rF["location"][1];
					float ly= rF["location"][0];
					flDbg(FL,"inserting %p: %s@%f,%f", pF, tname.c_str(), lx, ly);
					FFQuad_ fq(rF, ina, lx, ly);
					thnsTree.insert(fq);
					//flDbg(FL, "%d inserted %d", tid, ic);
				});
				// FFJSON& rF = *pF;
				// string tname((ccp)rF["name"]);
				// tname += (ccp)rF["user"]["name"];
				// vector<string> mstr = metaname(tname);
				// vector<uint> ina = nametouint(mstr);
				// bool found = true;
				// if (bina.size()>ina.size())
				//		goto skipFor;
				// for (int i=0; i<bina.size(); ++i) {
				//		if (bina[i]&ina[i]!=bina[i])
				//			found=false;
				// }
				// if (found) {
				//		int8_t matchCount = ffHasName(*pF, bina);
				//		flDbg(FL, "matchCount: %d", matchCount);
				//		goto insertEnd;
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
  insertEnd:
	setSavMtx.lock();
	pFSetToSave.insert(fnameints);
	setSavMtx.unlock();
	tpoolPtr->join();
}

void initFerryFair (FFJSON& cfg) {
	wdir=(ccp)cfg["rootdir"];
	FFJSON& ffcfg= cfg["cfg"];
	ffcfg.init(string("file://")+wdir+"/config.txo|OBJECT");
	flDbg(FL, "wdir: %s", wdir.c_str());
	pffcfg= &ffcfg;
	admin= ffcfg["secret"]["admin"];
	adminPass= ffcfg["secret"]["adminPass"];
	prbs= &ffcfg["rbs"];
	pusers= &ffcfg["users"];
	mailServer= ffcfg["secret"]["mailServer"];
	mailPort= ffcfg["secret"]["mailPort"];
	makeThngsTree(ffcfg);
	Pts pts;
	vector<string> mstr= metaname("gowtham");
	//vector<string> mstr= metaname("flat gowtham");
	//vector<string> mstr = metaname("Indulehka Bringha Hair Oil");
	pts.ina= nametouint(mstr);
	//Circle c = {180.0, 90.0, 10.5};
	//Circle c = {0.1, 0.1, 10.5};
	//Circle c = {0.9, 0.8, 10.5};
	//pts.c = {77.7584640, 12.9826816, 10.5};
	//pts.c = {77.7645299,12.9941367, 10.5};
	//pts.c = {77.7644272, 12.9940713, 10.5};
	//pts.c= {77.7644869, 12.9940933, 10.5}; // flat
	//pts.c= {82.2554938,17.0023267, 10.5}; // sowbagnil
	pts.c.x= 82.2554938;
	pts.c.y= 17.0023267;
	flDbg(FL, "c: %f,%f\n", pts.c.x, pts.c.y);
	FerryTimeStamp ftsStart;
	FerryTimeStamp ftsEnd;
	FerryTimeStamp ftsDiff;
	ftsStart.update();
	thnsTree.print(pts.c);
	//ina.push_back(0x80);
	thnsTree.getPointsFromQuad(pts);
	// cout << *pusers << endl;
	// return;
	ftsEnd.update();
	ftsDiff= ftsEnd - ftsStart;
	cout << "timeToFind= " << ftsDiff << endl;
	std::vector<NdNPrn>::iterator it= pts.pts.begin();
	it= pts.pts.begin();
	auto itend= it+pts.pni;
	while (it!=itend) {
		FFJSON* fp;
		if (it->prn==(QuadNode*)-1) {
			fp= (FFJSON*)it->qh;
		} else {
			fp= (FFJSON*)get<0>(getNode(*it));
		}
		printf("%s\n", (*fp)["location"].stringify().c_str());
		++it;
	}
	fs::path fswdir(wdir);
	fs::path fsThingsNoJsHtml= fswdir/"html/thingsNoJs.html";
	fs::path fsIndexHtml= fswdir/"index.html";
	fs::path fsIsJsHtml= fswdir/"html/isJs.html";
	ccp ccptnjh=  fileToStr(fsThingsNoJsHtml);
	thingsNoJsHtml.parse(ccptnjh);
	delete[] ccptnjh;
	thingImgHPtr= &thingsNoJsHtml.getElementById("ThingImg");
	thingImgHPtr->remove();
	thingHPtr= &thingsNoJsHtml.getElementById("Thing");
	ccp ccpih= fileToStr(fsIndexHtml);
	indexHtml.parse(ccpih);
	HTML_* htbl= &indexHtml.getElementById("htable");
	HTML_* about= &indexHtml.getElementById("about");
	HTML_* br= new HTML_("<br/>");
	HTML_* header= &indexHtml.getElementById("header");
	about->insertAdjacentElement("afterEnd", *br);
	header->insertAdjacentElement("beforeEnd", *br->cloneNode());
	htbl->remove();
	about->remove();
	delete htbl; delete about;
	delete[] ccpih;
	isJsHtml= fileToStr(fsIsJsHtml);
}

void uninitFerryFair () {
	delete thingImgHPtr;
	thnsTree.destroy();
	delete[] isJsHtml;
}
