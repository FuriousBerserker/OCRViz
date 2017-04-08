#include <stdio.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <set>
#include <string>
#include "ocr-types.h"
#include "pin.H"
#include "viz-util.hpp"
#define DEBUG 1

using namespace ::std;

class ColorScheme {
   public:
    string color;
    string style;
    ColorScheme(string color, string style);
    ColorScheme();
    virtual ~ColorScheme();
    string toString();
};

class Node {
   public:
    enum Type { EDT, DB, EVENT, INTERNAL };

   public:
    intptr_t id;
    list<Node*> descent;
    Type type;

    Node(ocrGuid_t id, u32 depc, ocrGuid_t* depv, Type type);
    virtual ~Node();
};

class AccessRecord {
   public:
    ocrGuid_t edtGuid;
    ADDRINT ip;
    AccessRecord(ocrGuid_t& edtGuid, ADDRINT ip);
};

class BytePage {
   public:
    AccessRecord* write;
    list<AccessRecord*> read;
    bool hasWrite();
    bool hasRead();
    void update(AccessRecord* ar, bool isRead);
};

class DBPage {
   public:
    uintptr_t startAddress;
    u64 len;
    BytePage** bytePageArray;
    DBPage(uintptr_t addr, u64 len);
    void updateBytePages(AccessRecord* ar, uintptr_t addr, u64 len,
                         bool isRead);
    BytePage* getBytePage(uintptr_t addr);
    int compareAddr(uintptr_t addr);
};

class ThreadLocalStore {
   public:
    ocrGuid_t edtGuid;
    vector<DBPage*> acquiredDB;
    void initializeAcquiredDB(u32 dpec, ocrEdtDep_t* depv);
    void insertDB(ocrGuid_t& guid);
    DBPage* getDB(uintptr_t addr);

   private:
    bool searchDB(uintptr_t addr, DBPage** ptr, u64* offset);
};

map<intptr_t, Node*> computationGraph;

map<Node::Type, ColorScheme> colorSchemes;

//EDT acquired DB
map<intptr_t, DBPage*> dbMap;

vector<string> skippedLibraries;

PIN_LOCK pinLock;

TLS_KEY tlsKey;

string userCodeImg;

ColorScheme::ColorScheme(string color, string style)
    : color(color), style(style) {}

ColorScheme::ColorScheme() {}

ColorScheme::~ColorScheme() {}

string ColorScheme::toString() {
    return "[color=" + color + ", style=" + style + "]";
}

Node::Node(ocrGuid_t id, u32 depc, ocrGuid_t* depv, Node::Type type)
    : id(id.guid), type(type) {
    //    if (depv != NULL) {
    //        for (u32 i = 0; i < depc; i++) {
    //            ocrGuid_t dep = *(depv + i);
    //            if (computationGraph.find(dep.guid) == computationGraph.end())
    //            {
    //                computationGraph[dep.guid] =
    //                    new Node(dep, 0, NULL, Node::INTERNAL);
    //            }
    //            computationGraph[dep.guid]->descent.push_back(this);
    //        }
    //    }
}

Node::~Node() {}

AccessRecord::AccessRecord(ocrGuid_t& edtGuid, ADDRINT ip) : ip(ip) {
    this->edtGuid.guid = edtGuid.guid;
}

bool BytePage::hasWrite() {
    if (write) {
        return true;
    } else {
        return false;
    }
}

bool BytePage::hasRead() {
    if (!read.empty()) {
        return true;
    } else {
        return false;
    }
}

void BytePage::update(AccessRecord* ar, bool isRead) {
    if (isRead) {
        read.push_back(ar);
    } else {
        write = ar;
    }
}

DBPage::DBPage(uintptr_t addr, u64 len) : startAddress(addr), len(len) {
    bytePageArray = new BytePage*[len];
    memset(bytePageArray, 0, sizeof(uintptr_t) * len);
}

void DBPage::updateBytePages(AccessRecord* ar, uintptr_t addr, u64 len,
                             bool isRead) {
    uintptr_t offset = addr - startAddress;
    for (u64 i = 0; i < len; i++) {
        if (!bytePageArray[offset + i]) {
            bytePageArray[offset + i] = new BytePage();
        }
        bytePageArray[offset + i]->update(ar, isRead);
    }
}

BytePage* DBPage::getBytePage(uintptr_t addr) {
    return bytePageArray[addr - startAddress];
}

