/*
  Martin Galpin (m@66laps.com)
  
  Copyright (c) 2010 66laps Limited. All rights reserved.
  
  This file is part of rFactor-OpenMotorsport.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include "Utilities.hpp"

#include <ctime>

const std::string GetISO8601Date(tm* date)
{
  char* buf = new char[80]; // magic number, 80 is enough for an ISO-8601 date
  strftime(buf, 80, "%Y-%m-%dT%H:%M:%S", date);
  std::string str(buf);
  delete [] buf;
  return str;
}