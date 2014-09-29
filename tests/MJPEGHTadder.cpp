/* 
 * File:   MJPEGFilePatcher.cpp
 * Author: gowtham
 *
 * Created on 31 May, 2014, 2:41:17 PM
 */

#include <base/JPEGImage.h>
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
    std::cout << "MJPEGFilePatcher test 1" << std::endl;
}

void test2() {
    std::cout << "MJPEGFilePatcher test 2" << std::endl;
    std::cout << "%TEST_FAILED% time=0 testname=test2 (MJPEGFilePatcher) message=error message sample" << std::endl;
}

int main(int argc, char** argv) {
    //    std::cout << "%SUITE_STARTING% MJPEGFilePatcher" << std::endl;
    //    std::cout << "%SUITE_STARTED%" << std::endl;
    //
    //    std::cout << "%TEST_STARTED% test1 (MJPEGFilePatcher)" << std::endl;
    //    test1();
    //    std::cout << "%TEST_FINISHED% time=0 test1 (MJPEGFilePatcher)" << std::endl;
    //
    //    std::cout << "%TEST_STARTED% test2 (MJPEGFilePatcher)\n" << std::endl;
    //    test2();
    //    std::cout << "%TEST_FINISHED% time=0 test2 (MJPEGFilePatcher)" << std::endl;
    //
    //    std::cout << "%SUITE_FINISHED% time=0" << std::endl;

    std::ifstream infile;
    std::ofstream outfile;
    infile.open(argv[1], std::ios::binary | std::ios::in);
    if (infile.is_open()) {
        std::string fn(argv[1]);
        fn.resize(fn.size() - 4);
        outfile.open(fn + "_patched.jpg", std::ios::binary | std::ios::out);
        std::string image((std::istreambuf_iterator<char>(infile)),
                std::istreambuf_iterator<char>());
        JPEGImage myimage((char*) image.c_str(), image.size());
        char* patchedImage = myimage.huffmanPatchChar();
        outfile.write(patchedImage, myimage.patchedImageSize);
        //outfile << patchedImage;
        outfile.close();
        infile.close();
    }
    return (EXIT_SUCCESS);
}