int DBPage::compareAddr(uintptr_t addr) {
    if (addr < startAddress) {
        return 1;
    } else if (addr < startAddress + len) {
        return 0;
    } else {
        return -1;
    }
}

bool compareDB(DBPage* db1, DBPage* db2) {
    return db1->startAddress < db2->startAddress;
}

bool ThreadLocalStore::searchDB(uintptr_t addr, DBPage** ptr, u64* offset) {
    int start = 0;
    int end = acquiredDB.size() - 1;
    while (start <= end) {
        int middle = (start + end) / 2;
        DBPage* dbPage = acquiredDB[middle];
        int res = dbPage->compareAddr(addr);
        if (res == -1) {
            start = middle + 1;
        } else if (res == 1) {
            end = middle - 1;
        } else {
            if (ptr) {
                *ptr = acquiredDB[middle];
            }
            return true;
        }
    }
    if (ptr) {
        *ptr = NULL;
    }
    if (offset) {
        *offset = start;
    }
    return false;
}

void ThreadLocalStore::initializeAcquiredDB(u32 depc, ocrEdtDep_t* depv) {
    acquiredDB.clear();
    acquiredDB.reserve(2 * depc);
    for (u32 i = 0; i < depc; i++) {
        if (depv[i].ptr) {
            acquiredDB.push_back(dbMap[depv[i].guid.guid]);
        }
    }
    sort(acquiredDB.begin(), acquiredDB.end(), compareDB);
}

void ThreadLocalStore::insertDB(ocrGuid_t& guid) {
    DBPage* dbPage = dbMap[guid.guid];
    u64 offset;
    searchDB(dbPage->startAddress, NULL, &offset);
    acquiredDB.insert(acquiredDB.begin() + offset, dbPage); 
}

DBPage* ThreadLocalStore::getDB(uintptr_t addr) {
    DBPage* dbPage;
    searchDB(addr, &dbPage, NULL);
    return dbPage;
}

int usage() {
    cout << "This tool visualizes the runtime dependency of OCR "
            "applications and outputs computation graph."
         << endl;
    return -1;
}

/**
 * Whether n2 is reachable from n1
 */
bool isReachable(ocrGuid_t& n1, ocrGuid_t& n2) {
    Node* node1 = computationGraph[n1.guid];
    Node* node2 = computationGraph[n2.guid];
    // TODO need to add assert
    bool result = false;
    set<Node*> accessedNodes;
    list<Node*> queue;
    queue.push_back(node1);
    while (!queue.empty()) {
        Node* current = queue.front();
        queue.pop_front();
        accessedNodes.insert(current);
        if (current == node2) {
            result = true;
            break;
        }
        for (list<Node *>::iterator di = current->descent.begin(),
                                    de = current->descent.end();
             di != de; di++) {
            Node* node = *di;
            if (accessedNodes.find(node) == accessedNodes.end()) {
                queue.push_back(node);
            }
        }
    }
    return result;
}

bool isSkippedLibrary(IMG img) {
    string imageName = IMG_Name(img);
    for (vector<string>::iterator li = skippedLibraries.begin(),
                                  le = skippedLibraries.end();
         li != le; li++) {
        if (isEndWith(imageName, *li)) {
            return true;
        }
    }
    return false;
}

bool isOCRLibrary(IMG img) {
    string ocrLibraryName = "libocr_x86.so";
    string imageName = IMG_Name(img);
    if (isEndWith(imageName, ocrLibraryName)) {
        return true;
    } else {
        return false;
    }
}

bool isUserCodeImg(IMG img) {
    string imageName = IMG_Name(img);
    if (imageName == userCodeImg) {
        return true;
    } else {
        return false;
    }
}

bool isIgnorableIns(INS ins) {
    if (INS_IsStackRead(ins) || INS_IsStackWrite(ins)) return true;

    // skip call, ret and JMP instructions
    if (INS_IsBranchOrCall(ins) || INS_IsRet(ins)) {
        return true;
    }

    return false;
}

// inline ocrGuid_t* getEdtGuid(THREADID tid) {
//    ThreadLocalStore* data =
//        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
//    return &data->edtGuid;
//}

inline void initializeTLS(ocrGuid_t& edtGuid, u32 depc, ocrEdtDep_t* depv,
                          THREADID tid) {
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    data->edtGuid.guid = edtGuid.guid;
    data->initializeAcquiredDB(depc, depv);
}
// void argsMainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
//#if DEBUG
//    cout << "argsMainEdt" << endl;
//#endif
//    ocrGuid_t* depIdv = new ocrGuid_t[depc];
//    for (uint32_t i = 0; i < depc; i++) {
//        depIdv[i] = depv[i].guid;
//    }
//    ocrGuid_t mainEdtGuid = {0};
//    Node* mainEdtNode = new Node(mainEdtGuid, depc, depIdv, Node::EDT);
//    computationGraph[mainEdtNode->id] = mainEdtNode;
//    delete[] depIdv;
//}

