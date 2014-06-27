/* 
 * File:   Base64Image.cpp
 * Author: Gowtham
 *
 * Created on Jun 16, 2014, 6:44:18 PM
 */

#include "mystdlib.h"
#include "JPEGImage.h"
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <memory>
#include <malloc.h>
#include <streambuf>

/*
 * Simple C++ Test Suite
 */

void test1() {
    std::cout << "Base64Image test 1" << std::endl;
}

void test2() {
    std::cout << "Base64Image test 2" << std::endl;
    std::cout << "%TEST_FAILED% time=0 testname=test2 (Base64Image) message=error message sample" << std::endl;
}

int main(int argc, char** argv) {
    //    std::cout << "%SUITE_STARTING% Base64Image" << std::endl;
    //    std::cout << "%SUITE_STARTED%" << std::endl;
    //
    //    std::cout << "%TEST_STARTED% test1 (Base64Image)" << std::endl;
    //    test1();
    //    std::cout << "%TEST_FINISHED% time=0 test1 (Base64Image)" << std::endl;
    //
    //    std::cout << "%TEST_STARTED% test2 (Base64Image)\n" << std::endl;
    //    test2();
    //    std::cout << "%TEST_FINISHED% time=0 test2 (Base64Image)" << std::endl;
    //
    //    std::cout << "%SUITE_FINISHED% time=0" << std::endl;
    std::ifstream infile;
    std::ofstream outfile;
    infile.open(argv[1], std::ios::binary | std::ios::in);
    if (infile.is_open()) {
        std::string fn(argv[1]);
        fn.resize(fn.size() - 4);
        outfile.open(fn + /*".b64jpg.txt"*/"b64HT.txt", std::ios::binary | std::ios::out);
        //std::string image((std::istreambuf_iterator<char>(infile)), std::istreambuf_iterator<char>());
//        JPEGImage myimage((char*) image.c_str(), image.size());
//        char* patchedImage = myimage.huffmanPatchChar();
        size_t s;
        char * b64_s = base64_encode((const unsigned char*) JPEGImage::StdHuffmanTable, 420, (size_t*) & s);
        //outfile.write("data:image/jpeg;base64,", 23);
        outfile.write(b64_s, s);
        //free(patchedImage);
        outfile.close();
        infile.close();
    }
    return (EXIT_SUCCESS);
}

