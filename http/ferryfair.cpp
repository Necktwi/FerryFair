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
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/hmac.h>
#include <ctime>

using namespace std;

bool valgrind_test= false;
int valgrind_count= 1;
//mutex qhModMtx;
//unique_lock<mutex> modLk(qhModMtx);
//condition_variable cvMod, cvSrch;
//atomic<int> searchCv{0};
//atomic<int> modQhCv{0};
shared_mutex qtMtx;

struct BidThings_ {
	set<Txj*> mdts;
	Pts all;
	Pts search;
};
map<Txj*, BidThings_> bidThings;
thread_local ccp to= nullptr;
thread_local char subj[64];
thread_local char mesg[128];
ccp wpPrivKe, wpPubKe;

ccp admin, adminPass;
static ccp from= "FerryFair";
string wdir;
Txj* prbs= nullptr;
Txj* pffcfg= nullptr;
Txj* pusers= nullptr;
ccp mailServer= nullptr;
int mailPort= 0;

static HTML_ thingsNoJsHtml;
static HTML_* thingHPtr;
static HTML_* thingImgHPtr;
static HTML_ indexHtml;
static char* isJsHtml;
thread_local char lusrnm[MAX_UN_LENGTH];

ccp yay = "{\"error\":\"yay\"}";

// Web Push notification helpers
static void* fnv1a_hash(void*);
static void fnv1a_hash_init(void*);
static size_t onCurlResponse (void* contents, size_t size, size_t nmemb,
		string* output);
ccp vapidSub = "mailto:jack@ferryfair.com";

static string urlBase64Encode(const vector<uint8_t>& data) {
	const char b64[]=
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	string out;
	size_t n= data.size();
	out.reserve(n / 3 * 4 + 3);
	for (size_t i= 0; i < n; i += 3) {
		uint32_t x= data[i] << 16;
		bool n1= i + 1 < n, n2= i + 2 < n;
		if (n1) x |= data[i+1] << 8;
		if (n2) x |= data[i+2];
		out += b64[(x >> 18) & 0x3F];
		out += b64[(x >> 12) & 0x3F];
		if (n1) out += b64[(x >> 6) & 0x3F];
		if (n2) out += b64[x & 0x3F];
	}
	return out;
}
static string urlBase64Encode(const string& s) {
	return urlBase64Encode(vector<uint8_t>(s.begin(), s.end()));
}

