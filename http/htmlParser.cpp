#include "htmlParser.h"
#include <sstream>
#include <stdexcept>
#include <new>
#include <filesystem>
#include <fstream>
#include <algorithm> // for remove

using namespace std;
namespace fs = std::filesystem;

// Constants (internal to cpp)
const int NORMAL = 0;
const int IN_COMMENT = 1;
const int IN_DOCTYPE = 2;
const int IN_CDATA = 3;
const int IN_TAG = 4;
const int IN_CLOSE_TAG = 5;
const int IN_RAW_TEXT = 6;

// Static member definition
HTML_ HTML_::nullHtml;

// Methods
HTML_::HTML_(const char* html_str) : parent(nullptr), type(NORML) {
   new (&content.children) vector<HTML_*>();
   string html(html_str ? html_str : "");
   parse(html);

   if (type == NORML && tag.empty() && attrs.empty() && content.children.size() == 1) {
      HTML_* child = content.children[0];
      if (child->type == TEXT) {
         type = TEXT;
         tag = child->tag;
         delete child;
         content.children.~vector<HTML_*>();
      }
   }
}

HTML_::HTML_() : parent(nullptr), type(NORML) {
    new (&content.children) vector<HTML_*>();
}

HTML_::~HTML_() {
   if (type == NORML || type == SELFCLOSING) {
      for (auto c : content.children) {
         delete c;
      }
      content.children.~vector<HTML_*>();
   }
}

size_t HTML_::stringify (
	string& out, const string& skipClass, const string& mustClass) const {
   size_t totalLen= calculateLength(skipClass, mustClass);
   size_t initialLen= out.length();
   out.reserve(initialLen + totalLen);
   stringifyHelper(out, skipClass, mustClass);
   return out.length() - initialLen;
}

size_t HTML_::calculateLength (const string& skipClass,
										 const string& mustClass) const {
   if (type == POINTER) return 0;
   if (hasAnyClass(skipClass) && !hasAnyClass(mustClass)) return 0;
   if (type == TEXT) return tag.length();
   
   size_t len = 1 + tag.length(); // "<tag"
   for (const auto& a : attrs) {
      len += 1 + a.first.length() + 4 + a.second.length(); // " key=\"val\""
   }
   
   if (type == SELFCLOSING) {
      len += 2; // "/>"
   } else {
      len += 1; // ">"
      for (auto c : content.children) {
         len += c->calculateLength(skipClass, mustClass);
      }
      len += 3 + tag.length(); // "</tag>"
   }
   return len;
}

void HTML_::stringifyHelper (
	string& out, const string& skipClass, const string& mustClass) const {
   if (type == POINTER) return;
   if (hasAnyClass(skipClass) && !hasAnyClass(mustClass)) return;
   if (type == TEXT) {
      out.append(tag);
      return;
   }

   out.append("<").append(tag);
   for (const auto& a : attrs) {
      out.append(" ").append(a.first).append("=\"").append(a.second).append("\"");
   }

   if (type == SELFCLOSING) {
      out.append("/>");
   } else {
      out.append(">");
      for (auto c : content.children) {
         c->stringifyHelper(out, skipClass, mustClass);
      }
      out.append("</").append(tag).append(">");
   }
}

vector<HTML_*> HTML_::getElementsByClassName(const string& className) {
   vector<HTML_*> res;
   if (type == TEXT || type == POINTER) return res;
   
   if (hasClass(className)) res.push_back(this);
   for (auto c : content.children) {
      auto sub = c->getElementsByClassName(className);
      res.insert(res.end(), sub.begin(), sub.end());
   }
   return res;
}

vector<HTML_*> HTML_::getElementsByAnyClassName(const string& className) {
   vector<HTML_*> res;
   if (type == TEXT || type == POINTER) return res;

   if (hasAnyClass(className)) res.push_back(this);
   for (auto c : content.children) {
      auto sub = c->getElementsByAnyClassName(className);
      res.insert(res.end(), sub.begin(), sub.end());
   }
   return res;
}