void afterEdtCreate(ocrGuid_t guid, ocrGuid_t templateGuid, u32 paramc,
                    u64* paramv, u32 depc, ocrGuid_t* depv, u16 properties,
                    ocrGuid_t outputEvent) {
#if DEBUG
    cout << "afterEdtCreate" << endl;
#endif
    if (depc >= 0xFFFFFFFE) {
        cerr << "error" << endl;
        exit(0);
    }
    THREADID threadid = PIN_ThreadId();
    Node* newEdtNode = new Node(guid, depc, depv, Node::EDT);
    PIN_GetLock(&pinLock, threadid);
    computationGraph[guid.guid] = newEdtNode;
    if (!isNullGuid(outputEvent)) {
        Node* outputEventNode = new Node(outputEvent, 0, NULL, Node::EVENT);
        computationGraph[outputEvent.guid] = outputEventNode;
        newEdtNode->descent.push_back(outputEventNode);
    }
    PIN_ReleaseLock(&pinLock);
#if DEBUG
    cout << "afterEdtCreate finish" << endl;
#endif
}

void afterDbCreate(ocrGuid_t guid, void* addr, u64 len, u16 flags,
                   ocrInDbAllocator_t allocator) {
#if DEBUG
    cout << "afterDbCreate" << endl;
#endif
    THREADID threadid = PIN_ThreadId();
    Node* newDbNode = new Node(guid, 0, NULL, Node::DB);
    DBPage* dbPage = new DBPage((uintptr_t)addr, len);
    PIN_GetLock(&pinLock, threadid);
    computationGraph[guid.guid] = newDbNode;
    dbMap[guid.guid] = dbPage;
    PIN_ReleaseLock(&pinLock);
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, threadid));
    data->insertDB(guid);
#if DEBUG
    cout << "afterDbCreate finish" << endl;
#endif
}

void afterEventCreate(ocrGuid_t guid, ocrEventTypes_t eventType,
                      u16 properties) {
#if DEBUG
    cout << "afterEventCreate" << endl;
#endif
    THREADID threadid = PIN_ThreadId();
    Node* newEventNode = new Node(guid, 0, NULL, Node::EVENT);
    PIN_GetLock(&pinLock, threadid);
    computationGraph[guid.guid] = newEventNode;
    PIN_ReleaseLock(&pinLock);
#if DEBUG
    cout << "afterEventCreate finish" << endl;
#endif
}

void afterAddDependence(ocrGuid_t source, ocrGuid_t destination, u32 slot,
                        ocrDbAccessMode_t mode) {
#if DEBUG
    cout << "afterAddDependence" << endl;
#endif
    // cout << source << "->" << destination << endl;
    if (!isNullGuid(source)) {
        PIN_GetLock(&pinLock, PIN_ThreadId());
        if (computationGraph.find(source.guid) == computationGraph.end()) {
            computationGraph[source.guid] =
                new Node(source, 0, NULL, Node::INTERNAL);
        }
        if (computationGraph.find(destination.guid) == computationGraph.end()) {
            computationGraph[destination.guid] =
                new Node(destination, 0, NULL, Node::INTERNAL);
        }
        computationGraph[source.guid]->descent.push_back(
            computationGraph[destination.guid]);
        PIN_ReleaseLock(&pinLock);
    }
#if DEBUG
    cout << "afterAddDependence finish" << endl;
#endif
}

void afterEventSatisfy(ocrGuid_t edtGuid, ocrGuid_t eventGuid,
                       ocrGuid_t dataGuid, u32 slot) {
#if DEBUG
    cout << "afterEventSatisfy" << endl;
#endif
    if (computationGraph.find(edtGuid.guid) == computationGraph.end()) {
        computationGraph[edtGuid.guid] =
            new Node(edtGuid, 0, NULL, Node::INTERNAL);
    }
    assert(computationGraph.find(eventGuid.guid) != computationGraph.end());
    assert(computationGraph.find(dataGuid.guid) != computationGraph.end());
    // assert(computationGraph.find(edtGuid.guid) !=
    // computationGraph.end());
    Node* edt = computationGraph[edtGuid.guid];
    Node* event = computationGraph[eventGuid.guid];
    //	Node* db = computationGraph[dataGuid.guid];
    //	db->descent.splice(db->descent.end(), event->descent);
    //	event->descent.push_back(db);
    //	edt->descent.push_back(db);
    edt->descent.push_back(event);
#if DEBUG
    cout << "afterEventSatisfy finish" << endl;
#endif
}