static vector<uint8_t> urlBase64Decode(const string& in) {
	auto val= [](char c) -> int {
		if (c >= 'A' && c <= 'Z') return c - 'A';
		if (c >= 'a' && c <= 'z') return c - 'a' + 26;
		if (c >= '0' && c <= '9') return c - '0' + 52;
		if (c == '-') return 62;
		if (c == '_') return 63;
		return -1;
	};
	vector<uint8_t> out;
	out.reserve(in.size() / 4 * 3 + 3);
	uint32_t buf= 0;
	int bits= 0;
	for (char c : in) {
		int v= val(c);
		if (v < 0) continue;
		buf = (buf << 6) | (uint32_t)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((buf >> bits) & 0xFF);
		}
	}
	return out;
}
void makeThngsTree (Txj& cfg);
void initFerryFair (Txj& cfg) {
	wdir= (ccp)cfg["rootdir"];
	Txj& ffcfg= cfg["cfg"];
	ffcfg.init(string("file://")+wdir+"/config.txo|OBJECT");
	flDbg(FL, "wdir: %s", wdir.c_str());
	pffcfg= &ffcfg;
	admin= ffcfg["secret"]["admin"];
	adminPass= ffcfg["secret"]["adminPass"];
	wpPrivKe= ffcfg["secret"]["webPush"]["privateKey"];
	if (!wpPrivKe) {
		flErr(FL, "No webpush private key in"
				" red/secret.txo/webPush/privateKey");
		exit -1;
	}
	wpPubKe= ffcfg["secret"]["webPush"]["publicKey"];
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
		Txj* fp;
		if (it->prn==(QuadNode*)-1) {
			fp= (Txj*)it->qh;
		} else {
			fp= (Txj*)get<0>(getNode(*it));
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

int ffVapidKey (Txj& ffHttp, Txj& reply) {
	return mkHttpRes(ffHttp, wpPubKe, "text/plain", strlen(wpPubKe));
}

string createVapidAuthHeader (const char* vapid_priv, const string& audience) {
	if (!vapid_priv) return "";
	BIO* bio= BIO_new_mem_buf(vapid_priv, (int)strlen(vapid_priv));
	if (!bio) return "";
	EVP_PKEY* pkey= PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
	BIO_free(bio);
	if (!pkey) {
		flWrn(FL, "VAPID: cannot load private key");
		return "";
	}
	EC_KEY* eck= EVP_PKEY_get1_EC_KEY(pkey);
	string vapidKey, sig;
	if (eck) {
		const EC_GROUP* grp= EC_KEY_get0_group(eck);
		const EC_POINT* pub= EC_KEY_get0_public_key(eck);
		uint8_t pt[65];
		size_t ptlen= EC_POINT_point2oct(grp, pub, POINT_CONVERSION_UNCOMPRESSED,
			pt, sizeof(pt), nullptr);
		if (ptlen > 0)
			vapidKey= urlBase64Encode(vector<uint8_t>(pt, pt + ptlen));
	}
	string b64Hdr= urlBase64Encode(
		string("{\"typ\":\"JWT\",\"alg\":\"ES256\"}"));
	long exp= (long)time(nullptr) + 3600;
	string b64Pl= urlBase64Encode(string("{\"aud\":\"") + audience +
		"\",\"exp\":" + to_string(exp) + ",\"sub\":\"" +
		(vapidSub ? vapidSub : "") + "\"}");
	string signingInput= b64Hdr + "." + b64Pl;
	if (eck) {
		uint8_t digest[32];
		SHA256((const uint8_t*)signingInput.data(), signingInput.size(), digest);
		ECDSA_SIG* esig= ECDSA_do_sign(digest, sizeof(digest), eck);
		if (esig) {
			const BIGNUM* r, *s;
			ECDSA_SIG_get0(esig, &r, &s);
			uint8_t raw[64];
			BN_bn2binpad(r, raw, 32);
			BN_bn2binpad(s, raw + 32, 32);
			sig= urlBase64Encode(vector<uint8_t>(raw, raw + 64));
			ECDSA_SIG_free(esig);
		}
	}
	bool ok= !vapidKey.empty() && !sig.empty();
	if (eck) EC_KEY_free(eck);
	EVP_PKEY_free(pkey);
	if (!ok) {
		flWrn(FL, "VAPID: failed to build auth header");
		return "";
	}
	return "vapid t=" + b64Hdr + "." + b64Pl + "." + sig + ", k=" + vapidKey;
}

static string hmacSha256 (const uint8_t* key, size_t klen,
	const uint8_t* data, size_t dlen) {
	uint8_t out[32];
	unsigned int outlen= 0;
	if (HMAC(EVP_sha256(), key, (int)klen, data, dlen, out, &outlen) ==
			nullptr)
		return "";
	return string((const char*)out, outlen);
}

// RFC 8291 aes128gcm; no ephemeral key: ECDH uses the VAPID private key,
// keyid in the crypto header is as_public (the VAPID public key)
// keys arrive base64url of DER SubjectPublicKeyInfo; normalize to
// the raw 65-byte uncompressed P-256 point
static void wpRawP256 (vector<uint8_t>& key) {
	if (key.size() == 65) return;
	const uint8_t* dp= key.data();
	EVP_PKEY* pk= d2i_PUBKEY(nullptr, &dp, (int)key.size());
	if (pk && EVP_PKEY_base_id(pk) == EVP_PKEY_EC) {
		EC_KEY* eck= EVP_PKEY_get1_EC_KEY(pk);
		if (eck) {
			const EC_GROUP* g= EC_KEY_get0_group(eck);
			const EC_POINT* pt= EC_KEY_get0_public_key(eck);
			vector<uint8_t> raw(65);
			if (EC_POINT_point2oct(g, pt, POINT_CONVERSION_UNCOMPRESSED,
					raw.data(), (int)raw.size(), nullptr) ==
					(int)raw.size())
				key= raw;
			EC_KEY_free(eck);
		}
	}
	if (pk) EVP_PKEY_free(pk);
}

static vector<uint8_t> wpEncrypt (const string& msg,
	const vector<uint8_t>& uaPubIn, const vector<uint8_t>& uaAuth,
	const uint8_t* salt, vector<uint8_t>& asPub) {
	if (uaAuth.size() != 16 || !wpPubKe || !wpPrivKe)
		return vector<uint8_t>();
	vector<uint8_t> uaPub= uaPubIn;
	wpRawP256(uaPub);
	if (uaPub.size() != 65)
		return vector<uint8_t>();
	BIO* bio= BIO_new_mem_buf(wpPrivKe, (int)strlen(wpPrivKe));
	EVP_PKEY* pkey= bio ? PEM_read_bio_PrivateKey(bio, nullptr,
		nullptr, nullptr) : nullptr;
	if (bio) BIO_free(bio);
	EC_KEY* eck= pkey ? EVP_PKEY_get1_EC_KEY(pkey) : nullptr;
	if (pkey) EVP_PKEY_free(pkey);
	if (!eck) {
		flWrn(FL, "wp: cannot load vapid private key");
		return vector<uint8_t>();
	}
	const EC_GROUP* grp= EC_KEY_get0_group(eck);
	asPub.resize(65);
	if (EC_POINT_point2oct(grp, EC_KEY_get0_public_key(eck),
			POINT_CONVERSION_UNCOMPRESSED, asPub.data(),
			(int)asPub.size(), nullptr) != (int)asPub.size()) {
		flWrn(FL, "wp: bad vapid public key");
		EC_KEY_free(eck);
		return vector<uint8_t>();
	}
	EC_POINT* uaPt= EC_POINT_new(grp);
	uint8_t shared[32];
	size_t slen= 0;
	bool ok= uaPt &&
		EC_POINT_oct2point(grp, uaPt, uaPub.data(), uaPub.size(),
		nullptr) == 1;
	if (ok) {
		slen= ECDH_compute_key(shared, sizeof(shared), uaPt, eck, nullptr);
		ok= slen > 0;
	}
	EC_POINT_free(uaPt);
	EC_KEY_free(eck);
	if (!ok) {
		flWrn(FL, "wp: ecdh failed");
		return vector<uint8_t>();
	}
	string prkKey= hmacSha256(uaAuth.data(), uaAuth.size(),
		shared, sizeof(shared));
	string keyInfo= "WebPush: info";
	keyInfo += (char)0;
	keyInfo.append((const char*)uaPub.data(), uaPub.size());
	keyInfo.append((const char*)asPub.data(), asPub.size());
	string ikmIn= keyInfo;
	ikmIn += (char)1;
	string ikm= hmacSha256((const uint8_t*)prkKey.data(), prkKey.size(),
		(const uint8_t*)ikmIn.data(), ikmIn.size());
	string prk= hmacSha256(salt, 16, (const uint8_t*)ikm.data(),
		ikm.size());
	string cekIn= "Content-Encoding: aes128gcm";
	cekIn += (char)0;
	cekIn += (char)1;
	string nonceIn= "Content-Encoding: nonce";
	nonceIn += (char)0;
	nonceIn += (char)1;
	string cek= hmacSha256((const uint8_t*)prk.data(), prk.size(),
		(const uint8_t*)cekIn.data(), cekIn.size());
	string nonce= hmacSha256((const uint8_t*)prk.data(), prk.size(),
		(const uint8_t*)nonceIn.data(), nonceIn.size());
	vector<uint8_t> toEnc(msg.begin(), msg.end());
	toEnc.push_back(0x02);
	vector<uint8_t> ct(toEnc.size());
	uint8_t tag[16];
	EVP_CIPHER_CTX* gctx= EVP_CIPHER_CTX_new();
	int clen= 0;
	bool gok= gctx &&
		EVP_CipherInit_ex(gctx, EVP_aes_128_gcm(), nullptr, nullptr,
			nullptr, 1) == 1 &&
		EVP_CIPHER_CTX_ctrl(gctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) ==
			1 &&
		EVP_CipherInit_ex(gctx, nullptr, nullptr,
			(const uint8_t*)cek.data(), (const uint8_t*)nonce.data(),
			1) == 1;
	if (gok)
		gok= EVP_CipherUpdate(gctx, ct.data(), &clen, toEnc.data(),
			(int)toEnc.size()) == 1;
	if (gok) {
		uint8_t fin[16];
		int flen= 0;
		gok= EVP_CipherFinal_ex(gctx, fin, &flen) == 1 &&
			EVP_CIPHER_CTX_ctrl(gctx, EVP_CTRL_GCM_GET_TAG, 16, tag) ==
			1;
	}
	if (gctx) EVP_CIPHER_CTX_free(gctx);
	if (!gok) {
		flWrn(FL, "wp: aes gcm failed");
		return vector<uint8_t>();
	}
	vector<uint8_t> body;
	body.reserve(86 + ct.size() + sizeof(tag));
	body.insert(body.end(), salt, salt + 16);
	const uint8_t rs[4]= {0, 0, 16, 0};
	body.insert(body.end(), rs, rs + sizeof(rs));
	body.push_back((uint8_t)asPub.size());
	body.insert(body.end(), asPub.begin(), asPub.end());
	body.insert(body.end(), ct.begin(), ct.end());
	body.insert(body.end(), tag, tag + sizeof(tag));
	return body;
}

static vector<uint8_t> encryptWebPushMsg (const string& msg,
	const vector<uint8_t>& uaPub, const vector<uint8_t>& uaAuth,
	vector<uint8_t>& asPub) {
	uint8_t salt[16];
	if (RAND_bytes(salt, sizeof(salt)) != 1)
		return vector<uint8_t>();
	return wpEncrypt(msg, uaPub, uaAuth, salt, asPub);
}

int ffStoreSubscription (Txj& ffHttp, Txj& rbs, Txj& rbsid, Txj& payload,
								 Txj& reply) {
	if (payload.size <= 0) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	rbsid["wpSub"]= payload;
	flNtc(FL, "subscriptionAdded");
	reply["status"]= 1;
	setSavMtx.lock();
	pFSetToSave.insert(&rbs);
	setSavMtx.unlock();
	return mkHttpRes(ffHttp, reply);
}

static string endpointAud (const string& endpoint) {
	size_t s= endpoint.find("://");
	if (s == string::npos) return "";
	size_t e= endpoint.find('/', s + 3);
	if (e == string::npos) e= endpoint.size();
	return endpoint.substr(0, e);
}

int ffSendPush (Txj& ffHttp, Txj& rbsid, Txj& payload) {
	Txj& wpSub= rbsid["wpSub"];
	Txj keys= wpSub["keys"];
	ccp endpoint= wpSub["endpoint"];
	ccp p256dh= keys["p256dh"], authKe= keys["auth"];
	if (!endpoint || !p256dh || !authKe)
		return -1;
	vector<uint8_t> uaPub= urlBase64Decode(p256dh);
	vector<uint8_t> uaAuth= urlBase64Decode(authKe);
	Txj msg;
	string data= payload.stringify(1);
	vector<uint8_t> asPub;
	vector<uint8_t> enc= encryptWebPushMsg(data, uaPub, uaAuth, asPub);
	if (enc.empty()) {
		return -1;
	}
	string auth= createVapidAuthHeader(wpPrivKe, endpointAud(endpoint));
	string authHdr= "Authorization: " + auth;
	string ctHdr= "Content-Type: application/octet-stream";
	string ceHdr= "Content-Encoding: aes128gcm";
	// Firefox push service (Autopush) requires TTL, in seconds
	string ttlHdr= "TTL: 86400";
	struct curl_slist* hdrs= curl_slist_append(nullptr, authHdr.c_str());
	hdrs= curl_slist_append(hdrs, ctHdr.c_str());
	hdrs= curl_slist_append(hdrs, ceHdr.c_str());
	hdrs= curl_slist_append(hdrs, ttlHdr.c_str());
	CURL* curl= curl_easy_init();
	if (!curl) { curl_slist_free_all(hdrs); return -1;}
	string readBuffer;
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_URL, endpoint);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (const char*)enc.data());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)enc.size());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onCurlResponse);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	CURLcode res= curl_easy_perform(curl);
	long code= 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_slist_free_all(hdrs);
	curl_easy_cleanup(curl);
	if (res != CURLE_OK) {
		return code;
	} else {
		if (code == 410) {
			rbsid.erase("wpSub");
			flDbg(FL, "wpSub gone, removed");
			setSavMtx.lock();
			pFSetToSave.insert(&rbsid);
			setSavMtx.unlock();
		}
	}
	return code;
}

