#include "myconverters.h"
#include<string>
#include<sstream>
#include<vector>
#include<stdlib.h>

/*int atoi(const char* str) {
    int num = 0;
    char digit;
    while ((digit = *str++) != '\0') {
        if (digit < '0' || digit > '9') {
            return num; // No valid conversion possible 
        }
        num *= 10;
        num += digit - '0';
    }
    return num;
}*/

/*float atof(const char* str) {
    float num = 0;
    char digit;
    bool f = false;
    int dc = 0;
    while ((digit = *str++) != '\0') {
        if (digit < '0' || digit > '9') {
            if (digit == '.' && !f) {
                f = true;
            } else {
                return num; // No valid conversion possible 
            }
        } else if (digit != '0') {

        }
        if (!f) {
            num *= 10;
            num += digit - '0';
        } else {
            dc++;
            num += (digit - '0') / (10^dc);
        }
    }
    return num;
}*/

char* itoa(int i, int size) {
    std::stringstream ss;
    std::string out;
    ss << i;
    ss >> out;
    if (size > 0) {
        int sl = out.length();
        int additionalZeroes = size - sl;
        while (additionalZeroes > 0) {
            out = "0" + out;
            additionalZeroes--;
        }
    }
    return (char*) out.c_str();
}

std::string implode(const std::string glue, const std::vector<std::string> &pieces) {
    std::string a;
    int leng = pieces.size();
    for (int i = 0; i < leng; i++) {
        a += pieces[i];
        if (i < (leng - 1))
            a += glue;
    }
    return a;
}

std::vector<std::string> explode(const std::string delimiter, const std::string &str) {
    std::vector<std::string> arr;

    int strleng = str.length();
    int delleng = delimiter.length();
    if (delleng == 0)
        return arr; //no change

    int i = 0;
    int k = 0;
    while (i < strleng) {
        int j = 0;
        while (i + j < strleng && j < delleng && str[i + j] == delimiter[j])
            j++;
        if (j == delleng)//found delimiter
        {
            arr.push_back(str.substr(k, i - k));
            i += delleng;
            k = i;
        } else {
            i++;
        }
    }
    arr.push_back(str.substr(k, i - k));
    return arr;
}

float timeToSec(std::string timestring) {
    float secs = 0;
    std::vector<std::string> t = explode(":", timestring);
    secs = atoi(t[0].c_str())*60 * 60 + atoi(t[1].c_str())*60 + atof(t[2].c_str());
    return secs;
}

std::string tolower(std::string s) {
    char* buf;
    buf = (char*) s.c_str();
    int i;
    for (int i = 0; i < s.length(); i++) {
        buf[i] = tolower(buf[i]);
    }
    return std::string(buf);
}
