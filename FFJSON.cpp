/* 
 * File:   FFJSON.cpp
 * Author: raheem
 * 
 * Created on November 29, 2013, 4:29 PM
 */

#include <string>
#include <malloc.h>
#include <math.h>
#include <string.h>

#include "FFJSON.h"
#include "mystdlib.h"
#include "myconverters.h"

FFJSON::FFJSON() {
}

FFJSON::FFJSON(std::string ffjson) {
    trimWhites(ffjson);
    this->ffjson = ffjson;
    this->type = objectType(ffjson);
    if (this->type == FFJSON_OBJ_TYPE::OBJECT) {
        int i = 1;
        int l = ffjson.length();
        int j = 1;
        int elementStartPos = 0;
        std::string property;
        std::string value;
        int k = 0;
        bool propset = false;
        bool valset = false;
        bool array = false;
        bool object = false;
        bool ffescstr = false;
        bool success = false;
        int recursiveObjectCount = 0;
        int recursiveArrayCount = 0;
        this->val.pairs = new std::map<std::string, FFJSON*>();
        while (i < l) {
            if (!propset) {
                if (ffjson[i] == ':') {
                    property = ffjson.substr(j, i - j);
                    trimWhites(property);
                    propset = true;
                    j = i;
                } else if (ffjson[i] == ',' || ffjson[i] == '\\' || ffjson[i] == '.') {
                    throw Exception("unexpected character in property at " + std::string(itoa(i)));
                }
            } else {
                if (ffjson[i] == '\\' && !ffescstr) {
                    i++;
                } else if (ffjson[i] == '{' && !array&& !ffescstr) {
                    object = true;
                    recursiveObjectCount++;
                } else if (ffjson[i] == '}' && !array&& !ffescstr && object) {
                    recursiveObjectCount--;
                    if (recursiveObjectCount == 0) {
                        object = false;
                    }
                } else if (ffjson[i] == '[' && !object && !ffescstr) {
                    recursiveArrayCount++;
                    array = true;
                } else if (ffjson[i] == ']' && !object&& !ffescstr && array) {
                    recursiveArrayCount--;
                    if (recursiveArrayCount == 0)
                        array = false;
                } else if ((ffjson[i] == ',' || ffjson[i] == '}')&&!array&&!object && !ffescstr) {
                    FFJSON* f = new FFJSON(value);
                    propset = false;
                    j = i + 1;
                    (*this->val.pairs)[property] = f;
                    if (ffjson[i] == '}')success = true;
                    value = "";
                    valset = true;
                } else if (!array && ffjson[i] == 'F') {
                    if (ffjson[i + 1] == 'F') {
                        if (ffjson[i + 2] == 'E') {
                            if (ffjson[i + 3] == 'S') {
                                if (ffjson[i + 4] == 'C') {
                                    if (ffjson[i + 5] == 'S') {
                                        if (ffjson[i + 6] == 'T') {
                                            if (ffjson[i + 7] == 'R') {
                                                ffescstr = !ffescstr;
                                                i += 7;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (!valset) {
                    value += ffjson[i];
                } else {
                    valset = false;
                }
                k++;
            }
            i++;
        }
        if (!success) {
            throw Exception("Terminating character not found.");
        }
    } else if (this->type == FFJSON_OBJ_TYPE::ARRAY) {
        int i = 1;
        int l = ffjson.length();
        int j = 1;
        int elementStartPos = 0;
        std::string property;
        std::string value;
        int k = 0;
        bool propset = false;
        bool valset = false;
        bool array = false;
        bool object = false;
        bool ffescstr = false;
        int recursiveObjectCount = 0;
        int recursiveArrayCount = 0;
        int elementCount = 0;
        this->val.array = new std::vector<FFJSON*>();
        bool success = false;
        while (i < l) {
            if (ffjson[i] == '\\' && !ffescstr) {
                i++;
            } else if (ffjson[i] == '{' && !array&& !ffescstr) {
                object = true;
                recursiveObjectCount++;
            } else if (ffjson[i] == '}' && !array&& !ffescstr && object) {
                recursiveObjectCount--;
                if (recursiveObjectCount == 0) {
                    object = false;
                }
            } else if (ffjson[i] == '[' && !object && !ffescstr) {
                recursiveArrayCount++;
                array = true;
            } else if (ffjson[i] == ']' && !object&& !ffescstr && array) {
                recursiveArrayCount--;
                if (recursiveArrayCount == 0)
                    array = false;
            } else if ((ffjson[i] == ',' || ffjson[i] == ']')&&!array&&!object && !ffescstr) {
                FFJSON* f = new FFJSON(value);
                propset = false;
                j = i + 1;
                (*this->val.array)[elementCount] = f;
                elementCount++;
                if (ffjson[i] == ']')success = true;
                value = "";
                valset = true;
            } else if (!array && ffjson[i] == 'F') {
                if (ffjson[i + 1] == 'F') {
                    if (ffjson[i + 2] == 'E') {
                        if (ffjson[i + 3] == 'S') {
                            if (ffjson[i + 4] == 'C') {
                                if (ffjson[i + 5] == 'S') {
                                    if (ffjson[i + 6] == 'T') {
                                        if (ffjson[i + 7] == 'R') {
                                            ffescstr = !ffescstr;
                                            i += 7;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (!valset) {
                value += ffjson[i];
                k++;
            } else {
                valset = false;
                k = 0;
            }
        }
        if (!success) {
            throw Exception("Terminating character not found.");
        }
    } else if (this->type == FFJSON_OBJ_TYPE::STRING) {
        this->val.string = (char*) malloc(ffjson.length() - 1);
        memcpy(this->val.string, ffjson.c_str() + 1, ffjson.length() - 2);
        this->val.string[ffjson.length() - 2] = '\0';
        this->length = ffjson.length() - 2;
    } else if (this->type == FFJSON_OBJ_TYPE::BOOL) {
        this->val.boolean = ((int) ffjson.find("true", 0)) != -1 ? true : false;
    } else if (this->type == FFJSON_OBJ_TYPE::UNRECOGNIZED) {
        if (ffjson.length() > 20) {
            if ((int) ffjson.find("FFESCSTR", 0) == 0) {
                if ((int) ffjson.find("FFESCSTR", 8) == ffjson.length() - 8) {
                    this->val.string = (char*) malloc(ffjson.length() - 15);
                    memcpy(this->val.string, ffjson.c_str() + 8, ffjson.length() - 16);
                    this->val.string[ffjson.length() - 16] = '\0';
                    this->length = ffjson.length() - 16;
                    this->type = FFJSON_OBJ_TYPE::STRING;
                }
            }
        } else {
            this->val.number = atof(ffjson.c_str());
            this->type = FFJSON_OBJ_TYPE::NUMBER;
        }
    }
}

FFJSON::~FFJSON() {
    if (this->type == FFJSON_OBJ_TYPE::OBJECT) {
        std::map<std::string, FFJSON*>::iterator i;
        i = val.pairs->begin();
        while (i != val.pairs->end()) {
            delete i->second;
            i++;
        }
        val.pairs->clear();
        delete this->val.pairs;
    } else if (this->type == FFJSON_OBJ_TYPE::ARRAY) {
        delete this->val.array;
    } else if (this->type == FFJSON_OBJ_TYPE::STRING || this->type == FFJSON_OBJ_TYPE::UNRECOGNIZED) {
        free(this->val.string);
    }
}

void FFJSON::trimWhites(std::string& s) {
    int i = 0;
    int j = s.length() - 1;
    while (s[i] == ' ' || s[i] == '\n' || s[i] == '\t') {
        i++;
    }
    while (s[j] == ' ' || s[j] == '\n' || s[j] == '\t') {
        j--;
    }
    j++;
    s = s.substr(i, j - i);
}

FFJSON::FFJSON_OBJ_TYPE FFJSON::objectType(std::string ffjson) {
    if (ffjson[0] == '{' && ffjson[ffjson.length() - 1] == '}') {
        return FFJSON_OBJ_TYPE::OBJECT;
    } else if (ffjson[0] == '"' && ffjson[ffjson.length() - 1] == '"') {
        return FFJSON_OBJ_TYPE::STRING;
    } else if (ffjson[0] == '[' && ffjson[ffjson.length() - 1] == ']') {
        return FFJSON_OBJ_TYPE::ARRAY;
    } else {
        return FFJSON_OBJ_TYPE::UNRECOGNIZED;
    }
}

FFJSON& FFJSON::operator[](std::string prop) {
    if (this->type == FFJSON_OBJ_TYPE::OBJECT) {
        if ((*this->val.pairs)[prop] != NULL) {
            return *((*this->val.pairs)[prop]);
        } else {
            throw Exception("NULL");
        }
    } else {
        throw Exception("NON OBJECT TYPE");
    }
}

FFJSON& FFJSON::operator[](int index) {
    if (this->type == FFJSON_OBJ_TYPE::ARRAY) {
        if ((*this->val.array)[index] != NULL) {
            return *((*this->val.array)[index]);
        } else {
            throw Exception("NULL");
        }
    } else {
        throw Exception("NON ARRAY TYPE");
    }
};
