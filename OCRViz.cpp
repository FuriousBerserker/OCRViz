#include <stdio.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
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

map<intptr_t, Node*> computationGraph;

map<Node::Type, ColorScheme> colorSchemes;

vector<string> skippedLibraries;

PIN_LOCK pinLock;

ColorScheme::ColorScheme(string color, string style)
    : color(color), style(style) {}

ColorScheme::ColorScheme() {}

ColorScheme::~ColorScheme() {}

string ColorScheme::toString() {
    return "[color=" + color + ", style=" + style + "]";
}

Node::Node(ocrGuid_t id, u32 depc, ocrGuid_t* depv, Node::Type type)
    : id(id.guid), type(type) {
    if (depv != NULL) {
        for (u32 i = 0; i < depc; i++) {
            ocrGuid_t dep = *(depv + i);
            if (computationGraph.find(dep.guid) == computationGraph.end()) {
                computationGraph[dep.guid] =
                    new Node(dep, 0, NULL, Node::INTERNAL);
            }
            computationGraph[dep.guid]->descent.push_back(this);
        }
    }
}

Node::~Node() {}

int usage() {
    cout << "This tool visualizes the runtime dependency of OCR "
            "applications and outputs computation graph."
         << endl;
    return -1;
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

//void argsMainEdt(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
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
    PIN_GetLock(&pinLock, threadid);
    computationGraph[guid.guid] = newDbNode;
    PIN_ReleaseLock(&pinLock);
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

void img(IMG img, void* v) {
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
//            RTN_InsertCall(mainEdtRTN, IPOINT_BEFORE, (AFUNPTR)argsMainEdt,
//                           IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
//                           IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
//                           IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
//                           IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);
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
            RTN_ReplaceSignatureProbed(
                rtn, AFUNPTR(afterEdtCreate), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
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
            RTN_ReplaceSignatureProbed(
                rtn, AFUNPTR(afterDbCreate), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
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
            RTN_ReplaceSignatureProbed(
                rtn, AFUNPTR(afterEventCreate), IARG_FUNCARG_ENTRYPOINT_VALUE,
                0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
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
            RTN_ReplaceSignatureProbed(
                rtn, AFUNPTR(afterAddDependence), IARG_FUNCARG_ENTRYPOINT_VALUE,
                0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE,
                3, IARG_END);
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
            RTN_ReplaceSignatureProbed(
                rtn, AFUNPTR(afterEventSatisfy), IARG_FUNCARG_ENTRYPOINT_VALUE,
                0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE,
                3, IARG_END);
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
            RTN_ReplaceSignatureProbed(rtn, AFUNPTR(fini), IARG_END);
            PROTO_Free(proto_notifyShutdown);
        }
    }
}

void initColorScheme() {
    ColorScheme a("green", "filled"), b("yellow", "filled"),
        c("blue", "filled"), d("black", "filled");
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
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) {
        return usage();
    }
    IMG_AddInstrumentFunction(img, 0);
    // PIN_AddFiniFunction(fini, 0);
    init();
    PIN_StartProgramProbed();
    return 0;
}
