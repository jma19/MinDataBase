#include <cmath>
#include <cstdlib>
#include <iostream>
#include "rbfm.h"

RecordBasedFileManager *RecordBasedFileManager::_rbf_manager = 0;

RecordBasedFileManager *RecordBasedFileManager::instance() {
    if (!_rbf_manager)
        _rbf_manager = new RecordBasedFileManager();

    return _rbf_manager;
}

RecordBasedFileManager::RecordBasedFileManager() {

}

RecordBasedFileManager::~RecordBasedFileManager() {
}

RC RecordBasedFileManager::createFile(const string &fileName) {
    PagedFileManager *pagedFileManager = PagedFileManager::instance();
    return pagedFileManager->createFile(fileName);
}

RC RecordBasedFileManager::destroyFile(const string &fileName) {
    PagedFileManager *pagedFileManager = PagedFileManager::instance();
    return pagedFileManager->destroyFile(fileName);
}

RC RecordBasedFileManager::openFile(const string &fileName, FileHandle &fileHandle) {
    PagedFileManager *pagedFileManager = PagedFileManager::instance();
    return pagedFileManager->openFile(fileName, fileHandle);
}

RC RecordBasedFileManager::closeFile(FileHandle &fileHandle) {
    PagedFileManager *pManager = PagedFileManager::instance();
    return pManager->closeFile(fileHandle);
}

//page format: List<Record> + List<Slots> + SlotNumber + FreeSpaceSize
RC RecordBasedFileManager::insertRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor,
                                        const void *data, RID &rid) {

    //usually, we just insert record to current page, if the page is not enough to store the data, we create a new page to store it

    //1. compute the record length, and get the formatted recorded
    void *record = malloc(PAGE_SIZE);
    int recordLen = getRecordLenAndFormatRecord(recordDescriptor, data, record);
    void *newRecord = malloc(recordLen);
    memcpy(newRecord, record, recordLen);

    //2. get current page
    unsigned int currPageNumber = fileHandle.getNumberOfPages();
    void *currPage = malloc(PAGE_SIZE);
    fileHandle.readPage(currPageNumber - 1, currPage);

    //3. check whether current page is able to store this record
    int costOfRecord = recordLen + SLOT_RECORD_OFFSET + SLOT_RECORD_OFFSET;

    if (costOfRecord > PAGE_SIZE - SLOTS_NUMBER - SLOT_FREE_SPACE_LENGTH) {
        return TOO_LARGER_RECORD;
    }

    short slotNum = *(short *) ((char *) currPage + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER);
    short freeSpace = *(short *) ((char *) currPage + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH);

    //has enough space, then write data into current page
    if (freeSpace > costOfRecord) {
        short startOfFreeSpace = *(short *) ((char *) currPage + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER -
                                             (SLOT_RECORD_LENGTH + SLOT_RECORD_OFFSET) * slotNum - freeSpace);

        memcpy((char *) currPage + startOfFreeSpace, (char *) newRecord, recordLen);
        //update free space
        freeSpace = freeSpace - costOfRecord;
        updateSlotFreeSpace(currPage, freeSpace);
        //update slot number
        updateSlotNumber(currPage, slotNum + 1);
        //add slot for new record in slot directory
        updateSlot(currPage, slotNum + 1, startOfFreeSpace, recordLen);

        rid.pageNum = currPageNumber - 1;
        rid.slotNum = slotNum;
    } else {
        //otherwise create a new page to write data
        fileHandle.appendPage(newRecord);
        updateSlotFreeSpace((char *) currPage + PAGE_SIZE,
                            PAGE_SIZE - costOfRecord - SLOTS_NUMBER - SLOT_FREE_SPACE_LENGTH);
        updateSlotNumber((char *) currPage + PAGE_SIZE, 1);
        updateSlot((char *) currPage + PAGE_SIZE, 1, 0, recordLen);
        rid.pageNum = currPageNumber;
        rid.slotNum = 0;
    }
    free(record);
    free(newRecord);
    return SUCCESS;
}

/**
 *
 * @param data pointer to the start of current pages
 * @param value
 */

void RecordBasedFileManager::updateSlotFreeSpace(void *data, short value) {
    *(short *) ((char *) data + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH) = value;
}

void RecordBasedFileManager::updateSlotNumber(void *data, short value) {
    *(short *) ((char *) data + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER) = value;
}

void RecordBasedFileManager::updateSlot(void *data, short slotNum, short recordOffset, short recordLen) {
    *(short *) ((char *) data + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER -
                slotNum * (SLOT_RECORD_LENGTH + SLOT_RECORD_OFFSET))
            = recordOffset;

    *((short *) ((char *) data + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER -
                 slotNum * (SLOT_RECORD_LENGTH + SLOT_RECORD_OFFSET) + SLOT_RECORD_OFFSET))
            = recordLen;
}

/**
 * format the stored record. and return the size of the data
 * @param recordDescriptor ----scheme
 * @param data -----input record data
 * @param newRecord
 * @return the length of the record in stored format: sizeOfIndicator + field offset + real data
 */
