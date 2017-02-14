#include <stdio.h>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include "pin.H"
#include "ocr-types.h"

#define DEBUG 1

using namespace::std;

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
	enum Type {
		EDT,
		DB,
		EVENT,
		INTERNAL
	};
public:
	ocrGuid_t id;
	vector<Node*> deps;
	Type type;

	Node(ocrGuid_t id, uint32_t depc, ocrGuid_t* depv, Type type);
	virtual ~Node();
};

class ThreadData {
public:
	Node* latestNode;
	ocrGuid_t* guid;

	ThreadData(Node* latestNode, ocrGuid_t* guid);
	virtual ~ThreadData();
};

map<ocrGuid_t, Node*> computationGraph;

map<Node::Type, ColorScheme> colorSchemes;

ColorScheme::ColorScheme(string color, string style): color(color), style(style) {

}

ColorScheme::ColorScheme() {
}

ColorScheme::~ColorScheme() {

}

string ColorScheme::toString() {
	return "[color=" + color + ", style=" + style + "]"; 
}

Node::Node(ocrGuid_t id, uint32_t depc, ocrGuid_t* depv, Node::Type type): id(id), type(type) {
	deps.reserve(depc);
	if (depv != NULL) {
		for (uint32_t i = 0; i < depc; i++) {
			ocrGuid_t dep = *(depv + i);
			if (computationGraph.find(dep) == computationGraph.end()) {
				computationGraph[dep] = new Node(dep, 0, NULL, Node::INTERNAL);	
			}
			deps.push_back(computationGraph[dep]);
		}
	}
}

Node::~Node() {
	
}

ThreadData::ThreadData(Node* latestNode, ocrGuid_t* guid): latestNode(latestNode), guid(guid) {

}

ThreadData::~ThreadData() {

}

PIN_LOCK pinLock;
TLS_KEY tls_key;

int usage() {
	return -1;
}

void argsMainEdt(uint32_t paramc, uint64_t* paramv, uint32_t depc, ocrEdtDep_t depv[]) {
#if DEBUG
	cout << "argsMainEdt" << endl;
#endif
//	ocrGuid_t* depIdv = new ocrGuid_t[depc];
//	for (uint32_t i = 0; i < depc; i++) {
//		depIdv[i] = depv[i].guid;
//	}
//	Node* mainEdtNode = new Node(0, depc, depIdv, Node::EDT);
//	computationGraph[mainEdtNode->id] = mainEdtNode;
//	delete[] depIdv;
}

void argsDbCreate(ocrGuid_t* guid, void** addr, uint64_t len, uint16_t flags, ocrGuid_t affinity, ocrInDbAllocator_t allocator) {
#if DEBUG
	cout << "argsDbCreate" << endl;
#endif
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	PIN_GetLock(&pinLock, threadid);
//	Node* newDbNode = new Node(*guid, 0, NULL, Node::DB);
//	threadData->latestNode = newDbNode;
//	threadData->guid = guid;
//	PIN_ReleaseLock(&pinLock);
}

void argsEdtCreate(ocrGuid_t* guid, ocrGuid_t templateGuid, uint32_t paramc, uint64_t* paramv, uint32_t depc, ocrGuid_t* depv, uint16_t flags, ocrGuid_t affinity, ocrGuid_t* outputEvent) {
#if DEBUG
	cout << "argsEdtCreate" << endl;
#endif
//	if (depc >= 0xFFFFFFFE) {
//		uint32_t trueDepc = 0;
//		for (uint32_t i = 0; i < depc; i++) {
//			if (! *(depv + i)) {
//				break;
//			}
//			trueDepc++;
//		}
//		depc = trueDepc;
//	}
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	PIN_GetLock(&pinLock, threadid);
//	Node* newEdtNode = new Node(*guid, depc, depv, Node::EDT);
//	threadData->latestNode = newEdtNode;
//	threadData->guid = guid;
//	PIN_ReleaseLock(&pinLock);

}