struct CompThingNameMatch {
	bool operator () (const tuple<Txj*,int8_t>& t1,
							const tuple<Txj*,int8_t>& t2) const {
		return (get<1>(t1) < get<1>(t2));
	}
};
int getIdChildInd (Txj& arr, int id) {
	int last = arr.size;
	last = id<last?id:last;
	for (int i=last-1;i>=0;++i) {
		if ((int)arr[i]["id"]==id) {
			return i;
		}
	}
	return -1;
}

/**
 * inserts thing in to reply r if not in mdts and adds other user's things
 * on which user has commented
 */
int addSmtgsToReply (Txj& users, Txj& user, Txj& r,
							set<Txj*>& mdts, bool usr= true, bool bNUts= false) {
	if (usr) {
		Txj q("{things:!}");//add all keys of user except things to r;
		user.answerObject(&q, nullptr, FerryTimeStamp(), &r);
	}
	Txj& ruts= r["uthings"];
	Txj& rmts= r["mthings"];
	Txj& uts= user["things"];
	int k= ruts->size;
	int l= rmts->size;
	if (bNUts) goto utsend;
	for (uint i= 0; i< uts.size; ++i) {
		Txj* f= &uts[i];
		if (to) {
			if (strcmp((*f)["user"], to))
				continue;
		}
		set<Txj*>::iterator it= mdts.find(f);
		if (it==mdts.end()) {
			ruts[k]= f;
			++k;
			mdts.insert(f);
		}
	}
  utsend:
	Txj::Iterator stit= user.find("smsgs");
	if (stit!=user.end()) {
		Txj& smsgs= *stit;
		Txj& rsmsgs= r["smsgs"];
		for (int i= 0; i< smsgs.size; ++i) {
			Txj& s= smsgs[i];
			if (!s[0].size)
				continue;
			Txj& uts= users[(ccp)s[0]]["things"];
			int tind= getIdChildInd(uts, (int)s[1]);
			Txj* f= &uts[tind];
			set<Txj*>::iterator it= mdts.find(f);
			if (it== mdts.end()) {
				rmts[l]= f;
				++l;
			}
		}
	}
	return k+l;
}

