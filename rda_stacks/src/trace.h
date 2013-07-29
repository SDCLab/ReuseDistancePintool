/*
 * trace.h
 *
 *  Created on: Mar 5, 2010
 *      Author: dschuff
 */

#ifndef TRACE_H_
#define TRACE_H_

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include "reusestack-common.h"
#include "rda-sync.h"

//#define TRACE_SAMPLES

class TraceElement {
public:
    enum Type { kNewAddress, kAccess, kMerge, kEnable };
    TraceElement() : type(kAccess), is_write(false), thread(0), address(0)  {}
    TraceElement(Type t, int th, address_t addr, bool write) : type(t), is_write(write),
        thread(th), address(addr) {}
    //std::string toString() {return std::string("") + }
    Type type;
    bool is_write;
    bool enable;
    int thread;
    address_t address;
};

#if defined(UNITTEST) || !defined(TRACE_SAMPLES)
class OutputTrace {
public:
  OutputTrace(std::string filename) {
  }
  void TraceNewSampledAddress(int thread, address_t address) {
  }
  void TraceAccess(int thread, address_t address, bool is_write) {
  }
  void TraceMerge(int thread) {}
  void TraceThreadEnable(int thread, bool enable) {}
};

class ThreadedOutputTrace  {
public:
  const static int kMaxThreads = 8;
  void Init(std::string filename) {
  }
  void CleanUp() {

  }
  void TraceNewSampledAddress(int thread, address_t address) {

  }
  void TraceAccess(int thread, address_t address, bool is_write) {

  }
  void TraceMerge(int thread) {

  }
};
#else
class OutputTrace {
public:
  OutputTrace(std::string filename) : tracefile_() {
    tracefile_.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    tracefile_.open(filename.c_str(), std::ios::out);
  }
  void TraceNewSampledAddress(int thread, address_t address) {
    newaddrs_++;
    tracefile_ << "N " << thread << " " << std::hex << address << std::endl;
  }
  void TraceAccess(int thread, address_t address, bool is_write) {
    accesses_++;
    tracefile_ << "A " << thread << " " << std::hex << address
        << (is_write ? " w" : " r") << std::endl;
  }
  void TraceMerge(int thread) {
    tracefile_ << "M " << thread << std::endl;
  }
  void TraceThreadEnable(int thread, bool enable) {
    tracefile_ << "E " << thread << " " << (enable ? "t" : "f") << std::endl;
  }
  ~OutputTrace() {
    std::cout << "newaddrs " << newaddrs_ << " accesses " << accesses_ << std::endl;
    tracefile_.close();
  }
private:
  acc_count_t newaddrs_;
  acc_count_t accesses_;
  std::ofstream tracefile_;
};

class ThreadedOutputTrace  {
public:
  const static int kMaxThreads = 8;
  void Init(std::string filename) {
    tracefile_.exceptions(std::ofstream::eofbit | std::ofstream::failbit | std::ofstream::badbit);
    tracefile_.open(filename.c_str(), std::ios::out);
    for (int i = 0; i < kMaxThreads; i++) {
      buffers_[i] = new std::vector<TraceElement>;
    }
    RdaInitLock(&lock_);
  }
  void CleanUp() {
    tracefile_.close();
    for (int i = 0; i < kMaxThreads; i++) {
      delete buffers_[i];
    }
  }
  void TraceNewSampledAddress(int thread, address_t address) {
    buffers_[thread]->push_back(TraceElement(TraceElement::kNewAddress, thread, address, false));
  }
  void TraceAccess(int thread, address_t address, bool is_write) {
    buffers_[thread]->push_back(TraceElement(TraceElement::kAccess, thread, address, is_write));
  }
  void TraceMerge(int thread) {
    LockHolder l(&lock_);//necessary? or ensure called by only one thread?
    for (int i = 0; i < kMaxThreads; i++) {
      for (std::vector<TraceElement>::iterator it = buffers_[i]->begin(); it != buffers_[i]->end();
          ++it) {
        if (it->type == TraceElement::kNewAddress) {
          WriteNewSampledAddress(it->thread, it->address);
          newaddrs_++;
        } else {
          accesses_++;
          WriteAccess(it->thread, it->address, it->is_write);
        }
      }
      buffers_[i]->clear();
    }
  }
  ~ThreadedOutputTrace() {
    TraceMerge(0);
    std::cout << "newaddrs " << newaddrs_ << " accesses " << accesses_ << std::endl;
    tracefile_.close();
  }
private:
  void WriteNewSampledAddress(int thread, address_t address) {
    tracefile_ << "N " << thread << " " << std::hex << address << std::endl;
  }
  void WriteAccess(int thread, address_t address, bool is_write) {
    tracefile_ << "A " << thread << " " << std::hex << address
        << (is_write ? " w" : " r") << std::endl;
  }
  std::vector<TraceElement> *buffers_[kMaxThreads];
  std::ofstream tracefile_;
  RdaLock lock_;
  acc_count_t newaddrs_;
  acc_count_t accesses_;
};
#endif

class InputTrace {
public:

  InputTrace(const std::string &filename) : tracefile_(), linecount_() {
    tracefile_.exceptions(std::ifstream::eofbit | std::ifstream::failbit | std::ifstream::badbit);
    tracefile_.open(filename.c_str());
  }
  const TraceElement *GetNext() {
    char type('U');
    char write('U');
    char enable('U');
    linecount_++;
    try {
      tracefile_ >> type;
      switch (type) {
        case 'N':
          tracefile_ >> element_.thread >> std::hex >> element_.address;
          element_.type = TraceElement::kNewAddress;
          break;
        case 'A':
          tracefile_ >> element_.thread >> std::hex >> element_.address >> write;
          element_.type = TraceElement::kAccess;
          element_.is_write = write == 'w' ? true : false;
          break;
        case 'E':
          tracefile_ >> element_.thread >> enable;
          element_.type = TraceElement::kEnable;
          element_.enable = enable == 't' ? true : false;
          break;
        case 'M':
          tracefile_ >> element_.thread;
          element_.type = TraceElement::kMerge;
          break;
        default:
          std::cout << "malformed trace file on line " << linecount_;
          throw std::runtime_error("");
      }
    } catch (std::ios_base::failure ex) {
      return NULL;
    }

    //std::cout << "entry |" << type << "|"<< element_.thread <<"|" << element_.address << "|"<<write << std::endl;
    return &element_;
  }
private:
  std::ifstream tracefile_;
  TraceElement element_;
  int linecount_;
};

#endif /* TRACE_H_ */