void preEdt(THREADID tid, ocrGuid_t edtGuid, u32 paramc, u64* paramv, u32 depc,
            ocrEdtDep_t* depv, u64* dbSizev) {
#if DEBUG
    cout << "preEdt" << endl;
#endif
    initializeTLS(edtGuid, depc, depv, tid);
//    for (u64 i = 0; i < depc; i++) {
//        if (depv[i].ptr) {
//            cout << "db: " << std::hex << depv[i].ptr << " " << std::dec <<
//            dbSizev[i] << endl;
//        }
//    }
#if DEBUG
    cout << "preEdt finish" << endl;
#endif
}

void CG2Dot() {
#if DEBUG
    cout << "CG2Dot" << endl;
#endif
    ofstream out;
    out.open("cg.dot");
    out << "digraph ComputationGraph {" << endl;
    cout << "total node num: " << computationGraph.size() << endl;
    for (map<intptr_t, Node *>::iterator ci = computationGraph.begin(),
                                         ce = computationGraph.end();
         ci != ce; ci++) {
        Node* node = ci->second;
        out << node->id << colorSchemes[node->type].toString() << ";" << endl;
    }
    for (map<intptr_t, Node *>::iterator ci = computationGraph.begin(),
                                         ce = computationGraph.end();
         ci != ce; ci++) {
        // cout << (uint64_t)ci->second << endl;
        Node* node = ci->second;
        for (list<Node *>::iterator ni = node->descent.begin(),
                                    ne = node->descent.end();
             ni != ne; ni++) {
            out << node->id << " -> " << (*ni)->id << ";" << endl;
            // cout << (uint64_t)node << "<-----" << (uint64_t)*ni
            // << endl;
        }
    }
    out << "}";
    out.close();
}

void fini() {
#if DEBUG
    cout << "fini" << endl;
#endif
    CG2Dot();
}

void overload(IMG img, void* v) {
#if DEBUG
    cout << "img: " << IMG_Name(img) << endl;
#endif

    if (isOCRLibrary(img)) {
        // monitor mainEdt
        //        RTN mainEdtRTN = RTN_FindByName(img, "mainEdt");
        //        if (RTN_Valid(mainEdtRTN)) {
        //#if DEBUG
        //            cout << "instrument mainEdt" << endl;
        //#endif
        //            RTN_Open(mainEdtRTN);
        //            RTN_InsertCall(mainEdtRTN, IPOINT_BEFORE,
        //            (AFUNPTR)argsMainEdt,
        //                           IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
        //                           IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
        //                           IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
        //                           IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
        //                           IARG_END);
        //            RTN_Close(mainEdtRTN);
        //        }

        // replace notifyEdtCreate
        RTN rtn = RTN_FindByName(img, "notifyEdtCreate");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyEdtCreate" << endl;
#endif
            PROTO proto_notifyEdtCreate = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyEdtCreate",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_AGGREGATE(ocrGuid_t),
                PIN_PARG(u32), PIN_PARG(u64*), PIN_PARG(u32),
                PIN_PARG(ocrGuid_t*), PIN_PARG(u16),
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(afterEdtCreate), IARG_PROTOTYPE,
                proto_notifyEdtCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE,
                5, IARG_FUNCARG_ENTRYPOINT_VALUE, 6,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 7, IARG_END);
            PROTO_Free(proto_notifyEdtCreate);
        }

        // replace notidyDbCreate
        rtn = RTN_FindByName(img, "notifyDbCreate");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyDbCreate" << endl;
#endif
            PROTO proto_notifyDbCreate = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyDbCreate",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG(void*), PIN_PARG(u64),
                PIN_PARG(u16), PIN_PARG_ENUM(ocrInDbAllocator_t),
                PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(afterDbCreate), IARG_PROTOTYPE,
                proto_notifyDbCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_END);
            PROTO_Free(proto_notifyDbCreate);
        }

        // replace notifyEventCreate
        rtn = RTN_FindByName(img, "notifyEventCreate");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyEventCreate" << endl;