void addSearchNoDups (Pts& pts, Txj& reply, set<Txj*>& mdts,
							 int prevni, bool dupLnks= true) {
	int k= reply["things"].size;
	for (int i= prevni; i<pts.pni; ++i) {
		NdNPrn& nd= pts.pts[i];
		Txj* f;
		if (nd.prn==(QuadNode*)-1) {
			f= (Txj*)nd.qh;
		} else {
			auto aa= getNode(nd);
			f= (Txj*)get<0>(aa);
		}
		flDbg(FLL, "finding in mdts");
		bool thingIsWithUser= dupLnks && mdts.find(f)!=mdts.end();
		flDbg(FLL, "thingIsWithUser: %d", thingIsWithUser);
		if (dupLnks && thingIsWithUser) {
			Txj& rt= reply["things"][k];
			rt["id"]= (*f)["id"];
			rt["user"]= &(*f)["user"]["name"];
		} else {
			reply["things"][k]= f;
			mdts.insert(f);
		}
		++k;
	}
}

bool isValidEmail (Txj& tname) {
	if (tname.isType(Txj::STRING) && tname.size<48) {
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
bool isValidThingName (Txj& tname) {
	if (tname.isType(Txj::STRING) && tname.size>0 && tname.size<=64) {
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

bool isValidThingDetails (Txj& tname) {
	if (tname.isType(Txj::STRING) && tname.size<=256) {
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

bool isValidLocation (Txj& cloc) {
	if (cloc.isType(Txj::ARRAY) && cloc.size==2) {
		if (cloc[0].isType(Txj::NUMBER) && cloc[1].isType(Txj::NUMBER)) {
			return true;
		}
	}
	return false;
}
static size_t onCurlResponse (void* contents, size_t size, size_t nmemb,
										string* output) {
	size_t totalSize = size * nmemb;
	flDbg(FL,"%.*s", totalSize, contents);
	output->append((char*)contents, totalSize);
	return totalSize;
}
vector<Txj*> usersId;
HTML_* thingToHtml (Txj& thn) {
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
	string tid= to_string((int)thn["id"]);
	thnName.setAttribute("href",
								string("/")+(ccp)thn["user"]["name"]+"?thing="+tid);
	HTML_& thnUsr= pThn->getElementById("ThingUsr");
	HTML_* thnAUsr= new HTML_(username);
	thnUsr.insertAdjacentElement("afterBegin", *thnAUsr);
	HTML_& thnId= pThn->getElementById("ThingId");
	HTML_* thnAId= new HTML_(tid.c_str());
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

int ffDefault (string& bid, Txj& rbs, Txj& ffHttp,
					Txj& reply, MkHttpArgs& mhArgs, auto& now) {
	bool bidset= false;
	if (bid.length()) {
		if(rbs.find(bid)!=rbs.end())
			goto gotbid;
	}
  newbid:
	bid= random_alphnuma_string();
	bidset= 1;
  bidcheck:
	if (rbs.find(bid)!=rbs.end()) {
		bid= random_alphnuma_string();
		goto bidcheck;
	}
	rbs[bid]["ip"]= (ccp)ffHttp["ip"];
  gotbid:
	Txj& rbip= rbs[bid]["ip"];
	if (!rbip || (strcmp(rbip, ffHttp["ip"]) && false)) {
		goto newbid;
	}
	Txj& rbsid= rbs[bid];
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
int mkHtmlThings (Txj& ffHttp, Txj& reply, Pts& pts, int numThings, int dir,
						string srch, string loc, bool init= false) {
	HTML_* pageHtml= indexHtml.cloneNode();
	HTML_& header= pageHtml->getElementById("header");
	HTML_& locH= header.getElementById("LocationBox");
	HTML_& srchH= header.getElementById("mouth");
	locH.setAttribute("value", loc);
	srchH.setAttribute("value", srch);
	HTML_* thingsDiv= new HTML_("<div id=\"things\"></div>");
	for (int i= 0; i < numThings; ++i) {
		Txj* thn= &reply["things"][i];
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
int ffUsers (Txj& ffHttp, Txj& users) {
	Txj::Iterator it= users.begin();
	HTML_ usersDiv("<div id=\"users\"></div>");
	while (it!=users.end()) {
		ccp un= it;
		if (strstr(un, "@")) {++it;continue;}
		Txj& user= *it;
		if (!user) {++it; users.erase(un); continue;}
		if (user["inactive"]) {++it;continue;}
		ccp cun= user["name"];
		cout<< cun<< endl;
		vector<string> unv= explodeByCase(cun);
		string uns= implode(" ", unv);
		string link("/"); link+= un;
		string suser("<div><a href='"); suser+= link+"'>"+uns+"</a></div>";
		HTML_* huser= new HTML_(suser.c_str());
		usersDiv.insertAdjacentElement("beforeEnd",*huser);
		++it;
	}
	string htmlResponse;
	usersDiv.stringify(htmlResponse);
	return mkHttpRes(ffHttp, htmlResponse.c_str(),"text/html", -1, 200,
						  "OK", nullptr);	
}
int ffSearch (
	Txj& payload, Txj& rbs, Txj& rbsid, Txj& tUsr, Txj& reply,
	Txj& ffHttp, bool noJs, long& lepoch, Txj& users, Txj& query,
	ccp username
) {
	ccp qSrch= query["search"];
	if (!qSrch) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	string srchStr(qSrch);
	decodeURIComponent(srchStr);
	BidThings_& bts= bidThings[rbsid.val.fptr];
	set<Txj*>& mdts= bts.mdts;
	Pts& pts= (noJs||srchStr.length())?bts.search:bts.all;
	pts= Pts();
	if (payload["locked"]) { //just opened the page
		mdts.clear();
	}
	if (!tUsr || !payload["locked"]) {
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
		//cvSrch.wait(modLk, []{return modQhCv.load()==0;});
		//++searchCv;
		qtMtx.lock_shared();
		thnsTree.getPointsFromQuad(pts);
		qtMtx.unlock_shared();
		//--searchCv;
		//cvMod.notify_all();
		addSearchNoDups(pts, reply, mdts, pni, !noJs);
	} else { //url with username
		Txj* txTName= &tUsr["name"];
		reply["tname"]= txTName;
		reply["tdesc"]= &tUsr["desc"];
		Txj& uts= tUsr["things"];
		Txj& ruts= reply["things"];
		if (query["thing"]) {
			if (uts.size) {
				int tind= getIdChildInd(uts, atoi(query["thing"]));
				ruts.init("[]");
				ruts[0]= &uts[tind];
			}
			return mkHttpRes(ffHttp, reply);
		}
		ruts= &uts;
	}
	if (payload["locked"] && username) { //just opened the page
		rbsid["urts"]= lepoch;
		Txj& user= users[lusrnm];
		addSmtgsToReply(users, user, reply, mdts, true, tUsr?&tUsr==&user:0);
		Txj& wpSub= rbsid["wpSub"];
		if (wpSub)
			reply["wpSub"]= &wpSub;
		setSavMtx.lock();
		pFSetToSave.insert(&rbs);
		setSavMtx.unlock();
	}
	int numThings= reply["things"]->size;
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

int ffPts (Txj& payload, Txj& rbsid, Txj& reply, Txj& ffHttp,
			  Txj& cookie, Txj& query) {
	flNtc(FL, "pts:");
	bool noJs= cookie["js"]? false : true;
	BidThings_& bts= bidThings[rbsid.val.fptr];
	set<Txj*>& mdts= bts.mdts;
	bool isSearch= noJs? true : payload["search"];
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
		//cvSrch.wait(modLk, []{return modQhCv.load()==0;});
		//++searchCv;
		qtMtx.lock_shared();
		nd.qh->findNeighbours(
			pts, tQN, tind, nd.prn, nd.ind, nd.dx);
		qtMtx.unlock_shared();
		//--searchCv;
		//cvMod.notify_all();
		addSearchNoDups(pts, reply, mdts, pni);
	} else {
		int i= tpts-20, j=0;
		if (tpts>pni) tpts= pni;
		for (; i < tpts; ++i,++j) {
			NdNPrn& nd= pts.pts[i];
			Txj* f;
			if (nd.prn==(QuadNode*)-1) {
				f= (Txj*)nd.qh;
			} else {
				auto aa= getNode(nd);
				f= (Txj*)get<0>(aa);
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
int ffSignIn (Txj& payload, Txj& rbsid, Txj& reply, Txj& ffHttp,
				  Txj& users, string& bid, long& lepoch, Txj& rbs) {
	flNtc(FL, "SignIn");
	ccp username= payload["username"], password= payload["password"];
	if(username) {strcpy(lusrnm, username); tolower(lusrnm);}
	flNtc(FL, "\nUser: %s\nPass: %s", username, password);
	ccp gid= payload["gid"];
	Txj fres;
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
	Txj& user= users[(ccp)lusrnm];
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
		Txj& wpSub= rbsid["wpSub"];
		if (wpSub) {
			reply["wpSub"]=&wpSub;
		}
		setSavMtx.lock();
		pFSetToSave.insert(&rbs);
		pFSetToSave.insert(&user);
		setSavMtx.unlock();
		return mkHttpRes(ffHttp, reply);
	} else {
		return mkHttpRes(ffHttp, "\{\"signin\":\"false\"}", jsonMime);
	}
}
int ffSignOut (Txj& user, Txj& rbsid) {
	user["bid"]= nullTxj;
	rbsid["user"]= nullTxj;
	return 0;
}
int ffSignOutRes (Txj& ffHttp, Txj& user, Txj& rbs, Txj& rbsid) {
	ffSignOut(user,rbsid);
	setSavMtx.lock();
	pFSetToSave.insert(&rbs);
	setSavMtx.unlock();
	return mkHttpRes(ffHttp, "{\"signOut\":true}", jsonMime);
}

int ffOwl (Txj& user, Txj& payload, ccp username, Txj& ffHttp,
			  Txj& users, long& lepoch, Txj& rbs, Txj& rbsid) {
	Txj& things= user["things"];
	Txj& smsgs= user["smsgs"];
	Txj& reps= user["reps"];
	int smind= smsgs.size;
	Txj& fQs= payload["Qs"];
	Txj::Iterator it;
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
		Txj::Iterator tit;
		if (tuser) {
			tit= users.find(tuser);
		}
		if (!tuser || tit==users.end()) {
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		}
		Txj& tfuser= users[tuser];
		Txj& tfthings= tfuser["things"];
		tit= it->begin();
		while (tit!=it->end()) {
			ccp ctid= (ccp)tit;
			if (!ctid) {
				return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
			}
			int tid= atoi(ctid);
			int tind= getIdChildInd(tfthings, tid);
			if (tind<0) {
				return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
			}
			Txj& tfthing= tfthings[tind];
			Txj& rmsgs= tfthing["rmsgs"];
			if (!rmsgs) {
				rmsgs.init("[]");
			}
			int rmind= rmsgs.size;
			smind= smsgs.size;
			int rmid= 1;
			if (rmind) {
				rmid= (int)rmsgs[rmind-1]["id"]+1;
			}
			rmsgs[rmind]["id"]= rmid;
			rmsgs[rmind]["user"]= (ccp)lusrnm;
			Txj& frmsg= rmsgs[rmind]["msg"];
			frmsg= *tit;
			rmsgs[rmind]["ts"]= lepoch;
			rmsgs[rmind]["new"]= true;
			rmsgs[rmind]["smind"]= smind;
			smsgs[smind].init("[]");
			smsgs[smind][0]= tuser;
			smsgs[smind][1]= tid;
			smsgs[smind][2]= rmid;
			ccp tbid= tfuser["bid"];
			if (tbid) {
				Txj& trbsid= rbs[tbid];
				Txj pld; pld["user"]= &user["name"];
				pld["tid"]= tid;
				pld["thingName"]= &tfthing["name"];
				pld["msg"]= &frmsg;
				ffSendPush(ffHttp, trbsid, pld);
			}
			*tit= rmid;
			++tit;
		}
		tfuser["lmts"]= lepoch;
		setSavMtx.lock();
		pFSetToSave.insert(&tfuser);
		setSavMtx.unlock();
		++it;
	}
	payload["status"]= 1;
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	setSavMtx.unlock();
  rqs://mark query as read
	Txj& fRs= payload["Rs"];
	if (!fRs) {
		goto rrs;
	}
	it= fRs.begin();
	while (it!=fRs.end()) {
		ccp ctid= (ccp)it;
		if (!ctid) {
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		}
		int tid= atoi(ctid);
		int tind= getIdChildInd(things, tid);
		if (tind<0) {
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		}
		Txj& rmsgs= things[tind]["rmsgs"];
		Txj::Iterator tit= it->begin();
		while (tit!=it->end()) {
			int mid= (int)*tit;
			mid= getIdChildInd(rmsgs, mid);
			if (mid<0) {
				return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
			}
			rmsgs[mid].erase("new");
			++tit;
		}
		++it;
	}
	payload["status"]= 1;
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	setSavMtx.unlock();
  rrs://mark received replies as read
	Txj& frrs= payload["rrs"];
	if (!frrs) {
		goto news;
	}
	for (int i=0; i<frrs.size; ++i) {
		int smind= frrs[i];
		for (int j=0; j<reps.size; j+=2) {
			if ((int)reps[j]==smind) {
				reps.erase(j,j+2);
				break;
			}
		}
	}
	payload["status"]= 1;
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	setSavMtx.unlock();
  news://fetch if there are new queries
	urts= (long)rbsid["urts"];
	if (!user["lmts"]) {
		goto rnews;
	}
	lmts= (long)user["lmts"];
	if (urts>lmts) {
		goto rnews;
	}
	for (int i= 0; i<things.size; ++i) {
		Txj& rmsgs= things[i]["rmsgs"];
		for (int j= 0;j<rmsgs.size;++j) {
			Txj& msg= rmsgs[j];
			long mts= (long)msg["ts"];
			if (mts<urts) {
				continue;
			}
			payload["news"][to_string((int)things[i]["id"])]
				[to_string(j)]= msg;
		}
	}
	payload["status"]= 1;
  rnews://fetch if there are new replies
	if (!reps.size) {
		goto reps;
	}
	i= reps.size-1;
	lmts= (long)reps[i];
	if (urts>lmts) {
		goto reps;
	}
	j= 0;
	do {
		--i;
		int smind= reps[i];
		Txj& smsg= smsgs[smind];
		Txj& tusrts= users[(ccp)smsg[0]]["things"];
		int tind= getIdChildInd(tusrts, (int)smsg[1]);
		Txj& trmsgs= tusrts[tind]["rmsgs"];
		int mind= getIdChildInd(trmsgs, (int)smsg[2]);
		Txj& rep= payload["rnews"][j];
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
	Txj& fRps= payload["Reps"];
	if (!fRps) {
		goto owldone;
	}
	it= fRps.begin();
	while (it!=fRps.end()) {
		ccp ctid= (ccp)it;
		if (!ctid) {
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		}
		int tid= atoi(ctid);
		int tind= getIdChildInd(things, tid);
		if (tind<0) {
			return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
		}
		Txj& tfthing= things[tind];
		Txj& rmsgs= tfthing["rmsgs"];
		Txj::Iterator tit= it->begin();
		while (tit!=it->end()) {
			int mid= stoi((ccp)tit);
			int mind= getIdChildInd(rmsgs, mid);
			if (mind<0) {
				return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
			}
			smind= smsgs.size;
			Txj& frmsg= rmsgs[mind]["rep"];
			frmsg= *tit;
			smsgs[smind].init("[]");
			smsgs[smind][0]= "";
			smsgs[smind][1]= tid;
			smsgs[smind][2]= mid;
			Txj& tusr= users[(ccp)rmsgs[mind]["user"]];
			Txj& treps= tusr["reps"];
			if (!treps) {
				treps.init("[]");
			}
			treps[treps.size]= rmsgs[mind]["smind"];
			treps[treps.size]= lepoch;
			ccp tbid= tusr["bid"];
			if (tbid) {
				Txj& trbsid= rbs[tbid];
				Txj pld; pld["user"]= &user["name"];
				pld["tid"]= tid;
				pld["thingName"]= &tfthing["name"];
				pld["msg"]= &frmsg;
				ffSendPush(ffHttp, trbsid, pld);
			}
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
	payload["status"]= 1;
  owldone:
	payload["status"]= 1;
	rbsid["urts"]= lepoch;
	return mkHttpRes(ffHttp, payload);
}
int ffSleep (Txj& ffHttp, Txj& query) {
	int sd= atoi((ccp)query["time"]);
	sleep(sd);
	sprintf(mesg, "slept for %d", sd);
	return mkHttpRes(ffHttp, mesg);
}
int ffActivate (Txj& ffHttp, Txj& query, Txj& users, long& lepoch,
					 ccp username) {
	username= query["user"];
	if (!username) {
		return mkHttpRes(ffHttp, yay, jsonMime, -1, 400);
	}
	strcpy(lusrnm, username); tolower(lusrnm);
	Txj& user= users[(ccp)lusrnm];
	if ((!user["password"] || !user["inactive"]) && !user["newpassword"]) {
		return mkHttpRes(ffHttp, "{\"error\":\"wrongKey\"}", jsonMime, -1, 400);
	} else if (!strcmp(user["activationKey"],query["key"])) {
		if (user["newpassword"]) {
			user["password"]= user["newpassword"];
			user["newpassword"]= false;
		}
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
int ffSendUsrThings (Txj& ffHttp, Txj& reply, Txj& query, Txj& tUsr) {
	Txj& rthns= reply["things"];
	Txj& uthns= tUsr["things"];
	if (query["thing"]) {
		rthns.init("[]");
		rthns[0]= &uthns[getIdChildInd(uthns, atoi(query["thing"]))];
	} else {
		rthns= &uthns;
	}
	reply["name"]= &tUsr["name"];
	Pts pts; return mkHtmlThings(
		ffHttp, reply, pts, rthns->size, 0, "", "0,0", true);
}
int ffUpdThn (Txj& ffHttp, Txj& reply, Txj& user, Txj& payload,
				  long& lepoch, Txj& users) {
	if (payload["things"]) {
		Txj& cthings= payload["things"];
		Txj& uthings= user["things"];
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
			Txj& fcthing= cthings[i];
			Txj& fcname= fcthing["name"];
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
			Txj& fcloc= fcthing["location"];
			Txj& futhing= j<0?uthings[uthings.size]:uthings[j];
			Txj& funame= futhing["name"];
			bool moded= false;
			if (j<0) {
				j= uthings.size;
				futhing["id"]= j>1?(int)uthings[j-2]["id"]+1:1;
				Txj& ln= futhing["user"].addLink(users, lusrnm);
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
				Txj::trimWhites(cname);
				Txj::trimWhites(uname);
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
						map<string, Txj*>::iterator it =
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
					nameChanged= true;
					funame= fcname;
				}
			  updateLoc:
				Txj& fuloc = futhing["location"];
				if (!isValidLocation(fcloc)) {
					return mkHttpRes(ffHttp, 
										  "{\"error\":\"invalidLocation\"}", jsonMime, -1, 400);
				}
				if (((double)fcloc[0]!=(double)fuloc[0] ||
					  (double)fcloc[1]!=(double)fuloc[1])) {
					locChanged= true;
				}
				if (nameChanged||locChanged) {
					moded= true;
					ina= nametouint(mstr);
					//cvMod.wait(modLk, [] {return searchCv.load()==0;});
					//++modQhCv;
					FFQuad_ fq(uthings[j], ina, fuloc[1], fuloc[0], true);
					qtMtx.lock();
					thnsTree.insert(fq);
					qtMtx.unlock();
					if (locChanged)
						fuloc= fcloc;
					//--modQhCv;
					//cvSrch.notify_all();
				}
			}
			Txj& fcthnDtls= fcthing["details"];
			if (fcthnDtls) {
				if (isValidThingDetails(fcthnDtls)) {
					futhing["details"]= fcthnDtls;
					moded= true;
				} else {
					return mkHttpRes(ffHttp, 
										  "{\"error\":\"invalidThingDetails\"}", jsonMime, -1, 400);
				}
			}
			if (nameChanged) {
				mstr= metaname(cname+" "+lusrnm);
				for (int k=0; k<mstr.size(); ++k) {
					mitposMtx.lock();
					map<string, Txj*>::iterator it=
						nameints->find(mstr[k]);
					if (it==nameints->end()) {
						(*fnameints)[mstr[k]]= 1;
						it= nameints->find(mstr[k]);
						mitpos[&it->first]= nameints->size()-1;
					} else {
						++(*nameints)[mstr[k]]->val.number;
					}
					mitposMtx.unlock();
				}
				ina= nametouint(mstr);
			}
			if (locChanged||nameChanged) {
				FFQuad_ fq(futhing, ina, fcloc[1], fcloc[0]);
				qtMtx.lock();
				thnsTree.insert(fq);
				qtMtx.unlock();
			}
			if (moded) {
				futhing["lastModed"]= lepoch;
			}
			reply["things"][reply["things"].size]= &futhing;
		}
	}
	setSavMtx.lock();
	pFSetToSave.insert(&user);
	pFSetToSave.insert(fnameints);
	setSavMtx.unlock();
	return mkHttpRes(ffHttp, reply);
}
int ffupload (Txj& ffHttp, Txj& reply, Txj& user, long& lepoch,
				  Txj& users, Txj& query, int& cfgMaxThings,
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
	Txj& uthings = user["things"];
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
		Txj& ups= uthings[thngi]["pics"];
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
int ffSignUp (Txj& payload, Txj& rbsid, Txj& reply, Txj& ffHttp,
				  Txj& users, string& bid, long& lepoch, Txj& rbs, ccp username, ccp password, ccp proto, auto& now) {
	//signup
	bool recovery= false;
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
	Txj& user= username?users[(ccp)lusrnm]:email?users[email]:nullTxj;
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
		Txj fres(readBuffer);
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
	} else if (!recovery &&
				  (!validUsername(username) || !strcmp(username, "css") ||
					!strcmp(username, "files") || !strcmp(username, "fnt") ||
					!strcmp(username, "html") || !strcmp(username, "img") ||
					!strcmp(username, "js") || !strcmp(username, "upload"))) {
		flWrn(FL, "Invalid username");
		return mkHttpRes(ffHttp, 
							  "{\"actEmailSent\":-6,\"msg\":\"Invalid username X|\"}",
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
		Txj::FeaturedMember fm,pfm;
		pfm = users.getFeaturedMember(Txj::FM_FILE);
		int pfnl=strlen(pfm.m_sFileName)-1;
		while (pfm.m_sFileName[pfnl]!='/' && pfnl>=0) {
			--pfnl;
		}
		fm.m_sFileName = new char[pfnl+14+strlen(lusrnm)];
		sprintf(fm.m_sFileName, "%.*s/./users/%s.txo", pfnl, pfm.m_sFileName,
				  lusrnm);
		flDbg(FL, "userFile: %s", fm.m_sFileName);
		user["name"]= username;
		if (gid) {
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
		(*user).setEFlag(Txj::CASTFILE);
		(*user).setEFlag(Txj::FILE);
		(*user).insertFeaturedMember(fm, Txj::FM_FILE);
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

int ferryfair (Txj& ffHttp) {
	MkHttpArgs& mhArgs= ffHttp["resArgs"];
	Txj reply;
	static Txj& ffcfg= *pffcfg;
	static Txj& rbs= *prbs;
	static Txj& users= *pusers;
	static int cfgMaxThings= ffcfg["maxThings"];
	static int cfgMaxThingPics= ffcfg["maxThingPics"];
	ccp referer= nullptr;char proto[8]= "https"; int protolen;
	ccp username= nullptr, password= nullptr, cpld= nullptr;
	ccp path;
	string bid;
	Txj& cookie= ffHttp["cookie"];
	Txj payload;
	size_t req= 0;
	flNtc(FL, "cookie[bid]: %s", (ccp)cookie["bid"]);
	if (cookie["bid"]) {
		bid= (ccp)cookie["bid"];
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
	if (!strncasecmp(path, "users/", 6)) {
		return ffUsers(ffHttp, users);
	}
	Txj& query = ffHttp["query"];
	if ((ccp)query["req"]) {
		req = fnv1a((ccp)query["req"]);
		flDbg(FL, "req: %zu", req);
	}
	Txj& tUsr= (path && path[0]!='\0' && strcpy(lusrnm, path) &&
						tolower(lusrnm))? users[(ccp)lusrnm] : nullTxj;
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
	case "vapidpublickey"_hash:
		return ffVapidKey(ffHttp, reply);
	case "sleep"_hash:
		return ffSleep(ffHttp, query);
	case "activate"_hash:
		return ffActivate(ffHttp, query, users, lepoch, username);
	default:
		break;
	}

	Txj& rbsid= rbs[bid];
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
	case "captcha"_hash: {
		flNtc(FL, "captcha");
		string tempPath(wdir+"/tmp/"+bid+".jpg");
		string randstr = random_alphnuma_string(7);
		cap randcap(randstr, tempPath, 7, 288, 68, 40, 80, 48);
		rbsid["captcha"]= randstr;
		randcap.save();
		rbs.clearEFlag(Txj::FILE);
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
	
	Txj& user= users[lusrnm];

	switch (req) {
	case "signOut"_hash: {
		return ffSignOutRes(ffHttp, user, rbs, rbsid);
	}
	case "updateThing"_hash: {
		return ffUpdThn(ffHttp, reply, user, payload, lepoch, users);
	}
	case "upload"_hash: {
		return ffupload(ffHttp, reply, user, lepoch, users, query,
							 cfgMaxThings, cfgMaxThingPics, cpld);
	}
	case "owl"_hash: {
		return ffOwl(user, payload, username, ffHttp, users, lepoch,
						 rbs, rbsid);
	}
	case "notify"_hash: {
		return ffStoreSubscription(ffHttp, rbs, rbsid, payload, reply);
	}
	}
	if (tUsr)
		return 1;
	return 0;
}

void makeThngsTree (Txj& cfg) {
	fnameints= &cfg["nameints"];
	nameints= fnameints->val.pairs;
	map<string, Txj*>::iterator nit= nameints->begin();
	multiset<map<string, Txj*>::iterator, CompNameWt> namewtset(cmpNmWt);
	while (nit!=nameints->end()) {
		namewtset.insert(nit);
		++nit;
	}
	uint i=0;
	multiset<map<string, Txj*>::iterator, CompNameWt>::iterator mit
		= namewtset.begin();
	while (mit!=namewtset.end()) {
		mitpos[&((*mit)->first)]= i;
		++i;
		++mit;
	}
	Txj& users= cfg["users"];
	Txj::Iterator it= users.begin();
	Txj::Iterator tit;
	int ic= 0;
	vector<string> bmstr= metaname("flat gowtham");
	vector<uint> bina= nametouint(bmstr);
					
	while (it!= users.end()) {
		if (it->isType(Txj::LINK)) {
			++it;
			continue;
		}
		string user= it.getIndex();
		int id= (*it)["id"];
		while (usersId.size()<=id) {
			usersId.push_back(nullptr);
		}
		usersId[id]= &(*it);
		//adds users to nameints
		//vector<string> musr = metaname(user);
		Txj& uthings= (*it)["things"];
		//(*fnameints)[musr[0]]=uthings.size+(int)(*fnameints)[musr[0]];
		tit= uthings.begin();
		while (tit!=uthings.end()) {
			if (!((*tit)["name"].isType(Txj::UNDEFINED) ||
					(*tit)["location"].isType(Txj::UNDEFINED))) {
				Txj* pF= &*tit;
				//flDbg(FL, "inserting %d", ic);
				//tpoolPtr->enqueue([pF, ic] (int tid) {
					Txj& rF= *pF;
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
				//});
				// Txj& rF = *pF;
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
			// c.nf=(Txj*)1;
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