void argsEventCreate(ocrGuid_t* guid, ocrEventTypes_t eventType, uint16_t flags) {
#if DEBUG
	cout << "argsEventCreate" << endl;
#endif
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	PIN_GetLock(&pinLock, threadid);
//	Node* newEventNode = new Node(*guid, 0, NULL, Node::EVENT);
//	threadData->latestNode = newEventNode;
//	threadData->guid = guid;
//	PIN_ReleaseLock(&pinLock);
}

void argsAddDependence(ocrGuid_t source, ocrGuid_t destination, uint32_t slot, ocrDbAccessMode_t mode) {
#if DEBUG
	cout << "argsAddDependence" << endl;
#endif
//	cout << source << "->" << destination << endl;
//	PIN_GetLock(&pinLock, PIN_ThreadId());
//	if (computationGraph.find(source) == computationGraph.end()) {
//		computationGraph[source] = new Node(source, 0, NULL, Node::INTERNAL);
//	}
//	if (computationGraph.find(destination) == computationGraph.end()) {
//		computationGraph[destination] = new Node(destination, 0, NULL, Node::INTERNAL);
//	}
//	computationGraph[destination]->deps.push_back(computationGraph[source]);
//	PIN_ReleaseLock(&pinLock);
}

void afterEdtCreate() {
#if DEBUG
	cout << "afterEdtCreate" << endl;
#endif
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	Node* node = threadData->latestNode;
//	node->id = *threadData->guid;
//	PIN_GetLock(&pinLock, threadid);
//	computationGraph[node->id] = node;
//	PIN_ReleaseLock(&pinLock);
}

void afterDbCreate() {
#if DEBUG
	cout << "afterDbCreate" << endl;
#endif
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	Node* node = threadData->latestNode;
//	node->id = *threadData->guid;
//	PIN_GetLock(&pinLock, threadid);
//	computationGraph[node->id] = node;
//	PIN_ReleaseLock(&pinLock);
}

void afterEventCreate() {
#if DEBUG
	cout << "afterEdtCreate" << endl;
#endif
//	THREADID threadid = PIN_ThreadId();
//	ThreadData* threadData = static_cast<ThreadData*>(PIN_GetThreadData(tls_key, threadid));
//	Node* node = threadData->latestNode;
//	node->id = *threadData->guid;
//	PIN_GetLock(&pinLock, threadid);
//	computationGraph[node->id] = node;
//	PIN_ReleaseLock(&pinLock);
}

void CG2Dot() {
#if DEBUG
	cout << "CG2Dot" << endl;
#endif
	ofstream out;
	out.open("cg.dot");
	out << "digraph ComputationGraph {" << endl;
	cout << computationGraph.size() << endl;
	for (map<ocrGuid_t, Node*>::iterator ci = computationGraph.begin(), ce = computationGraph.end(); ci != ce; ci++) {
		Node* node = ci->second;
		out<< node->id << colorSchemes[node->type].toString() << ";" << endl; 
	}
	for (map<ocrGuid_t, Node*>::iterator ci = computationGraph.begin(), ce = computationGraph.end(); ci != ce; ci++) {
		cout << (uint64_t)ci->second << endl;
		Node* node = ci->second;
		for (vector<Node*>::iterator ni = node->deps.begin(), ne = node->deps.end(); ni != ne; ni++) {
			out << 	(*ni)->id << " -> " << node->id << ";" << endl;
			//cout << (uint64_t)node << "<-----" << (uint64_t)*ni << endl;
		}	
	}
	out << "}";
	out.close();
}