#endif
            PROTO proto_notifyEventCreate = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyEventCreate",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_ENUM(ocrEventTypes_t),
                PIN_PARG(u16), PIN_PARG_END());
            RTN_ReplaceSignature(rtn, AFUNPTR(afterEventCreate), IARG_PROTOTYPE,
                                 proto_notifyEventCreate,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END);
            PROTO_Free(proto_notifyEventCreate);
        }

        // replace notifyAddDependence
        rtn = RTN_FindByName(img, "notifyAddDependence");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyAddDependence" << endl;
#endif
            PROTO proto_notifyAddDependence = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyAddDependence",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_AGGREGATE(ocrGuid_t),
                PIN_PARG(u32), PIN_PARG_ENUM(ocrDbAccessMode_t),
                PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(afterAddDependence), IARG_PROTOTYPE,
                proto_notifyAddDependence, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);
            PROTO_Free(proto_notifyAddDependence);
        }

        // replace notifyEventSatisfy
        rtn = RTN_FindByName(img, "notifyEventSatisfy");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyEventSatisfy" << endl;
#endif
            PROTO proto_notifyEventSatisfy = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyEventSatisfy",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_AGGREGATE(ocrGuid_t),
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG(u32), PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(afterEventSatisfy), IARG_PROTOTYPE,
                proto_notifyEventSatisfy, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);
            PROTO_Free(proto_notifyEventSatisfy);
        }

        // replace notifyShutdown
        rtn = RTN_FindByName(img, "notifyShutdown");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace notifyShutdown" << endl;
#endif
            PROTO proto_notifyShutdown =
                PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT,
                               "notifyShutdown", PIN_PARG_END());
            RTN_ReplaceSignature(rtn, AFUNPTR(fini), IARG_PROTOTYPE,
                                 proto_notifyShutdown, IARG_END);
            PROTO_Free(proto_notifyShutdown);
        }

        // replace notifyEdtStart
        rtn = RTN_FindByName(img, "notifyEdtStart");
        if (RTN_Valid(rtn)) {
#if DEBUG
            cout << "replace proto_notifyEdtStart" << endl;
#endif
            PROTO proto_notifyEdtStart = PROTO_Allocate(
                PIN_PARG(void), CALLINGSTD_DEFAULT, "notifyEdtStart",
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG(u32), PIN_PARG(u64*),
                PIN_PARG(u32), PIN_PARG(ocrEdtDep_t*), PIN_PARG(u64*),
                PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(preEdt), IARG_PROTOTYPE, proto_notifyEdtStart,
                IARG_THREAD_ID, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE,
                5, IARG_END);
            PROTO_Free(proto_notifyEdtStart);
        }
    }
}

void outputRaceInfo(ADDRINT ip1, bool ip1IsRead, ADDRINT ip2, bool ip2IsRead) {
    int32_t ip1Line, ip1Column, ip2Line, ip2Column;
    string ip1File, ip2File;
    string ip1Type, ip2Type;
    if (ip1IsRead) {
        ip1Type = "Read";
    } else {
        ip1Type = "Write";
    }
    if (ip2IsRead) {
        ip2Type = "Read";
    } else {
        ip2Type = "Write";
    }
    
    PIN_LockClient();
    PIN_GetSourceLocation(ip1, &ip1Column, &ip1Line, &ip1File);
    PIN_GetSourceLocation(ip2, &ip2Column, &ip2Line, &ip2File);
    PIN_UnlockClient();
    cout << ip1Type << "-" << ip2Type << " race detect!" << endl;
    cout << "first op is " << ip1 << " in " << ip1File << ": " << ip1Line << ": " << ip1Column << endl;
    cout << "second op is " << ip2 << " in " << ip2File << ": " << ip2Line << ": " << ip2Column << endl;
    abort();

}

void checkDataRace(ADDRINT ip, ocrGuid_t& guid, bool isRead, BytePage* bytePage) {
    if (isRead) {
        if (bytePage->hasWrite()) {
            bool mhp = !isReachable(bytePage->write->edtGuid, guid);
            if (mhp) {
                outputRaceInfo(bytePage->write->ip, false, ip, true);
            }
        }
    } else {
        if (bytePage->hasWrite()) {
            bool mhp = !isReachable(bytePage->write->edtGuid, guid);
            if (mhp) {
                outputRaceInfo(bytePage->write->ip, false, ip, false);
            }

        }
        if (bytePage->hasWrite()) {
            for (list<AccessRecord *>::iterator ai = bytePage->read.begin(),
                                                ae = bytePage->read.end();
                 ai != ae; ai++) {
                bool mhp = !isReachable((*ai)->edtGuid, guid);
                if (mhp) {
                    outputRaceInfo((*ai)->ip, true, ip, false);
                }

            }
        }
    }
}

