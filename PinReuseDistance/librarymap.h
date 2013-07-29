/*
 * librarymap.h
 *
 *  Created on: Nov 19, 2009
 *      Author: dschuff
 */

#ifndef LIBRARYMAP_H_
#define LIBRARYMAP_H_

#include <list>
#include <string>
#include "pin.H"

/*
 * Class to map a PC to a image+offset, because pin loads libraries in different places
 * (also might be needed/wanted for the base executable which could be PIE)
 */
class LibraryMap {
public:
  class Entry {
  public:
    Entry(const std::string *image, ADDRINT offset) : image(image), offset(offset) {}
    const std::string *image;
    ADDRINT offset;
  };
  void AddImage(ADDRINT base_address, ADDRINT top_address, const std::string &name);
  struct Entry Lookup(ADDRINT address);
  std::string GetPythonString();
private:
  class LibraryEntry {
  public:
    LibraryEntry(std::string img_name, ADDRINT base_addr, ADDRINT top_addr) :
        name(img_name), base_address(base_addr), top_address(top_addr) {}
    std::string name;
    ADDRINT base_address;
    ADDRINT top_address;
  };
  static const std::string kInvalidString;
  std::list<LibraryEntry> images_;
};

#endif /* LIBRARYMAP_H_ */
