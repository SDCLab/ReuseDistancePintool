/*
 * librarymap.cc
 *
 *  Created on: Nov 19, 2009
 *      Author: dschuff
 */

#include "librarymap.h"

const std::string LibraryMap::kInvalidString("INVALID");

void LibraryMap::AddImage(ADDRINT base_address, ADDRINT top_address, const std::string &name) {
  images_.push_back(LibraryEntry(name, base_address, top_address));
}

struct LibraryMap::Entry LibraryMap::Lookup(ADDRINT address) {
  for (std::list<LibraryEntry>::iterator iter = images_.begin(); iter != images_.end(); ++iter) {
    if (address >= iter->base_address && address <= iter->top_address) {
      return Entry(&iter->name, address - iter->base_address);
    }
  }
  return LibraryMap::Entry(&kInvalidString, 0);
}

std::string LibraryMap::GetPythonString() {
  std::string out("import collections\n"
                  "LME = collections.namedtuple('LME', 'name base top')\n"
                  "LibraryMap = [");
  for (std::list<LibraryEntry>::iterator iter = images_.begin(); iter != images_.end(); ++iter) {
    out += "LME('" + iter->name + "'," + hexstr(iter->base_address) + "," + hexstr(iter->top_address)
        + "),";
  }
  out += "]\n";
  return out;
}

