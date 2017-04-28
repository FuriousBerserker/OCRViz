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
#define DEBUG 0

#define START_EPOCH 0
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
    enum EdgeType { SPAWN, CONTINUE, JOIN };

   public:
    intptr_t id;
    list<Node*> children;
    Type type;

    Node(intptr_t id, Type type);
    virtual ~Node();
    void addChild(Node* node);
};

class EDTNode : public Node {
   public:
    vector<Node*> spawnEdges;
    EDTNode(intptr_t id);
    virtual ~EDTNode();
    void addSpawnEdges(Node* node);
    Node* getSpawnEdge(u16 epoch);
};

class DBNode : public Node {
   public:
    u16 accessMode;
    DBNode(intptr_t id, u16 accessMode);
    virtual ~DBNode();
};

class EventNode : public Node {
   public:
    EventNode(intptr_t id);
    virtual ~EventNode();
};

struct NodeKey {
    intptr_t guid;
};

class KeyComparator {
   public:
    bool operator()(const NodeKey& key1, const NodeKey& key2) const;
};

class AccessRecord {
   public:
    NodeKey edtKey;
    u16 epoch;
    ADDRINT ip;
    AccessRecord(NodeKey& nodeKey, u16 epoch, ADDRINT ip);
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
    u64 length;
    BytePage** bytePageArray;
    DBPage(uintptr_t addr, u64 len);
    void updateBytePages(AccessRecord* ar, uintptr_t addr, u64 len,
                         bool isRead);
    BytePage* getBytePage(uintptr_t addr);
    int compareAddr(uintptr_t addr);
};

class ThreadLocalStore {
   public:
    EDTNode* currentEdt;
    u16 epoch;
    vector<DBPage*> acquiredDB;
    void initializeAcquiredDB(u32 dpec, ocrEdtDep_t* depv);
    void insertDB(ocrGuid_t& guid);
    DBPage* getDB(uintptr_t addr);

   private:
    bool searchDB(uintptr_t addr, DBPage** ptr, u64* offset);
};

// CG
map<NodeKey, Node*, KeyComparator> computationGraph;

// node color scheme binding
map<Node::Type, ColorScheme> nodeColorSchemes;

// edge color scheme binding
map<Node::EdgeType, ColorScheme> edgeColorSchemes;

// EDT acquired DB
map<intptr_t, DBPage*> dbMap;

// library list
vector<string> skippedLibraries;

// user code image name
string userCodeImg;

// pin obj
PIN_LOCK pinLock;

TLS_KEY tlsKey;

ColorScheme::ColorScheme(string color, string style)
    : color(color), style(style) {}

ColorScheme::ColorScheme() {}

ColorScheme::~ColorScheme() {}

string ColorScheme::toString() {
    return "[color=" + color + ", style=" + style + "]";
}

Node::Node(intptr_t id, Node::Type type) : id(id), type(type) {}

Node::~Node() {}

inline void Node::addChild(Node* node) { children.push_back(node); }

EDTNode::EDTNode(intptr_t id) : Node(id, Node::EDT) {}

EDTNode::~EDTNode() {}

inline void EDTNode::addSpawnEdges(Node* node) { spawnEdges.push_back(node); }

inline Node* EDTNode::getSpawnEdge(u16 epoch) {
    assert(spawnEdges.size() > epoch);
    return spawnEdges[epoch];
}

DBNode::DBNode(intptr_t id, u16 accessMode)
    : Node(id, Node::DB), accessMode(accessMode) {}

DBNode::~DBNode() {}

EventNode::EventNode(intptr_t id) : Node(id, Node::EVENT) {}

EventNode::~EventNode() {}

bool KeyComparator::operator()(const NodeKey& key1, const NodeKey& key2) const {
    return key1.guid < key2.guid ? true : false;
}

