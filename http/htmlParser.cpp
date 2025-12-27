#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace std;

const int NORMAL = 0;
const int IN_COMMENT = 1;
const int IN_DOCTYPE = 2;
const int IN_CDATA = 3;
const int IN_TAG = 4;
const int IN_CLOSE_TAG = 5;

class HTML_ {
public:
   string tag;
   string text;
   string doctype;
   string preamble;
   vector<pair<string, string>> attrs;
   vector<HTML_*> children;
   HTML_* parent;
   bool self_closing;

   HTML_ (const char* html_str) : parent(nullptr),
                                  self_closing(false) {
      string html(html_str ? html_str : "");
      parse(html);
   }

   ~HTML_ () {
      for (auto c : children) {
         delete c;
      }
   }

   string stringify () {
      ostringstream oss;
      oss << doctype << "<" << tag;
      for (auto& a : attrs) {
         oss << " " << a.first << "=\"" << a.second << "\"";
      }
      if (self_closing) {
         oss << "/>";
      } else {
         oss << ">";
         // preserve whitespace in pre/code
         if (tag == "pre" || tag == "code") {
            oss << text;
         } else {
            // trim text
            size_t start = text.find_first_not_of(" \t\n\r");
            size_t end = text.find_last_not_of(" \t\n\r");
            if (start != string::npos) {
               oss << text.substr(start, end - start + 1);
            }
         }
         for (auto c : children) {
            oss << c->stringify();
         }
         oss << "</" << tag << ">";
      }
      return oss.str();
   }

   vector<HTML_*> getElementsByClassName (const string& className) {
      vector<HTML_*> res;
      if (hasClass(className)) res.push_back(this);
      for (auto c : children) {
         auto sub = c->getElementsByClassName(className);
         res.insert(res.end(), sub.begin(), sub.end());
      }
      return res;
   }

   vector<HTML_*> getElementsByTagName (const string& tagName) {
      vector<HTML_*> res;
      if (this->tag == tagName) res.push_back(this);
      for (auto c : children) {
         auto sub = c->getElementsByTagName(tagName);
         res.insert(res.end(), sub.begin(), sub.end());
      }
      return res;
   }

   string getAttribute (const string& name) {
      for (auto& a : attrs) {
         if (a.first == name) return a.second;
      }
      return "";
   }

   void remove () {
      if (parent) {
         auto& siblings = parent->children;
         siblings.erase(std::remove(siblings.begin(), siblings.end(),
                                    this), siblings.end());
         parent = nullptr;
         // Note: Manual deletion required to avoid double-free with destructor
      }
   }

private:
   vector<pair<string, string>> parseAttrs (const string& attr_str) {
      vector<pair<string, string>> attrs;
      size_t i = 0;
      size_t attr_len = attr_str.size();
      while (i < attr_len) {
         // skip whitespace
         while (i < attr_len && isspace(attr_str[i])) ++i;
         if (i >= attr_len) break;
         // parse name
         size_t start = i;
         while (i < attr_len && !isspace(attr_str[i]) && attr_str[i] != '=') ++i;
         string name = attr_str.substr(start, i - start);
         if (name.empty()) break;
         // skip whitespace
         while (i < attr_len && isspace(attr_str[i])) ++i;
         if (i >= attr_len || attr_str[i] != '=') continue;
         ++i; // skip =
         // skip whitespace
         while (i < attr_len && isspace(attr_str[i])) ++i;
         if (i >= attr_len) break;
         char quote = attr_str[i];
         string value;
         if (quote == '"' || quote == '\'') {
            ++i; // skip quote
            start = i;
            while (i < attr_len && attr_str[i] != quote) ++i;
            value = attr_str.substr(start, i - start);
            if (i < attr_len) ++i; // skip closing quote
         } else {
            // unquoted value until space or end
            start = i;
            while (i < attr_len && !isspace(attr_str[i])) ++i;
            value = attr_str.substr(start, i - start);
         }
         // trim and collapse spaces in value
         value.erase(
            value.begin(), find_if(
               value.begin(), value.end(), [](int ch){return !isspace(ch);}));
         value.erase(find_if(value.rbegin(), value.rend(), [](int ch){return !isspace(ch);}).base(), value.end());
         string cleaned_value;
         bool last_space = false;
         for (char c : value) {
            if (isspace(c)) {
               if (!last_space) {
                  cleaned_value += ' ';
                  last_space = true;
               }
            } else {
               cleaned_value += c;
               last_space = false;
            }
         }
         attrs.emplace_back(name, cleaned_value);
      }
      return attrs;
   }

