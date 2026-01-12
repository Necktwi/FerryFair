#ifndef HTMLPARSER_H
#define HTMLPARSER_H

#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

// Forward declarations
class HTML_;

enum HTMLType_ { NORML, TEXT, SELFCLOSING, POINTER };

union HTMLContent_ {
   std::vector<HTML_*> children;
   HTML_* pointer;
   HTMLContent_() {}
   ~HTMLContent_() {}
};

class HTML_ {
public:
   std::string tag;
   std::vector<std::pair<std::string, std::string>> attrs;
   HTMLContent_ content;
   HTMLType_ type;
   HTML_* parent;

   static HTML_ nullHtml;

   HTML_(const char* html_str);
   HTML_();
   ~HTML_();

   size_t stringify(std::string& out, const std::string& skipClass = "", const std::string& mustClass = "") const;
   size_t calculateLength(const std::string& skipList, const std::string& mustList) const;
   void stringifyHelper(std::string& out, const std::string& skipList, const std::string& mustList) const;
   
   std::vector<HTML_*> getElementsByClassName(const std::string& className);
   std::vector<HTML_*> getElementsByAnyClassName(const std::string& className);
   std::vector<HTML_*> getElementsByTagName(const std::string& tagName);
   HTML_& getElementById(const char* id);
   HTML_& insertAdjacentElement(const char* position, HTML_& elm);
   HTML_* cloneNode(bool deep = true) const;
   std::string getAttribute(const std::string& name) const;
   void setAttribute(const std::string& name, const std::string& value);
   void remove();
   bool hasAnyClass(const std::string& classes) const;
   void parse(const std::string& html);

private:
   bool hasSingleClass(const std::string& cls) const;
   bool hasClass(const std::string& cls) const;
   std::vector<std::pair<std::string, std::string>> parseAttrs(const std::string& attr_str);
};

#endif // HTMLPARSER_H