vector<HTML_*> HTML_::getElementsByTagName(const string& tagName) {
   vector<HTML_*> res;
   if (type == TEXT || type == POINTER) return res;

   if (this->tag == tagName) res.push_back(this);
   for (auto c : content.children) {
      auto sub = c->getElementsByTagName(tagName);
      res.insert(res.end(), sub.begin(), sub.end());
   }
   return res;
}

HTML_& HTML_::getElementById(const char* id) {
   if (type == TEXT || type == POINTER) return nullHtml;

   string id_str = id ? id : "";
   if (getAttribute("id") == id_str) {
      return *this;
   }
   for (auto c : content.children) {
      HTML_& res = c->getElementById(id);
      if (&res != &nullHtml) {
         return res;
      }
   }
   return nullHtml;
}

HTML_& HTML_::insertAdjacentElement(const char* position, HTML_& elm) {
    string pos = position ? position : "";
    if (pos == "beforeBegin") {
        if (parent) {
            auto& siblings = parent->content.children;
            auto it = find(siblings.begin(), siblings.end(), this);
            if (it != siblings.end()) {
                siblings.insert(it, &elm);
                elm.parent = parent;
                return elm;
            }
        }
    } else if (pos == "afterBegin") {
        content.children.insert(content.children.begin(), &elm);
        elm.parent = this;
        return elm;
    } else if (pos == "beforeEnd") {
        content.children.push_back(&elm);
        elm.parent = this;
        return elm;
    } else if (pos == "afterEnd") {
        if (parent) {
            auto& siblings = parent->content.children;
            auto it = find(siblings.begin(), siblings.end(), this);
            if (it != siblings.end()) {
                siblings.insert(it + 1, &elm);
                elm.parent = parent;
                return elm;
            }
        }
    }
    
    nullHtml.type = POINTER;
    return nullHtml;
}

HTML_* HTML_::cloneNode(bool deep) const {
   HTML_* clone = new HTML_();
   clone->tag = this->tag;
   clone->attrs = this->attrs;
   clone->type = this->type;
   clone->parent = nullptr; 

   if (type == TEXT) {
      clone->content.children.~vector<HTML_*>();
   } else if (type == POINTER) {
      clone->content.children.~vector<HTML_*>();
      clone->content.pointer = this->content.pointer;
   } else if (deep) {
      for (auto c : content.children) {
         HTML_* childClone = c->cloneNode(true);
         childClone->parent = clone;
         clone->content.children.push_back(childClone);
      }
   }
   return clone;
}

string HTML_::getAttribute(const string& name) const {
   for (auto& a : attrs) {
      if (a.first == name) return a.second;
   }
   return "";
}

void HTML_::setAttribute(const string& name, const string& value) {
   for (auto& a : attrs) {
      if (a.first == name) {
         a.second = value;
         return;
      }
   }
   attrs.emplace_back(name, value);
}

void HTML_::remove() {
   if (parent) {
      auto& siblings = parent->content.children;
      siblings.erase(std::remove(siblings.begin(), siblings.end(),
                                 this), siblings.end());
      parent = nullptr;
   }
}

bool HTML_::hasAnyClass(const string& classes) const {
   if (classes.empty()) return false;
   stringstream ss(classes);
   string token;
   while (ss >> token) {
      if (hasSingleClass(token)) return true;
   }
   return false;
}