   bool hasClass (const string& cls) {
      for (auto& a : attrs) {
         if (a.first == "class" &&
             a.second.find(cls) != string::npos) {
            return true;
         }
      }
      return false;
   }

   void parse (const string& html) {
      if (html.empty()) return;
      vector<string> voidTags = {"area", "base", "br", "col", "embed",
                                 "hr", "img", "input", "link", "meta",
                                 "param", "source", "track", "wbr"};
      vector<HTML_*> stack;
      stack.push_back(this);
      HTML_* current = this;
      bool rootSet = false;
      int state = NORMAL;
      string buffer;
      size_t html_len = html.size();
      for (size_t i = 0; i < html_len; ++i) {
         char c = html[i];
         switch (state) {
            case NORMAL:
               if (c == '<') {
                  if (i + 3 < html_len && html.substr(i, 4) == "<!--") {
                     state = IN_COMMENT;
                     buffer = "<!--";
                     i += 3;
                  } else if (i + 8 < html_len && html.substr(i, 9) == "<!DOCTYPE") {
                     state = IN_DOCTYPE;
                     buffer = "<!DOCTYPE";
                     i += 8;
                  } else if (i + 8 < html_len && html.substr(i, 9) == "<![CDATA[") {
                     state = IN_CDATA;
                     buffer = "<![CDATA[";
                     i += 8;
                  } else if (i + 1 < html_len && html[i + 1] == '/') {
                     state = IN_CLOSE_TAG;
                     buffer = "";
                     ++i; // skip /
                  } else {
                     state = IN_TAG;
                     buffer = "";
                  }
               } else {
                  if (current) current->text += c;
               }
               break;

            case IN_COMMENT:
               buffer += c;
               if (i + 2 < html_len && html.substr(i, 3) == "-->") {
                  preamble += buffer + "-->";
                  state = NORMAL;
                  buffer = "";
                  i += 2;
               }
               break;

            case IN_DOCTYPE:
               buffer += c;
               if (c == '>') {
                  doctype += buffer;
                  state = NORMAL;
                  buffer = "";
               }
               break;

            case IN_CDATA:
               buffer += c;
               if (i + 2 < html_len && html.substr(i, 3) == "]]>") {
                  preamble += buffer + "]]>";
                  state = NORMAL;
                  buffer = "";
                  i += 2;
               }
               break;

            case IN_TAG:
               if (c == '>') {
                  string tag_content = buffer;
                  bool is_self_closing_by_slash = !tag_content.empty() && tag_content.back() == '/';
                  
                  if (is_self_closing_by_slash) {
                     tag_content.pop_back();
                  }

                  size_t space_pos = tag_content.find(' ');
                  string tag_name = tag_content.substr(0, space_pos);
                  string attr_part = (space_pos != string::npos) ? tag_content.substr(space_pos + 1) : "";

                  if (!rootSet) {
                     rootSet = true;
                     this->tag = tag_name;
                     this->attrs = parseAttrs(attr_part);
                     this->self_closing = is_self_closing_by_slash ||
                                          (find(voidTags.begin(), voidTags.end(), this->tag) != voidTags.end());
                     current = this;
                  } else {
                     HTML_* child = new HTML_("");
                     child->parent = current;
                     child->tag = tag_name;
                     child->attrs = parseAttrs(attr_part);
                     child->self_closing = is_self_closing_by_slash ||
                                           (find(voidTags.begin(), voidTags.end(), child->tag) != voidTags.end());
                     if (current) current->children.push_back(child);
                     if (!child->self_closing) {
                        stack.push_back(child);
                        current = child;
                     }
                  }
                  state = NORMAL;
                  buffer = "";
               } else {
                  buffer += c;
               }
               break;

            case IN_CLOSE_TAG:
               if (c == '>') {
                  string closeTag = buffer;
                  if (!stack.empty() && stack.back()->tag == closeTag) {
                     stack.pop_back();
                     current = stack.empty() ? nullptr : stack.back();
                  }
                  state = NORMAL;
                  buffer = "";
               } else {
                  buffer += c;
               }
               break;
         }
      }
   }
};