AccessRecord::AccessRecord(NodeKey& nodeKey, u16 epoch, ADDRINT ip)
    : epoch(START_EPOCH), ip(ip) {
    this->edtKey.guid = nodeKey.guid;
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

DBPage::DBPage(uintptr_t addr, u64 len) : startAddress(addr), length(len) {
    bytePageArray = new BytePage*[len];
    memset(bytePageArray, 0, sizeof(uintptr_t) * len);
}

void DBPage::updateBytePages(AccessRecord* ar, uintptr_t addr, u64 len,
                             bool isRead) {
    assert(addr >= startAddress && addr + len <= startAddress + length);
    uintptr_t offset = addr - startAddress;
    for (u64 i = 0; i < len; i++) {
        if (!bytePageArray[offset + i]) {
            bytePageArray[offset + i] = new BytePage();
        }
        bytePageArray[offset + i]->update(ar, isRead);
    }
}

BytePage* DBPage::getBytePage(uintptr_t addr) {
    assert(addr >= startAddress && addr < startAddress + length);
    return bytePageArray[addr - startAddress];
}

int DBPage::compareAddr(uintptr_t addr) {
    if (addr < startAddress) {
        return 1;
    } else if (addr < startAddress + length) {
        return 0;
    } else {
        return -1;
    }
}

bool compareDB(DBPage* db1, DBPage* db2) {
    return db1->startAddress < db2->startAddress;
}

bool ThreadLocalStore::searchDB(uintptr_t addr, DBPage** ptr, u64* offset) {
#if DEBUG
    cout << "search DB\n";
#endif
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
#if DEBUG
            cout << "search DB finish, true\n";
#endif
            return true;
        }
    }
    if (ptr) {
        *ptr = NULL;
    }
    if (offset) {
        *offset = start;
    }
#if DEBUG
    cout << "search DB finish, false\n";
#endif
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
    cout << "This tool detects data race i OCR program" << endl;
    return -1;
}

/**
 * Whether n2 is reachable from n1
 */