void recordMemRead(void* addr, uint32_t size, ADDRINT sp, ADDRINT ip) {
    THREADID tid = PIN_ThreadId();    
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    DBPage* dbPage = data->getDB((uintptr_t)addr);
    if (dbPage) {
        for (uint32_t i = 0; i < size; i++) {
            BytePage* current = dbPage->getBytePage((uintptr_t)addr + i);
            if (current) {
                checkDataRace(ip, data->edtGuid, true, current);
            }
        }
        AccessRecord* ar = new AccessRecord(data->edtGuid, ip);
        dbPage->updateBytePages(ar, (uintptr_t)addr, size, true);
    }
}

void recordMemWrite(void* addr, uint32_t size, ADDRINT sp, ADDRINT ip) {
    THREADID tid = PIN_ThreadId();
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    DBPage* dbPage = data->getDB((uintptr_t)addr);
    if (dbPage) {
        for (uint32_t i = 0; i < size; i++) {
            BytePage* current = dbPage->getBytePage((uintptr_t)addr + i);
            if (current) {
                checkDataRace(ip, data->edtGuid, false, current);
            }
        }
        AccessRecord* ar = new AccessRecord(data->edtGuid, ip);
        dbPage->updateBytePages(ar, (uintptr_t)addr, size, false);
    }
}

void instrumentInstruction(INS ins) {
    if (isIgnorableIns(ins)) return;

    if (INS_IsAtomicUpdate(ins)) return;

    uint32_t memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (uint32_t memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp)) {
            INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)recordMemRead,
                                     IARG_MEMORYOP_EA, memOp,
                                     IARG_MEMORYREAD_SIZE, IARG_REG_VALUE,
                                     REG_STACK_PTR, IARG_INST_PTR, IARG_END);
        }
        // Note that in some architectures a single memory operand can be
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp)) {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)recordMemWrite, IARG_MEMORYOP_EA,
                memOp, IARG_MEMORYWRITE_SIZE, IARG_REG_VALUE, REG_STACK_PTR,
                IARG_INST_PTR, IARG_END);
        }
    }
}

void instrumentRoutine(RTN rtn) {
    RTN_Open(rtn);
    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        instrumentInstruction(ins);
    }
    RTN_Close(rtn);
}

void instrumentImage(IMG img, void* v) {
    if (isUserCodeImg(img)) {
        for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
            for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn);
                 rtn = RTN_Next(rtn)) {
                instrumentRoutine(rtn);
            }
        }
    }
}

void threadStart(THREADID tid, CONTEXT* ctxt, int32_t flags, void* v) {
    ThreadLocalStore* store = new ThreadLocalStore();
    PIN_SetThreadData(tlsKey, store, tid);
}

void threadFini(THREADID tid, const CONTEXT* ctxt, int32_t code, void* v) {
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    delete data;
    PIN_SetThreadData(tlsKey, NULL, tid);
}

void initColorScheme() {
    ColorScheme a("green", "filled"), b("yellow", "filled"),
        c("blue", "filled"), d("gray", "filled");
    colorSchemes[Node::EDT] = a;
    colorSchemes[Node::DB] = b;
    colorSchemes[Node::EVENT] = c;
    colorSchemes[Node::INTERNAL] = d;
}

void initSkippedLibrary() {
    skippedLibraries.push_back("ld-linux-x86-64.so.2");
    skippedLibraries.push_back("libpthread.so.0");
    skippedLibraries.push_back("libc.so.6");
    skippedLibraries.push_back("libocr_x86.so");
}

void init() {
    initSkippedLibrary();
    initColorScheme();
}

int main(int argc, char* argv[]) {
    PIN_InitLock(&pinLock);
    tlsKey = PIN_CreateThreadDataKey(0);
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) {
        return usage();
    }
    userCodeImg = argv[argc - 1];

    IMG_AddInstrumentFunction(overload, 0);
    IMG_AddInstrumentFunction(instrumentImage, 0);
    PIN_AddThreadStartFunction(threadStart, 0);
    PIN_AddThreadFiniFunction(threadFini, 0);
    // PIN_AddFiniFunction(fini, 0);
    init();
    PIN_StartProgram();
    return 0;
}