int RecordBasedFileManager::getRecordLenAndFormatRecord(const vector<Attribute> &recordDescriptor, const void *data,
                                                        void *newRecord) {
    //use a short space to store the null indicator
    short recordLen = 0;
    int numOfFields = recordDescriptor.size();
    recordLen += numOfFields * FIELD_OFFSET_LENGTH;

    //get the nulls Indicator length
    int nullsIndicatorLength = getByteForNullsIndicator(numOfFields);
    recordLen += nullsIndicatorLength;
    const char *pFlag = (const char *) data;         // pointer to null flags
    const char *pData = pFlag + nullsIndicatorLength;  // pointer to actual field data

    unsigned short *fieldOffset = new unsigned short[numOfFields + 1];

    uint8_t flagMask = 0x80;     // cannot use (signed) byte

    //actual data start offset
    int headerLen = numOfFields * FIELD_OFFSET_LENGTH + nullsIndicatorLength;

    fieldOffset[0] = headerLen;
    for (int i = 1; i <= numOfFields; i++) {
        if (!(flagMask & (*pFlag))) {//not null pointer field
            switch (recordDescriptor[i - 1].type) {
                case TypeInt:
                case TypeReal: {
                    recordLen += sizeof(TypeReal);
                    pData += sizeof(TypeReal);
                    fieldOffset[i] = fieldOffset[i - 1] + sizeof(TypeReal);
                    break;
                }
                case TypeVarChar: {
                    int length = *((int *) pData); //pData first 32 bit value;
                    recordLen += length;
                    pData += sizeof(int) + length;
                    fieldOffset[i] = fieldOffset[i - 1] + VARCHAR_OFFSET_LENGTH + length;
                    break;
                }
            }
        } else {
            fieldOffset[i] = fieldOffset[i - 1];
        }
        if (flagMask == 0x01) {
            flagMask = 0x80;
            pFlag += 1;
        } else {
            flagMask = flagMask >> 1;
        }
    }
    //copy null indicator to new record
    memcpy((char *) newRecord, (char *) data, nullsIndicatorLength);
    for (int i = 0; i < numOfFields; i++) {
        *((short *) ((char *) newRecord + nullsIndicatorLength) + i) = fieldOffset[i];
    }

    memcpy((char *) newRecord + headerLen,
           (char *) data + nullsIndicatorLength, recordLen - headerLen);

    return recordLen;
}

RC RecordBasedFileManager::readRecord(FileHandle &fileHandle, const vector<Attribute> &recordDescriptor,
                                      const RID &rid,
                                      void *data) {

    void *page = malloc(PAGE_SIZE);
    RC res = fileHandle.readPage(rid.pageNum, page);
    if (res != SUCCESS) {
        return FAIL;
    }
    short slotNum = *(short *) ((char *) page + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER);
    if (slotNum < rid.slotNum) {
        return FAIL;
    }
    //slot number start from 0
    short recordOffset = *(short *) ((char *) page + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER -
                                     (rid.slotNum + 1) * SLOT_SIZE);

    short recordLen = *(short *) ((char *) page + PAGE_SIZE - SLOT_FREE_SPACE_LENGTH - SLOTS_NUMBER -
                                  (rid.slotNum + 1) * SLOT_SIZE + SLOT_RECORD_OFFSET);

    void *record = malloc(recordLen);

    memcpy(record, (char *) page + recordOffset, recordLen);
    int nullIndicatorSize = getByteForNullsIndicator(recordDescriptor.size());
    //copy indicator to data
    memcpy(data, record, nullIndicatorSize);
    memcpy((char *) data + nullIndicatorSize,
           (char *) record + nullIndicatorSize + recordDescriptor.size() * FIELD_OFFSET_LENGTH,
           recordLen - nullIndicatorSize - recordDescriptor.size() * FIELD_OFFSET_LENGTH);
    free(record);
    return 1;
}

RC RecordBasedFileManager::printRecord(const vector<Attribute> &recordDescriptor, const void *data) {

    int nullIndicatorSize = getByteForNullsIndicator(recordDescriptor.size());
    //display the data
    char *pFlag = (char *) data;
    char *record = (char *) data;
    int offset = nullIndicatorSize;
    uint8_t flagMask = 0x80;     // cannot use (signed) byte
    int varCharLen = 0;
    for (int i = 0; i < recordDescriptor.size(); i++) {
        cout << recordDescriptor[i].name << ":";
        if (!(flagMask & (*pFlag))) {//not null pointer field
            switch (recordDescriptor[i].type) {
                case TypeInt:
                    cout << *(int *) (record + offset) << endl;
                    offset += 4;
                    break;
                case TypeReal: {
                    cout << *(float *) (record + offset) << endl;
                    offset += 4;
                    break;
                }
                case TypeVarChar: {
                    varCharLen = *(int *) (record + offset);
                    offset += sizeof(int);
                    for (int j = 0; j < varCharLen; j++) {
                        printf("%c", *(record + offset + j));
                    }
                    cout << " " << endl;
                    offset += varCharLen;
                    break;
                }
            }
        } else {
            cout << "NULL" << endl;
        }
        if (flagMask == 0x01) {
            flagMask = 0x08;
            pFlag += 1;
        } else {
            flagMask = flagMask >> 1;
        }
    }
    return 1;
}

// Calculate actual bytes for nulls-indicator for the given field counts
int RecordBasedFileManager::getByteForNullsIndicator(int fieldCount) {
    return ceil((double) fieldCount / CHAR_BIT);
}
