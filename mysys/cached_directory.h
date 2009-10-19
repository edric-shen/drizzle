/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 *   cached_directory.h
 *
 * @brief
 *   Defines the interface to the CachedDirectory class.
 */

#ifndef MYSYS_CACHED_DIRECTORY_H
#define MYSYS_CACHED_DIRECTORY_H

#include <iostream>
#include <vector>
#include <string>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>


/**
 * A utility class to handle processing the entries/files within a directory.
 *
 * This class will allow the user to either get a list of the entry names 
 * within a given directory.
 */
class CachedDirectory
{
  std::string path;

public:
  class Entry
  {
  public:
    std::string filename;
    Entry(std::string in_name)
      : filename(in_name)
    {}
  };
  typedef std::vector<Entry *> Entries;
private:
  int error; ///< Error code stored from various syscalls
  Entries entries; ///< Entries in the directory

public:
  /**
   * Emptry Constructor.
   *
   */
  CachedDirectory();
      
  /**
   * Constructor taking full directory path as sole parameter.
   *
   * @param[in] Path to the directory to open
   */
 CachedDirectory(const std::string &in_path); 
 /**
  * Destructor.  Cleans up any resources we've taken 
  */
  ~CachedDirectory();

  /**
   * Returns whether the CachedDirectory object is in a failed state
   */
  inline bool fail() const 
  {
    return error != 0;
  }

  /** 
   * Returns the stored error code of the last action the directory
   * object took (open, read, etc)
   */
  inline int getError() const
  {
    return error;
  }

  /** 
   * Returns the current path for the cached directory
   */
  inline const char *getPath() const
  {
    return path.c_str();
  }

  /**
   * Return the list of entries read from the directory
   *
   * @returns
   *   A vector of strings containing the directory entry names.
   */
  inline const Entries &getEntries()
  {
    return entries;
  }

  /**
   * Encapsulate the logic to open the directory.
   * @param[in] dirPath The path to the directory to open and read.
   * @retval true Success
   * @retval false Failure
   */
  bool open(const std::string &dirPath);

};

#endif  /* MYSYS_CACHED_DIRECTORY_H */
