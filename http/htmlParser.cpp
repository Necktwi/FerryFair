#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace std;

// Added new state for raw text blocks
const int NORMAL = 0;
const int IN_COMMENT = 1;
const int IN_DOCTYPE = 2;
const int IN_CDATA = 3;
const int IN_TAG = 4;
const int IN_CLOSE_TAG = 5;
const int IN_RAW_TEXT = 6;


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
      // If the tag is empty, this is a text node; just return the text.
      if (tag.empty()) {
         return text;
      }
      
      ostringstream oss;
      oss << doctype << "<" << tag;
      for (auto& a : attrs) {
         oss << " " << a.first << "=\"" << a.second << "\"";
      }

      if (self_closing) {
         oss << "/>";
      } else {
         oss << ">";
         // If tag is pre or code, output its stored raw text directly.
         if (tag == "pre" || tag == "code") {
            oss << text;
         } else {
            // Otherwise, stringify children which can be elements or text
            // nodes.
            for (auto c : children) {
               oss << c->stringify();
            }
         }
         oss << "</" << tag << ">";
      }
      return oss.str();
   }

   vector<HTML_*> getElementsByClassName (const string& className) {
      vector<HTML_*> res;
      if (hasClass(className)) res.push_back(this);
      for (auto c : children) {
         if (c->tag.empty()) continue;
         auto sub = c->getElementsByClassName(className);
         res.insert(res.end(), sub.begin(), sub.end());
      }
      return res;
   }

   vector<HTML_*> getElementsByTagName (const string& tagName) {
      vector<HTML_*> res;
      if (this->tag == tagName) res.push_back(this);
      for (auto c : children) {
         if (c->tag.empty()) continue;
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
      }
   }

private:
   HTML_() : parent(nullptr), self_closing(false) {}

   vector<pair<string, string>> parseAttrs (const string& attr_str) {
      vector<pair<string, string>> attrs;
      size_t i = 0;
      size_t attr_len = attr_str.size();
      while (i < attr_len) {
         while (i < attr_len && isspace(attr_str[i])) ++i;
         if (i >= attr_len) break;
         
         size_t start = i;
         while (i < attr_len && !isspace(attr_str[i]) && attr_str[i] != '=') ++i;
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
      vector<string> voidTags = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"};
      vector<HTML_*> stack;
      stack.push_back(this);
      HTML_* current = this;
      bool rootSet = false;
      int state = NORMAL;
      string buffer;
      string text_buffer;
      string raw_tag_to_close;

      auto create_text_node = [&](string& text) {
          if (text.empty() || !current) return;
          size_t start = text.find_first_not_of(" \t\n\r");
          if (start != string::npos) {
              size_t end = text.find_last_not_of(" \t\n\r");
              string trimmed_text = text.substr(start, end - start + 1);
              HTML_* text_node = new HTML_();
              text_node->tag = "";
              text_node->text = trimmed_text;
              text_node->parent = current;
              current->children.push_back(text_node);
          }
          text.clear();
      };

      for (size_t i = 0; i < html.size(); ++i) {
         char c = html[i];
         switch (state) {
            case NORMAL:
               if (c == '<') {
                  create_text_node(text_buffer);
                  if (i + 3 < html.size() && html.substr(i, 4) == "<!--") { state = IN_COMMENT; buffer = "<!--"; i += 3; }
                  else if (i + 8 < html.size() && html.substr(i, 9) == "<!DOCTYPE") { state = IN_DOCTYPE; buffer = "<!DOCTYPE"; i += 8; }
                  else if (i + 8 < html.size() && html.substr(i, 9) == "<![CDATA[") { state = IN_CDATA; buffer = "<![CDATA["; i += 8; }
                  else if (i + 1 < html.size() && html[i + 1] == '/') { state = IN_CLOSE_TAG; buffer = ""; ++i; }
                  else { state = IN_TAG; buffer = ""; }
               } else {
                  text_buffer += c;
               }
               break;
            
            case IN_RAW_TEXT:
                {
                    string end_tag = "</" + raw_tag_to_close + ">";
                    if (i + end_tag.length() <= html.size() && html.substr(i, end_tag.length()) == end_tag) {
                        stack.pop_back();
                        current = stack.empty() ? nullptr : stack.back();
                        state = NORMAL;
                        i += end_tag.length() - 1;
                    } else {
                        if (current) current->text += c;
                    }
                }
                break;

            case IN_COMMENT:
            case IN_DOCTYPE:
            case IN_CDATA:
                 buffer += c;
                 if (state == IN_COMMENT && i + 2 < html.size() && html.substr(i, 3) == "-->") { preamble += buffer + "-->"; state = NORMAL; buffer = ""; i += 2; }
                 else if (state == IN_DOCTYPE && c == '>') { doctype += buffer; state = NORMAL; buffer = ""; }
                 else if (state == IN_CDATA && i + 2 < html.size() && html.substr(i, 3) == "]]>") { preamble += buffer + "]]>"; state = NORMAL; buffer = ""; i += 2; }
                 break;

            case IN_TAG:
               if (c == '>') {
                  string tag_content = buffer;
                  bool is_self_closing_by_slash = !tag_content.empty() && tag_content.back() == '/';
                  if (is_self_closing_by_slash) tag_content.pop_back();

                  size_t space_pos = tag_content.find(' ');
                  string tag_name = tag_content.substr(0, space_pos);
                  string attr_part = (space_pos != string::npos) ? tag_content.substr(space_pos + 1) : "";
                  bool is_void = find(voidTags.begin(), voidTags.end(), tag_name) != voidTags.end();

                  HTML_* node_to_process = nullptr;
                  if (!rootSet) {
                     rootSet = true;
                     node_to_process = this;
                     current = this;
                  } else {
                     HTML_* child = new HTML_();
                     child->parent = current;
                     if (current) current->children.push_back(child);
                     node_to_process = child;
                  }
                  
                  node_to_process->tag = tag_name;
                  node_to_process->attrs = parseAttrs(attr_part);
                  node_to_process->self_closing = is_self_closing_by_slash || is_void;

                  if (!node_to_process->self_closing) {
                     if(tag_name == "pre" || tag_name == "code") {
                        state = IN_RAW_TEXT;
                        raw_tag_to_close = tag_name;
                     }
                     stack.push_back(node_to_process);
                     current = node_to_process;
                  }
                  
                  state = (state == IN_TAG) ? NORMAL : state;
                  buffer = "";
               } else {
                  buffer += c;
               }
               break;

            case IN_CLOSE_TAG:
               if (c == '>') {
                  string closeTag = buffer;
                  closeTag.erase(closeTag.find_last_not_of(" \t\n\r") + 1);
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
      create_text_node(text_buffer);
   }
};