void HTML_::parse (const string& html) {
   if (html.empty()) return;
   vector<string> voidTags = {
      "area", "base", "br", "col", "embed", "hr", "img", 
      "input", "link", "meta", "param", "source", "track", "wbr"
   };
   vector<HTML_*> stack;
   stack.push_back(this);
   HTML_* current = this;
   bool rootSet = false;
   bool rootClosed = false;
   int state = NORMAL;
   string buffer;
   string text_buffer;
   string raw_tag_to_close;
   char in_quotes = 0; 

   auto wrapRootInHtml = [&]() {
      HTML_* oldRoot = new HTML_();
      oldRoot->tag = this->tag;
      oldRoot->attrs = this->attrs;
      oldRoot->content.children = this->content.children;
      oldRoot->type = this->type;
      oldRoot->parent = this;

      for(auto c : oldRoot->content.children) {
          c->parent = oldRoot;
      }

      this->tag = "html";
      this->attrs.clear();
      this->content.children.clear();
      this->content.children.push_back(oldRoot);
      this->type = NORML;
   };

   auto create_text_node = [&](string& text) {
       if (text.empty() || !current) return;
       size_t start = text.find_first_not_of(" \t\n\r");
       if (start != string::npos) {
           if (rootClosed) {
               wrapRootInHtml();
               rootClosed = false;
               current = this;
           }
           size_t end = text.find_last_not_of(" \t\n\r");
           string trimmed_text = text.substr(start, end - start + 1);
           
           HTML_* text_node = new HTML_();
           text_node->content.children.~vector<HTML_*>();
           text_node->type = TEXT;
           text_node->tag = trimmed_text;
           text_node->parent = current;
           current->content.children.push_back(text_node);
       }
       text.clear();
   };

   for (size_t i = 0; i < html.size(); ++i) {
      char c = html[i];
      switch (state) {
         case NORMAL:
            if (c == '<') {
               create_text_node(text_buffer);
               if (i + 3 < html.size() && html.substr(i, 4) == "<!--") {
                  state = IN_COMMENT;
                  i += 3;
               } else if (i + 8 < html.size() && 
                          html.substr(i, 9) == "<!DOCTYPE") {
                  state = IN_DOCTYPE;
                  i += 8;
               } else if (i + 8 < html.size() && 
                          html.substr(i, 9) == "<![CDATA[") {
                  state = IN_CDATA;
                  i += 8;
               } else if (i + 1 < html.size() && html[i + 1] == '/') {
                  state = IN_CLOSE_TAG;
                  buffer = "";
                  ++i;
               } else {
                  state = IN_TAG;
                  buffer = "";
               }
            } else {
               text_buffer += c;
            }
            break;
         
         case IN_RAW_TEXT:
             {
                 string end_tag = "</" + raw_tag_to_close + ">";
                 if (i + end_tag.length() <= html.size() && 
                     html.substr(i, end_tag.length()) == end_tag) {
                     stack.pop_back();
                     current = stack.empty() ? nullptr : stack.back();
                     state = NORMAL;
                     i += end_tag.length() - 1;
                     
                     if (stack.size() == 1 && stack.back() == this) {
                        rootClosed = true;
                     }
                 } else {
                     if (current) {
                        if (current->content.children.empty() || current->content.children.back()->type != TEXT) {
                            HTML_* txt = new HTML_();
                            txt->content.children.~vector<HTML_*>();
                            txt->type = TEXT;
                            txt->parent = current;
                            current->content.children.push_back(txt);
                        }
                        current->content.children.back()->tag += c;
                     }
                 }
             }
             break;

         case IN_COMMENT:
         case IN_DOCTYPE:
         case IN_CDATA:
              if (state == IN_COMMENT && i + 2 < html.size() && 
                  html.substr(i, 3) == "-->") {
                 state = NORMAL;
                 i += 2;
              } else if (state == IN_DOCTYPE && c == '>') {
                 state = NORMAL;
              } else if (state == IN_CDATA && i + 2 < html.size() && 
                         html.substr(i, 3) == "]]>") {
                 state = NORMAL;
                 i += 2;
              }
              break;

         case IN_TAG:
            if (c == '>' && !in_quotes) {
               string tag_content = buffer;
               bool is_self_closing_by_slash = !tag_content.empty() && 
                                               tag_content.back() == '/';
               if (is_self_closing_by_slash) tag_content.pop_back();

               size_t space_pos = tag_content.find(' ');
               string tag_name = tag_content.substr(0, space_pos);
               string attr_part = (space_pos != string::npos) ? 
                                  tag_content.substr(space_pos + 1) : "";
               bool is_void = find(voidTags.begin(), voidTags.end(), 
                                   tag_name) != voidTags.end();

               if (rootClosed) {
                   wrapRootInHtml();
                   rootClosed = false;
                   current = this;
               }

               HTML_* node_to_process = nullptr;
               if (!rootSet) {
                  rootSet = true;
                  node_to_process = this;
                  current = this;
               } else {
                  HTML_* child = new HTML_();
                  child->parent = current;
                  if (current) current->content.children.push_back(child);
                  node_to_process = child;
               }
               
               node_to_process->tag = tag_name;
               node_to_process->attrs = parseAttrs(attr_part);
               node_to_process->type = (is_self_closing_by_slash || is_void) ? SELFCLOSING : NORML;

               if (node_to_process->type == NORML) {
                  if(tag_name == "pre" || tag_name == "code") {
                     state = IN_RAW_TEXT;
                     raw_tag_to_close = tag_name;
                  }
                  stack.push_back(node_to_process);
                  current = node_to_process;
               } else {
                   if (node_to_process == this) {
                       rootClosed = true;
                   }
               }
               
               state = (state == IN_TAG) ? NORMAL : state;
               buffer = "";
            } else {
               if ((c == '"' || c == '\'') && !in_quotes) {
                  in_quotes = c;
               } else if (c == in_quotes) {
                  in_quotes = 0;
               }
               buffer += c;
            }
            break;

         case IN_CLOSE_TAG:
            if (c == '>') {
               string closeTag = buffer;
               closeTag.erase(closeTag.find_last_not_of(" \t\n\r") + 1);
               if (!stack.empty() && stack.back()->tag == closeTag) {
                  bool closingRoot = (stack.size() == 2 && stack.back() == this);
                  stack.pop_back();
                  current = stack.empty() ? nullptr : stack.back();
                  if (closingRoot) rootClosed = true;
               }
               state = NORMAL;
               buffer = "";
            } else {
               buffer += c;
            }
            break;
      }
   }
   create_text_node(text_buffer);
}

