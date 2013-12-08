/* 
 * File:   myconverters.h
 * Author: newmek7
 *
 * Created on 22 March, 2013, 11:52 AM
 */

#ifndef MYCONVERTERS_H
#define	MYCONVERTERS_H
#include <string>
#include <vector>

//int atoi(const char* str);
//float atof(const char* str);

char* itoa(int i, int size = 0);
std::string implode(const std::string glue, const std::vector<std::string> &pieces);
std::vector<std::string> explode(const std::string delimiter, const std::string &str);
float timeToSec(std::string timestring);
std::string tolower(std::string s);

#endif	/* MYCONVERTERS_H */

