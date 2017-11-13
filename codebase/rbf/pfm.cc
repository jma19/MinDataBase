#include "pfm.h"
#include <iostream>
#include <cmath>
#include <stdio.h>

PagedFileManager *PagedFileManager::_pf_manager = 0;

PagedFileManager *PagedFileManager::instance() {
    if (!_pf_manager)
        _pf_manager = new PagedFileManager();

    return _pf_manager;
}


PagedFileManager::PagedFileManager() {

}


PagedFileManager::~PagedFileManager() {
}


RC PagedFileManager::createFile(const string &fileName) {
    FILE *pFile;
    pFile = fopen(fileName.c_str(), "r");
    if (pFile) {
        fclose(pFile);
        return FAIL;
    }
    pFile = fopen(fileName.c_str(), "w");
    if (!pFile) {
        return FAIL;
    }
    return SUCCESS;
}

RC PagedFileManager::destroyFile(const string &fileName) {
    return remove(fileName.c_str()) == 0 ? SUCCESS : FAIL;
}

RC PagedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
    FILE *pFile;
    pFile = fopen(fileName.c_str(), "r+");
    return fileHandle.loadFile(pFile);
}


RC PagedFileManager::closeFile(FileHandle &fileHandle) {
    return fileHandle.closeFile();
}


FileHandle::FileHandle() {
    readPageCounter = 0;
    writePageCounter = 0;
    appendPageCounter = 0;
}


FileHandle::~FileHandle() {
}


RC FileHandle::readPage(PageNum pageNum, void *data) {
    if (!isOpen()) {
        cout << "page file is closed..." << endl;
        return FAIL;
    }
    if (pageNum > getNumberOfPages() - 1) {
        return PAGE_NUMBER_TOO_LARGE;
    }

    if (fseek(_file, PAGE_SIZE * pageNum, SEEK_SET) != 0) {
        return FSEEK_FAIL;
    }
    if (fread(data, PAGE_SIZE, 1, _file) != 1) {
        return FREAD_FAIL;
    }
    readPageCounter++;
    //Set position of stream to the beginning
    rewind(_file);
    return SUCCESS;
}


RC FileHandle::writePage(PageNum pageNum, const void *data) {
    if (!isOpen()) {
        cout << "page file is closed..." << endl;
        return FAIL;
    }
    if (pageNum > getNumberOfPages()) {
        return PAGE_NUMBER_TOO_LARGE;
    }
    //Sets the position indicator associated with the stream to a new position
    int rc = fseek(_file, PAGE_SIZE * pageNum, SEEK_SET);

    if (rc != 0) return FSEEK_FAIL;

    //Writes an array of count elements, each one with a size of size bytes,
    // from the block of memory pointed by ptr to the current position in the stream.
    if (fwrite(data, PAGE_SIZE, 1, _file) != 1) {
        return FWRITE_FAIL;
    }
    //Set position of stream to the beginning
    rewind(_file);
    writePageCounter += 1;
    fflush(_file);
    return SUCCESS;
}


RC FileHandle::appendPage(const void *data) {
    if (!isOpen()) {
        cout << "page file is closed..." << endl;
        return FAIL;
    }
    PageNum pageNum = getNumberOfPages();
    RC res = writePage(pageNum, data);
    if (res == SUCCESS) {
        appendPageCounter++;
    }
    return res;
}

unsigned FileHandle::getNumberOfPages() {
    fseek(_file, 0, SEEK_END);
    int pageCount = ceil(ftell(_file) / PAGE_SIZE);
    return pageCount;
}

RC FileHandle::collectCounterValues(unsigned &readPageCount, unsigned &writePageCount, unsigned &appendPageCount) {
    readPageCount = readPageCounter;
    writePageCount = writePageCounter;
    appendPageCount = appendPageCounter;
    return SUCCESS;
}

RC FileHandle::loadFile(FILE *file) {
    if (!file) {
        return FAIL;
    }
    _file = file;
    return SUCCESS;
}

RC FileHandle::closeFile() {
    if (!_file) {
        cout << "_file is NULL" << endl;
        return FAIL;
    }
    return fclose(_file) == 0 ?
           SUCCESS : FAIL;

}

bool FileHandle::isOpen() {
    return _file != NULL;
}