bool HTML_::hasSingleClass(const string& cls) const {
   if (cls.empty()) return false;
   for (const auto& a : attrs) {
      if (a.first == "class") {
         const string& val = a.second;
         size_t pos = 0;
         while ((pos = val.find(cls, pos)) != string::npos) {
            bool startMatch = (pos == 0 || isspace(val[pos-1]));
            bool endMatch = (pos + cls.length() == val.length() || 
                            isspace(val[pos + cls.length()]));
            if (startMatch && endMatch) return true;
            pos += cls.length();
         }
      }
   }
   return false;
}

bool HTML_::hasClass(const string& cls) const {
   if (cls.empty()) return false;
   stringstream ss(cls);
   string token;
   while (ss >> token) {
      if (!hasSingleClass(token)) return false;
   }
   return true;
}

vector<pair<string, string>> HTML_::parseAttrs(const string& attr_str) {
   vector<pair<string, string>> attrs;
   size_t i = 0;
   size_t attr_len = attr_str.size();
   while (i < attr_len) {
      while (i < attr_len && isspace(attr_str[i])) ++i;
      if (i >= attr_len) break;
      
      size_t start = i;
      while (i < attr_len && !isspace(attr_str[i]) && 
             attr_str[i] != '=') {
         ++i;
      }
      string name = attr_str.substr(start, i - start);
      if (name.empty()) break;
      
      while (i < attr_len && isspace(attr_str[i])) ++i;
      if (i >= attr_len || attr_str[i] != '=') continue;
      ++i;
      
      while (i < attr_len && isspace(attr_str[i])) ++i;
      if (i >= attr_len) break;
      
      char quote = attr_str[i];
      string value;
      if (quote == '"' || quote == '\'') {
         ++i;
         start = i;
         while (i < attr_len && attr_str[i] != quote) ++i;
         value = attr_str.substr(start, i - start);
         if (i < attr_len) ++i;
      } else {
         start = i;
         while (i < attr_len && !isspace(attr_str[i])) ++i;
         value = attr_str.substr(start, i - start);
      }
      attrs.emplace_back(name, value);
   }
   return attrs;
}

string fileToStr (fs::path fspath) {
   ifstream index(fspath);
   ostringstream resStr;
   resStr << index.rdbuf();
   return resStr.str();
}

void printTree(HTML_* node, int depth) {
    for (int i = 0; i < depth; ++i) cout << "  ";
    cout << node->tag << " (id=" << node->getAttribute("id") << ", class=" << node->getAttribute("class") << ")" << endl;
    if (node->type == NORML || node->type == SELFCLOSING) {
        for (auto c : node->content.children) printTree(c, depth + 1);
    }
}