void img(IMG img, void* v) {
#if DEBUG
	cout << "img: " << IMG_Name(img) << endl;
#endif

	//monitor mainEdt
	RTN mainEdtRTN = RTN_FindByName(img, "mainEdt");
	if (RTN_Valid(mainEdtRTN)) {
#if DEBUG
	cout << "instrument mainEdt" << endl;
#endif
		RTN_Open(mainEdtRTN);
//		RTN_InsertCall(mainEdtRTN, IPOINT_BEFORE, (AFUNPTR)argsMainEdt, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);
		RTN_Close(mainEdtRTN);
	}

	//monitor ocrEdtCreate
	RTN edtCreateRTN = RTN_FindByName(img, "ocrEdtCreate");
	if (RTN_Valid(edtCreateRTN)) {
#if DEBUG
	cout << "instrument ocrEdtCreate" << endl;
#endif
		RTN_Open(edtCreateRTN);
//		RTN_InsertCall(edtCreateRTN, IPOINT_BEFORE, (AFUNPTR)argsEdtCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE, 5, IARG_FUNCARG_ENTRYPOINT_VALUE, 6, IARG_FUNCARG_ENTRYPOINT_VALUE, 7, IARG_FUNCARG_ENTRYPOINT_VALUE, 8, IARG_END);
//		RTN_InsertCall(edtCreateRTN, IPOINT_AFTER, (AFUNPTR)afterEdtCreate, IARG_END);
		RTN_Close(edtCreateRTN);
	}

	//monitor ocrDbCreate
	RTN dbCreateRTN = RTN_FindByName(img, "ocrDbCreate");
	if (RTN_Valid(dbCreateRTN)) {
#if DEBUG
	cout << "instrument ocrDbCreate" << endl;
#endif
		RTN_Open(dbCreateRTN);
		RTN_InsertCall(dbCreateRTN, IPOINT_BEFORE, (AFUNPTR)argsDbCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE, 5, IARG_END);
		RTN_InsertCall(dbCreateRTN, IPOINT_AFTER, (AFUNPTR)afterDbCreate, IARG_END);
		RTN_Close(dbCreateRTN);
	}

	//monitor ocrEventCreate
	RTN eventCreateRTN = RTN_FindByName(img, "ocrEventCreate");
	if (RTN_Valid(eventCreateRTN)) {
#if DEBUG
	cout << "instrument ocrEventCreate" << endl;
#endif
		RTN_Open(eventCreateRTN);
//		RTN_InsertCall(eventCreateRTN, IPOINT_BEFORE, (AFUNPTR)argsEventCreate, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END);
//		RTN_InsertCall(eventCreateRTN, IPOINT_AFTER, (AFUNPTR)afterEventCreate, IARG_END);
		RTN_Close(eventCreateRTN);
	}

	//monitor ocrAddDependency
	RTN addDependenceRTN = RTN_FindByName(img, "ocrAddDependence");
	if (RTN_Valid(addDependenceRTN)) {
#if DEBUG
	cout << "instrument ocrAddDependence" << endl;
#endif
		RTN_Open(addDependenceRTN);
//		RTN_InsertCall(addDependenceRTN, IPOINT_BEFORE, (AFUNPTR)argsAddDependence, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3, IARG_END);
		RTN_Close(addDependenceRTN);
	}
}

void threadStart(THREADID threadid, CONTEXT* cxt, int32_t flags, void* v) {
#if DEBUG
	cout << "thread start" << endl;
#endif
	ThreadData* threadData = new ThreadData(NULL, NULL);
	PIN_SetThreadData(tls_key, threadData, threadid);
}

void fini(int code, void* v) {
#if DEBUG
	cout << "fini" << endl;
#endif
	CG2Dot();
	for (map<ocrGuid_t, Node*>::iterator ci = computationGraph.begin(), ce = computationGraph.end(); ci != ce; ci++) {
		delete ci->second;
	}
}

void initColorScheme() {
	ColorScheme a("green", "filled"), b("yellow", "filled"), c("blue", "filled"), d("black", "filled");
	colorSchemes[Node::EDT] = a;
	colorSchemes[Node::DB] = b;
	colorSchemes[Node::EVENT] = c;
	colorSchemes[Node::INTERNAL] = d;
}

void init() {
	initColorScheme();
}

int main(int argc, char* argv[]) {
	PIN_InitLock(&pinLock);
	tls_key = PIN_CreateThreadDataKey(NULL);
	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) {
		return usage();
	}
	IMG_AddInstrumentFunction(img, 0);
	PIN_AddThreadStartFunction(threadStart, 0);
	PIN_AddFiniFunction(fini, 0);
	init();
	PIN_StartProgram();
	return 0;
}