bool isReachable(NodeKey& n1, u16 epoch1, NodeKey& n2) {
    Node* node1 = computationGraph[n1];
    Node* node2 = computationGraph[n2];
    assert(node1->type == Node::EDT && node2->type == Node::EDT);
    bool result = false;
    set<Node*> accessedNodes;
    list<Node*> queue;

    if (n1.guid == n2.guid) {
        return true;
    }

    // add all spawn edge and continue edge
    EDTNode* edtNode1 = static_cast<EDTNode*>(node1);
    for (u16 i = epoch1; i < edtNode1->spawnEdges.size(); i++) {
        queue.push_back(edtNode1->spawnEdges[i]);
    }

    for (list<Node *>::iterator di = edtNode1->children.begin(),
                                de = edtNode1->children.end();
         di != de; di++) {
        queue.push_back(*di);
    }

    while (!queue.empty()) {
        Node* current = queue.front();
        queue.pop_front();
        accessedNodes.insert(current);
        if (current == node2) {
            result = true;
            break;
        } else if (current->Node::EDT) {
            EDTNode* currentEdt = static_cast<EDTNode*>(current);
            for (vector<Node *>::iterator si = currentEdt->spawnEdges.begin(),
                                          se = currentEdt->spawnEdges.end();
                 si != se; si++) {
                Node* spawnedNode = *si;
                if (accessedNodes.find(spawnedNode) == accessedNodes.end()) {
                    queue.push_back(spawnedNode);
                }
            }
        }
        for (list<Node *>::iterator di = current->children.begin(),
                                    de = current->children.end();
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
    NodeKey edtKey = {edtGuid.guid};
    data->currentEdt = static_cast<EDTNode*>(computationGraph[edtKey]);
    data->epoch = START_EPOCH;
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
                    ocrGuid_t outputEvent, ocrGuid_t parent) {
#if DEBUG
    cout << "afterEdtCreate" << endl;
#endif
    if (depc >= 0xFFFFFFFE) {
        cerr << "error" << endl;
        exit(0);
    }
    THREADID threadid = PIN_ThreadId();
    EDTNode* newEdtNode = new EDTNode(guid.guid);
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, threadid));

    NodeKey edtKey = {guid.guid};
    assert(computationGraph.find(edtKey) == computationGraph.end());
    PIN_GetLock(&pinLock, threadid);
    computationGraph[edtKey] = newEdtNode;
    if (!isNullGuid(outputEvent)) {
        EventNode* outputEventNode = new EventNode(outputEvent.guid);
        NodeKey eventKey = {outputEvent.guid};
        computationGraph[eventKey] = outputEventNode;
        newEdtNode->addChild(outputEventNode);
    }
    PIN_ReleaseLock(&pinLock);

    // add spawn edge & increase parent's epoch
    if (!isNullGuid(parent)) {
        data->currentEdt->addSpawnEdges(newEdtNode);
        data->epoch++;
    }

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
    DBNode* newDbNode = new DBNode(guid.guid, flags);
    DBPage* dbPage = new DBPage((uintptr_t)addr, len);
    NodeKey dbKey = {guid.guid};
    assert(computationGraph.find(dbKey) == computationGraph.end());
    PIN_GetLock(&pinLock, threadid);
    computationGraph[dbKey] = newDbNode;
    dbMap[guid.guid] = dbPage;
    PIN_ReleaseLock(&pinLock);

    // new created DB is acquired by current EDT instantly
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
    EventNode* newEventNode = new EventNode(guid.guid);
    NodeKey eventKey = {guid.guid};

    // only for debug
    if (computationGraph.find(eventKey) != computationGraph.end()) {
        cout << guid.guid << "  " << computationGraph[eventKey]->id << "  "
             << computationGraph[eventKey]->type << endl;
    }

    assert(computationGraph.find(eventKey) == computationGraph.end());
    PIN_GetLock(&pinLock, threadid);
    computationGraph[eventKey] = newEventNode;
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
    //    cout << source.guid << "->" << destination.guid << endl;
    NodeKey srcKey = {source.guid};
    NodeKey dstKey = {destination.guid};
    assert(computationGraph.find(dstKey) != computationGraph.end());

    // only for debug
    //    if (computationGraph.find(dstKey) == computationGraph.end()) {
    //        for (map<NodeKey, Node*>::iterator mi = computationGraph.begin(),
    //        me = computationGraph.end(); mi != me; mi++) {
    //            cout << "id is " << mi->second->id << " " << mi->second->type
    //            << endl;
    //        }
    //        computationGraph[dstKey] = new Node(destination.guid,
    //        Node::INTERNAL);
    //    }

    if (!isNullGuid(source)) {
        PIN_GetLock(&pinLock, PIN_ThreadId());
        //        if (computationGraph.find(srcKey) == computationGraph.end()) {
        //            computationGraph[srcKey] =
        //                new Node(source, 0, NULL, Node::INTERNAL);
        //        }
        //        if (computationGraph.find(dstKey) == computationGraph.end()) {
        //            computationGraph[dstKey] =
        //                new Node(destination, 0, NULL, Node::INTERNAL);
        //        }
        computationGraph[srcKey]->addChild(computationGraph[dstKey]);
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
    // According to spec, event satisfied after EDT terminates.
    NodeKey eventKey = {eventGuid.guid};
    NodeKey edtKey = {edtGuid.guid};
    //    if (computationGraph.find(edtGuid.guid) == computationGraph.end()) {
    //        computationGraph[edtGuid.guid] =
    //            new Node(edtGuid, 0, NULL, Node::INTERNAL);
    //    }
    assert(computationGraph.find(eventKey) != computationGraph.end());
    assert(computationGraph.find(edtKey) != computationGraph.end());

    Node* edt = computationGraph[edtKey];
    Node* event = computationGraph[eventKey];
    //	Node* db = computationGraph[dataGuid.guid];
    //	db->descent.splice(db->descent.end(), event->descent);
    //	event->descent.push_back(db);
    //	edt->descent.push_back(db);
    edt->addChild(event);
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

void outputLink(ostream& out, Node* n1, u16 epoch1, Node* n2, u16 epoch2,
                Node::EdgeType edgeType) {
    out << '\"' << n1->id;
    if (n1->type == Node::EDT) {
        out << '#' << epoch1;
    }
    out << '\"';
    out << " -> ";
    out << '\"' << n2->id;
    if (n2->type == Node::EDT) {
        out << '#' << epoch2;
    }
    out << '\"';
    out << ' ' << edgeColorSchemes[edgeType].toString();
    out << ';' << endl;
}

void CG2Dot() {
#if DEBUG
    cout << "CG2Dot" << endl;
#endif
    ofstream out;
    out.open("cg.dot");
    out << "digraph ComputationGraph {" << endl;
    cout << "total node num: " << computationGraph.size() << endl;
    for (map<NodeKey, Node *>::iterator ci = computationGraph.begin(),
                                        ce = computationGraph.end();
         ci != ce; ci++) {
        Node* node = ci->second;
        string nodeColor = nodeColorSchemes[node->type].toString();
        if (node->type == Node::EDT) {
            EDTNode* edtNode = static_cast<EDTNode*>(node);
            for (u16 i = 0; i <= edtNode->spawnEdges.size(); i++) {
                out << '\"' << node->id << "#" << i << '\"' << nodeColor << ";"
                    << endl;
            }
        } else {
            out << '\"' << node->id << '\"' << nodeColor << ";" << endl;
        }
    }

    for (map<NodeKey, Node *>::iterator ci = computationGraph.begin(),
                                        ce = computationGraph.end();
         ci != ce; ci++) {
        // cout << (uint64_t)ci->second << endl;
        Node* node = ci->second;
        if (node->type == Node::EDT) {
            EDTNode* edtNode = static_cast<EDTNode*>(node);
            for (u16 i = 0; i < edtNode->spawnEdges.size(); i++) {
                outputLink(out, edtNode, i, edtNode, i + 1, Node::CONTINUE);
            }

            for (u16 i = 0; i < edtNode->spawnEdges.size(); i++) {
                outputLink(out, edtNode, i, edtNode->getSpawnEdge(i),
                           START_EPOCH, Node::SPAWN);
            }

            for (list<Node *>::iterator ni = edtNode->children.begin(),
                                        ne = edtNode->children.end();
                 ni != ne; ni++) {
                Node* pointedNode = *ni;
                outputLink(out, edtNode, edtNode->spawnEdges.size(),
                           pointedNode, START_EPOCH, Node::JOIN);
            }
        } else {
            for (list<Node *>::iterator ni = node->children.begin(),
                                        ne = node->children.end();
                 ni != ne; ni++) {
                Node* pointedNode = *ni;
                outputLink(out, node, START_EPOCH, pointedNode, START_EPOCH,
                           Node::JOIN);
            }
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
                PIN_PARG_AGGREGATE(ocrGuid_t), PIN_PARG_AGGREGATE(ocrGuid_t),
                PIN_PARG_END());
            RTN_ReplaceSignature(
                rtn, AFUNPTR(afterEdtCreate), IARG_PROTOTYPE,
                proto_notifyEdtCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE,
                5, IARG_FUNCARG_ENTRYPOINT_VALUE, 6,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 7, IARG_FUNCARG_ENTRYPOINT_VALUE,
                8, IARG_END);
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
            cout << "replace notifyEdtStart" << endl;
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
//    if (!ip1File.empty() && !ip2File.empty()) {
    cout << ip1Type << "-" << ip2Type << " race detect!" << endl;
    cout << "first op is " << ip1 << " in " << ip1File << ": " << ip1Line
         << ": " << ip1Column << endl;
    cout << "second op is " << ip2 << " in " << ip2File << ": " << ip2Line
         << ": " << ip2Column << endl;
    abort();
//    }
}

void checkDataRace(ADDRINT ip, NodeKey& nodeKey, bool isRead,
                   BytePage* bytePage) {
    if (isRead) {
        if (bytePage->hasWrite()) {
            bool mhp = !isReachable(bytePage->write->edtKey,
                                    bytePage->write->epoch, nodeKey);
            if (mhp) {
                outputRaceInfo(bytePage->write->ip, false, ip, true);
            }
        }
    } else {
        if (bytePage->hasWrite()) {
            bool mhp = !isReachable(bytePage->write->edtKey,
                                    bytePage->write->epoch, nodeKey);
            if (mhp) {
                outputRaceInfo(bytePage->write->ip, false, ip, false);
            }
        }
        if (bytePage->hasRead()) {
            for (list<AccessRecord *>::iterator ai = bytePage->read.begin(),
                                                ae = bytePage->read.end();
                 ai != ae; ai++) {
                AccessRecord* ar = *ai;
                bool mhp = !isReachable(ar->edtKey, ar->epoch, nodeKey);
                if (mhp) {
                    outputRaceInfo((*ai)->ip, true, ip, false);
                }
            }
        }
    }
}

void recordMemRead(void* addr, uint32_t size, ADDRINT sp, ADDRINT ip) {
#if DEBUG
    cout << "record memory read\n";
#endif
    THREADID tid = PIN_ThreadId();
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    if (data->currentEdt) {
        DBPage* dbPage = data->getDB((uintptr_t)addr);
        NodeKey edtKey = {data->currentEdt->id};
        if (dbPage) {
            for (uint32_t i = 0; i < size; i++) {
                BytePage* current = dbPage->getBytePage((uintptr_t)addr + i);
                if (current) {
                    checkDataRace(ip, edtKey, true, current);
                }
            }
            AccessRecord* ar = new AccessRecord(edtKey, data->epoch, ip);
            dbPage->updateBytePages(ar, (uintptr_t)addr, size, true);
        }
    }
#if DEBUG
    cout << "record memory read finish\n";
#endif
}

void recordMemWrite(void* addr, uint32_t size, ADDRINT sp, ADDRINT ip) {
#if DEBUG
    cout << "record memory write\n";
#endif
    THREADID tid = PIN_ThreadId();
    ThreadLocalStore* data =
        static_cast<ThreadLocalStore*>(PIN_GetThreadData(tlsKey, tid));
    if (data->currentEdt) {
        DBPage* dbPage = data->getDB((uintptr_t)addr);
        NodeKey edtKey = {data->currentEdt->id};
        if (dbPage) {
            for (uint32_t i = 0; i < size; i++) {
                BytePage* current = dbPage->getBytePage((uintptr_t)addr + i);
                if (current) {
                    checkDataRace(ip, edtKey, false, current);
                }
            }
            AccessRecord* ar = new AccessRecord(edtKey, data->epoch, ip);
            dbPage->updateBytePages(ar, (uintptr_t)addr, size, false);
        }
    }
#if DEBUG
    cout << "record memory write finish\n";
#endif
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
#if DEBUG
    cout << "instrument image\n";
#endif
    if (isUserCodeImg(img)) {
        for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
            for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn);
                 rtn = RTN_Next(rtn)) {
                instrumentRoutine(rtn);
            }
        }
    }
#if DEBUG
    cout << "instrument image finish\n";
#endif
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
    nodeColorSchemes[Node::EDT] = a;
    nodeColorSchemes[Node::DB] = b;
    nodeColorSchemes[Node::EVENT] = c;
    nodeColorSchemes[Node::INTERNAL] = d;

    ColorScheme e("red", "bold"), f("cyan", "bold"), g("black", "bold");
    edgeColorSchemes[Node::SPAWN] = e;
    edgeColorSchemes[Node::JOIN] = f;
    edgeColorSchemes[Node::CONTINUE] = g;
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
    cout << "User image is " << userCodeImg << endl;
    IMG_AddInstrumentFunction(overload, 0);
    IMG_AddInstrumentFunction(instrumentImage, 0);
    PIN_AddThreadStartFunction(threadStart, 0);
    PIN_AddThreadFiniFunction(threadFini, 0);
    // PIN_AddFiniFunction(fini, 0);
    init();
    PIN_StartProgram();
    return 0;
}
